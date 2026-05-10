#include "Eap2Common.h"
#include "Eap2Version.h"
#include "MidiParser.h"

struct VisNote {
    uint32_t startTick;
    uint32_t endTick;
    uint8_t pitch;
    uint8_t velocity;
    uint8_t channel;
};

struct InstanceData {
    MidiParser parser;
    std::vector<VisNote> notes;
    std::string lastPath;
    bool isLoaded = false;

    void Rebuild() {
        notes.clear();
        const auto& evs = parser.GetEvents();

        struct AInfo {
            uint32_t tick;
            uint8_t vel;
            bool on;
        };
        std::vector<AInfo> active(16 * 128, { 0, 0, false });

        for (const auto& e : evs) {
            const uint8_t type = e.status & 0xF0;
            const uint8_t ch = e.status & 0x0F;
            const int32_t key = ch * 128 + e.data1;

            if (type == 0x90 && e.data2 > 0) {
                if (active[key].on)
                    notes.push_back({ active[key].tick, e.absoluteTick,
                                      e.data1, active[key].vel, ch });
                active[key] = { e.absoluteTick, e.data2, true };

            } else if (type == 0x80 || (type == 0x90 && e.data2 == 0)) {
                if (active[key].on) {
                    notes.push_back({ active[key].tick, e.absoluteTick,
                                      e.data1, active[key].vel, ch });
                    active[key].on = false;
                }
            }
        }
    }
};

std::map<int64_t, InstanceData> g_inst;
std::mutex g_mtx;

std::filesystem::path FromUtf8(const char* s) {
    const int32_t n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

InstanceData* GetInst(int64_t id) {
    auto it = g_inst.find(id);
    return (it != g_inst.end() && it->second.isLoaded) ? &it->second : nullptr;
}

void F_LoadMidi(SCRIPT_MODULE_PARAM* p) {
    const int64_t id = static_cast<int64_t>(p->get_param_double(0));
    const LPCSTR path = p->get_param_string(1);
    if (!path || !path[0]) {
        p->push_result_boolean(false);
        return;
    }

    std::lock_guard<std::mutex> lk(g_mtx);
    auto& inst = g_inst[id];

    if (inst.isLoaded && inst.lastPath == path) {
        p->push_result_boolean(true);
        return;
    }

    if (inst.parser.Load(FromUtf8(path))) {
        inst.Rebuild();
        inst.isLoaded = true;
        inst.lastPath = path;
        p->push_result_boolean(true);
    } else {
        inst.isLoaded = false;
        p->push_result_boolean(false);
    }
}

void F_IsLoaded(SCRIPT_MODULE_PARAM* p) {
    const int64_t id = static_cast<int64_t>(p->get_param_double(0));
    std::lock_guard<std::mutex> lk(g_mtx);
    p->push_result_boolean(GetInst(id) != nullptr);
}

void F_GetTPQN(SCRIPT_MODULE_PARAM* p) {
    const int64_t id = static_cast<int64_t>(p->get_param_double(0));
    std::lock_guard<std::mutex> lk(g_mtx);
    const auto* inst = GetInst(id);
    p->push_result_int(inst ? static_cast<int32_t>(inst->parser.GetTPQN()) : 480);
}

void F_GetTickAtTime(SCRIPT_MODULE_PARAM* p) {
    const int64_t id = static_cast<int64_t>(p->get_param_double(0));
    const double t = p->get_param_double(1);
    std::lock_guard<std::mutex> lk(g_mtx);
    const auto* inst = GetInst(id);
    p->push_result_double(inst ? static_cast<double>(inst->parser.GetTickAtTime(t)) : 0.0);
}

void F_GetBpmAtTime(SCRIPT_MODULE_PARAM* p) {
    const int64_t id = static_cast<int64_t>(p->get_param_double(0));
    const double t = p->get_param_double(1);
    std::lock_guard<std::mutex> lk(g_mtx);
    const auto* inst = GetInst(id);
    p->push_result_double(inst ? inst->parser.GetBpmAtTime(t) : 120.0);
}

void F_GetTimeSigAt(SCRIPT_MODULE_PARAM* p) {
    const int64_t id = static_cast<int64_t>(p->get_param_double(0));
    const uint32_t tick = static_cast<uint32_t>(p->get_param_double(1));
    std::lock_guard<std::mutex> lk(g_mtx);
    const auto* inst = GetInst(id);
    if (!inst) {
        p->push_result_int(4);
        p->push_result_int(4);
        return;
    }
    const auto ts = inst->parser.GetTimeSignatureAt(tick);
    p->push_result_int(ts.numerator);
    p->push_result_int(ts.denominator);
}

void F_GetVisibleNotes(SCRIPT_MODULE_PARAM* p) {
    const int64_t id = static_cast<int64_t>(p->get_param_double(0));
    const double curTick = p->get_param_double(1);
    const double window = p->get_param_double(2);
    const int32_t minPitch = p->get_param_int(3);
    const int32_t maxPitch = p->get_param_int(4);
    const int32_t minVel = p->get_param_int(5);
    const int32_t chFilt = p->get_param_int(6);
    const bool hidePrc = p->get_param_boolean(7);

    std::lock_guard<std::mutex> lk(g_mtx);
    int32_t dummy = 0;
    const auto* inst = GetInst(id);
    if (!inst) {
        p->push_result_array_int(&dummy, 0);
        return;
    }

    const double lo = curTick - window;
    const double hi = curTick + window;

    std::vector<int32_t> out;
    out.reserve(512);
    for (const auto& n : inst->notes) {
        if (static_cast<double>(n.endTick) < lo) continue;
        if (static_cast<double>(n.startTick) > hi) continue;
        if (n.pitch < minPitch || n.pitch > maxPitch) continue;
        if (n.velocity < minVel) continue;
        if (chFilt && n.channel != chFilt - 1) continue;
        if (hidePrc && n.channel == 9) continue;
        out.push_back(static_cast<int32_t>(n.startTick));
        out.push_back(static_cast<int32_t>(n.endTick));
        out.push_back(n.pitch);
        out.push_back(n.velocity);
        out.push_back(n.channel);
    }

    if (out.empty()) {
        p->push_result_array_int(&dummy, 0);
        return;
    }
    p->push_result_array_int(out.data(), static_cast<int32_t>(out.size()));
}

void F_GetActiveKeys(SCRIPT_MODULE_PARAM* p) {
    const int64_t id = static_cast<int64_t>(p->get_param_double(0));
    const double curTick = p->get_param_double(1);
    const int32_t minPitch = p->get_param_int(2);
    const int32_t maxPitch = p->get_param_int(3);

    std::lock_guard<std::mutex> lk(g_mtx);
    int32_t dummy = 0;
    const auto* inst = GetInst(id);
    if (!inst) {
        p->push_result_array_int(&dummy, 0);
        return;
    }

    std::vector<int32_t> out;
    for (const auto& n : inst->notes) {
        if (static_cast<double>(n.startTick) <= curTick &&
            curTick < static_cast<double>(n.endTick)) {
            if (n.pitch >= minPitch && n.pitch <= maxPitch)
                out.push_back(n.pitch);
        }
    }
    if (out.empty()) {
        p->push_result_array_int(&dummy, 0);
        return;
    }
    p->push_result_array_int(out.data(), static_cast<int32_t>(out.size()));
}

void F_Unload(SCRIPT_MODULE_PARAM* p) {
    const int64_t id = static_cast<int64_t>(p->get_param_double(0));
    std::lock_guard<std::mutex> lk(g_mtx);
    g_inst.erase(id);
}

SCRIPT_MODULE_FUNCTION module_funcs[] = {
    { L"LoadMidi", F_LoadMidi },
    { L"IsLoaded", F_IsLoaded },
    { L"GetTPQN", F_GetTPQN },
    { L"GetTickAtTime", F_GetTickAtTime },
    { L"GetBpmAtTime", F_GetBpmAtTime },
    { L"GetTimeSigAt", F_GetTimeSigAt },
    { L"GetVisibleNotes", F_GetVisibleNotes },
    { L"GetActiveKeys", F_GetActiveKeys },
    { L"Unload", F_Unload },
    { nullptr, nullptr }
};
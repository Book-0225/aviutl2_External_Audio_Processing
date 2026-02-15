#include "Eap2Common.h"
#include "MidiParser.h"
#include "SynthCommon.h"
#include <map>
#include <vector>
#include <mutex>
#include <filesystem>
#include <cmath>
#include <algorithm>

constexpr auto TOOL_NAME = L"MIDI Generator";

FILTER_ITEM_FILE midi_file(L"MIDI File", L"", L"MIDI File (*.mid)\0*.mid;*.midi\0");
FILTER_ITEM_SELECT midi_type(L"Type", 0, gen_type_list);
FILTER_ITEM_SELECT::ITEM list_sync_mode[] = {
    { L"同期しない", 0 },
    { L"MIDIにBPMを同期", 1 },
    { L"AviUtlにBPMを同期", 2 },
    { nullptr }
};
FILTER_ITEM_SELECT midi_sync_mode(L"BPMの同期", 0, list_sync_mode);
FILTER_ITEM_TRACK midi_fixed_bpm(L"BPM(手動)", 120.0, 1.0, 999.0, 0.1);
FILTER_ITEM_TRACK midi_offset(L"オフセット", 0.0, -100.0, 100.0, 0.01);
FILTER_ITEM_TRACK midi_timbre(L"音色", 0.0, 0.0, 1.0, 0.01);
FILTER_ITEM_TRACK midi_attack(L"Attack", 0.0, 0.0, 2000.0, 1.0);
FILTER_ITEM_TRACK midi_decay(L"Decay", 0.0, 0.0, 5000.0, 1.0);
FILTER_ITEM_TRACK midi_sustain(L"Sustain", 0.0, -60.0, 0.0, 0.1);
FILTER_ITEM_TRACK midi_release(L"Release", 0.0, 0.0, 5000.0, 1.0);
FILTER_ITEM_TRACK midi_cutoff(L"Cutoff", 1.0, 0.0, 1.0, 0.01);
FILTER_ITEM_TRACK midi_reso(L"Resonance", 0.0, 0.0, 1.0, 0.01);
FILTER_ITEM_TRACK midi_detune(L"Detune", 0.2, 0.0, 1.0, 0.01);

void* filter_items_midi_gen[] = {
    &midi_file,
    &midi_type,
    &midi_sync_mode,
    &midi_fixed_bpm,
    &midi_offset,
    &midi_timbre,
    &midi_attack,
    &midi_decay,
    &midi_sustain,
    &midi_release,
    &midi_cutoff,
    &midi_reso,
    &midi_detune,
    nullptr
};

class MidiPlayer {
public:
    MidiParser parser;
    std::filesystem::path currentPath;
    std::vector<VoiceState> voices;
    static const int32_t MAX_VOICES = 64;
    int64_t last_sample_pos = -1;
    size_t next_event_index = 0;

    MidiPlayer() {
        voices.resize(MAX_VOICES);
        for (auto& v : voices) v.init();
    }

    bool Load(const std::filesystem::path& path) {
        if (currentPath == path && !parser.GetEvents().empty()) return true;
        if (parser.Load(path)) {
            currentPath = path;
            AllNotesOff();
            return true;
        }
        return false;
    }

    void AllNotesOff() {
        for (auto& v : voices) {
            v.active = false;
            v.noteNumber = -1;
            v.noteOnTime = 0.0;
            v.noteOffTime = -1.0;
            v.reset_filter();
        }
        last_sample_pos = -1;
        next_event_index = 0;
    }

    VoiceState* AllocVoice(int32_t note, float velocity, double time) {
        VoiceState* target = nullptr;
        for (auto& v : voices) if (!v.active) { target = &v; break; }
        if (!target) for (auto& v : voices) if (v.noteOffTime >= 0.0) { target = &v; break; }
        if (!target) {
            double oldest = 1e16;
            for (auto& v : voices) if (v.noteOnTime < oldest) { oldest = v.noteOnTime; target = &v; }
        }
        if (target) {
            target->init();
            target->active = true;
            target->noteNumber = note;
            target->velocity = velocity;
            target->noteOnTime = time;
            target->noteOffTime = -1.0;
            return target;
        }
        return &voices[0];
    }

    void NoteOff(int32_t note, double time) {
        VoiceState* target = nullptr;
        double oldest = 1e16;
        for (auto& v : voices) {
            if (v.active && v.noteNumber == note && v.noteOffTime < 0.0) {
                if (v.noteOnTime < oldest) {
                    oldest = v.noteOnTime;
                    target = &v;
                }
            }
        }
        if (target) target->noteOffTime = time;
    }

    void PreRoll(double current_time_sec, const std::function<int64_t(double)>& TimeToTickFunc, double release_len) {
        AllNotesOff();
        int64_t current_tick = TimeToTickFunc(current_time_sec);
        if (current_tick <= 0) return;
        const auto& events = parser.GetEvents();
        struct NoteInfo { int32_t velocity; int64_t onTick; };
        std::map<int, NoteInfo> active_notes;
        for (size_t i = 0; i < events.size(); ++i) {
            const auto& ev = events[i];
            if (ev.absoluteTick > current_tick) {
                next_event_index = i;
                break;
            }
            next_event_index = i + 1;
            uint8_t status = ev.status & 0xF0;
            if (status == 0x90) {
                if (ev.data2 > 0) active_notes[ev.data1] = { ev.data2, ev.absoluteTick };
                else active_notes.erase(ev.data1);
            }
            else if (status == 0x80) {
                active_notes.erase(ev.data1);
            }
        }
        for (const auto& kv : active_notes) {
            int32_t note = kv.first;
            int32_t vel = kv.second.velocity;
            VoiceState* v = AllocVoice(note, vel / 127.0f, current_time_sec - 0.05);
        }
    }

    void ProcessMidiEvent(const RawMidiEvent& ev, double time) {
        uint8_t status = ev.status & 0xF0;
        uint8_t channel = ev.status & 0x0F;
        if (status == 0x90) {
            if (ev.data2 > 0) AllocVoice(ev.data1, ev.data2 / 127.0f, time);
            else NoteOff(ev.data1, time);
        }
        else if (status == 0x80) {
            NoteOff(ev.data1, time);
        }
        else if (status == 0xE0) {
            int32_t val = MidiParser::CombineBytes14(ev.data1, ev.data2);
            currentPitchBend = val;
        }
        else if (status == 0xB0) {
            if (ev.data1 == 1) currentModWheel = ev.data2 / 127.0f;
            else if (ev.data1 == 7) currentVolume = ev.data2 / 127.0f;
            else if (ev.data1 == 10) currentPan = ev.data2 / 127.0f;
            else if (ev.data1 == 74) currentCutoffCC = ev.data2 / 127.0f;
        }
    }
    int32_t currentPitchBend = 8192;
    float currentModWheel = 0.0f;
    float currentVolume = 1.0f;
    float currentPan = 0.5f;
    float currentCutoffCC = 0.5f;
};

static std::mutex g_midi_mutex;
static std::map<const void*, MidiPlayer> g_midi_players;

bool func_proc_audio_midi(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int64_t current_obj_sample_index = audio->object->sample_index;
    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    if (midi_file.value == nullptr) return true;
    std::filesystem::path path(midi_file.value);
    if (path.empty()) return true;
    SynthParams p;
    p.type = midi_type.value;
    p.attack = midi_attack.value / 1000.0;
    p.decay = midi_decay.value / 1000.0;
    p.sustain = static_cast<float>(std::pow(10.0, midi_sustain.value / 20.0));
    p.release = midi_release.value / 1000.0;
    p.timbre = static_cast<float>(midi_timbre.value);
    p.freq = 440.0f;
    p.filter_cutoff = static_cast<float>(midi_cutoff.value);
    p.filter_res = static_cast<float>(midi_reso.value);
    p.detune = static_cast<float>(midi_detune.value);
    double offset_sec = midi_offset.value;
    double fixed_bpm = midi_fixed_bpm.value;
    int32_t mode = midi_sync_mode.value;
    MidiPlayer* player = nullptr;
    bool seek_detected = false;
    {
        std::lock_guard<std::mutex> lock(g_midi_mutex);
        player = &g_midi_players[audio->object];
        if (!player->Load(path)) return true;
        if (player->last_sample_pos != -1 &&
            std::abs(player->last_sample_pos - current_obj_sample_index) > 100) {
            seek_detected = true;
        }
        player->last_sample_pos = current_obj_sample_index + total_samples;
    }
    uint16_t tpqn = player->parser.GetTPQN();
    double global_bpm = g_shared_bpm.load();
    double tick_factor = 0.0;
    if (mode == 0) tick_factor = (fixed_bpm * tpqn) / 60.0;
    else if (mode == 2) tick_factor = (global_bpm * tpqn) / 60.0;
    auto TimeToTick = [&](double t_sec) -> int64_t {
        double midi_time = (t_sec - offset_sec);
        if (midi_time < 0) return -1;
        if (mode == 1) return player->parser.GetTickAtTime(midi_time);
        else return (int64_t)(midi_time * tick_factor);
        };

    if (seek_detected || current_obj_sample_index == 0) {
        double current_abs_time = (double)current_obj_sample_index / Fs;
        player->PreRoll(current_abs_time, TimeToTick, p.release);
    }

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }
    std::fill(bufL.begin(), bufL.begin() + total_samples, 0.0f);
    std::fill(bufR.begin(), bufR.begin() + total_samples, 0.0f);
    const auto& events = player->parser.GetEvents();
    for (int32_t i = 0; i < total_samples; ++i) {
        double current_rel_time = (double)(current_obj_sample_index + i) / Fs;
        int64_t current_tick = TimeToTick(current_rel_time);
        if (current_tick >= 0) {
            while (player->next_event_index < events.size()) {
                const auto& ev = events[player->next_event_index];
                if (ev.absoluteTick > current_tick) break;
                player->ProcessMidiEvent(ev, current_rel_time);
                player->next_event_index++;
            }
        }
        StereoSample mix = { 0.0f, 0.0f };
        for (auto& v : player->voices) {
            if (!v.active) continue;
            v.modWheel = player->currentModWheel;
            v.volume = player->currentVolume;
            v.pan = player->currentPan;
            float freq_hz = MidiParser::CalculateFrequency(v.noteNumber, player->currentPitchBend);
            p.freq = freq_hz;
            double note_elapsed = current_rel_time - v.noteOnTime;
            double effective_duration = 100000.0;
            if (v.noteOffTime >= 0.0) {
                effective_duration = v.noteOffTime - v.noteOnTime;
                if (effective_duration < 0) effective_duration = 0;
                if (note_elapsed > effective_duration + p.release) {
                    v.active = false;
                    continue;
                }
            }
            if (note_elapsed < 0) continue;
            StereoSample s = GenerateSampleStereo(v, p, note_elapsed, Fs, effective_duration) / 2;
            s = s * v.velocity;
            mix += s;
        }
        bufL[i] = static_cast<float>(mix.l);
        bufR[i] = static_cast<float>(mix.r);
    }
    if (audio->object->channel_num >= 1) audio->set_sample_data(bufL.data(), 0);
    if (audio->object->channel_num >= 2) audio->set_sample_data(bufR.data(), 1);
    return true;
}

void CleanupMidiGeneratorResources() {
    std::lock_guard<std::mutex> lock(g_midi_mutex);
    g_midi_players.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_midi_gen = {
    TYPE_AUDIO_MEDIA,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_midi_gen,
    nullptr,
    func_proc_audio_midi
};
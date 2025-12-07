#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <random>
#include <string>
#include "Avx2Utils.h"
#include "StringUtils.h"
#include "PluginManager.h"

#define TOOL_NAME L"Generator"

FILTER_ITEM_SELECT::ITEM gen_type_list[] = {
    { L"Sine", 0 },
    { L"Square", 1 },
    { L"Triangle", 2 },
    { L"Saw", 3 },
    { L"White Noise", 4 },
    { L"Pink Noise", 5 },
    { nullptr }
};
FILTER_ITEM_SELECT gen_type(L"Waveform", 0, gen_type_list);
FILTER_ITEM_TRACK gen_freq(L"Frequency", 440.0, 20.0, 20000.0, 1.0);
struct GenData {
    char uuid[40] = { 0 };
};
FILTER_ITEM_DATA<GenData> gen_data(L"GEN_DATA");

void* filter_items_generator[] = {
    &gen_type,
    &gen_freq,
    &gen_data,
    nullptr
};

const int32_t BLOCK_SIZE = 64;

struct GeneratorState {
    double phase = 0.0;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() { phase = 0.0; b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f; initialized = true; }
    void clear() { if (initialized) init(); }
};

struct GenParamCache {
    int32_t last_type = -1;
};

static std::mutex g_gen_state_mutex;
static std::map<const void*, GeneratorState> g_gen_states;

static std::mutex g_gen_param_cache_mutex;
static std::map<std::string, GenParamCache> g_gen_param_cache;

static std::mt19937 g_rng(12345);
static std::uniform_real_distribution<float> g_dist(-1.0f, 1.0f);

std::wstring GetGenParamsString(int32_t type) {
    std::wstring typeName = L"Unknown";
    switch (type) {
    case 0: typeName = L"Sine"; break;
    case 1: typeName = L"Square"; break;
    case 2: typeName = L"Triangle"; break;
    case 3: typeName = L"Saw"; break;
    case 4: typeName = L"White Noise"; break;
    case 5: typeName = L"Pink Noise"; break;
    }
    return L" (" + typeName + L")";
}

struct RenameParam {
    std::wstring newName;
    std::wstring oldNameCandidate;
    std::wstring defaultName;
    std::string id;
};

static void func_proc_check_and_rename(void* param, EDIT_SECTION* edit) {
    RenameParam* p = (RenameParam*)param;
    OBJECT_HANDLE obj = nullptr;
    int32_t max_layer = edit->info->layer_max;
    for (int32_t layer = 0; layer <= max_layer; ++layer) {
        OBJECT_HANDLE obj_temp = edit->find_object(layer, 0);
        while (!obj) {
            int32_t effect_count = edit->count_object_effect(obj_temp, GEN_TOOL_NAME(TOOL_NAME));
            for (int32_t i = 0; i < effect_count; ++i) {
                std::wstring indexed_filter_name = std::wstring(GEN_TOOL_NAME(TOOL_NAME));
                if (i > 0) indexed_filter_name += L":" + std::to_wstring(i);
                LPCSTR hex_encoded_id_str = edit->get_object_item_value(obj_temp, indexed_filter_name.c_str(), gen_data.name);
                if (hex_encoded_id_str && hex_encoded_id_str[0] != '\0') {
                    if (StringUtils::HexToString(hex_encoded_id_str) == p->id) {
                        obj = obj_temp;
                        break;
                    }
                }
            }
            int32_t end_frame = edit->get_object_layer_frame(obj_temp).end;
            obj_temp = edit->find_object(layer, end_frame + 1);
            if (!obj_temp) break;
        }
    }
    if (!obj) return;
    LPCWSTR currentNamePtr = edit->get_object_name(obj);
    std::wstring currentName = currentNamePtr ? currentNamePtr : L"";
    bool doRename = false;
    if (currentName.empty()) doRename = true;
    else if (currentName == p->defaultName) doRename = true;
    else if (!p->oldNameCandidate.empty() && currentName == p->oldNameCandidate) doRename = true;
    if (doRename) edit->set_object_name(obj, p->newName.c_str());
}

bool func_proc_audio_generator(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    int32_t type = gen_type.value;
    float freq = static_cast<float>(gen_freq.value);
    std::string instance_id;
    if (gen_data.value->uuid[0] != '\0') {
        instance_id = gen_data.value->uuid;
    }
    else {
        instance_id = StringUtils::GenerateUUID();
        strcpy_s(gen_data.value->uuid, sizeof(gen_data.value->uuid), instance_id.c_str());
    }
    if (!instance_id.empty()) {
        int64_t effect_id = audio->object->effect_id;
        bool is_copy = false;
        PluginManager::GetInstance().RegisterOrUpdateInstance(instance_id, effect_id, is_copy);
        if (is_copy) {
            strcpy_s(gen_data.value->uuid, sizeof(gen_data.value->uuid), instance_id.c_str());
            {
                std::lock_guard<std::mutex> lock(g_gen_param_cache_mutex);
                g_gen_param_cache.erase(instance_id);
            }
        }
    }
    else {
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(g_gen_param_cache_mutex);
        GenParamCache& cache = g_gen_param_cache[instance_id];
        if (cache.last_type != type) {
            int32_t old_type = (cache.last_type == -1) ? type : cache.last_type;
            g_main_thread_tasks.push_back([instance_id, type, old_type]() {
                if (g_edit_handle) {
                    RenameParam rp;
                    rp.id = instance_id;
                    rp.defaultName = TOOL_NAME;
                    rp.newName = std::wstring(rp.defaultName) + GetGenParamsString(type);
                    rp.oldNameCandidate = std::wstring(rp.defaultName) + GetGenParamsString(old_type);
                    g_edit_handle->call_edit_section_param(&rp, func_proc_check_and_rename);
                }
                });

            cache.last_type = type;
        }
    }
    GeneratorState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_gen_state_mutex);
        state = &g_gen_states[audio->object];
        if (!state->initialized) state->init();
        if (state->last_sample_index != -1 &&
            std::abs(state->last_sample_index + total_samples - audio->object->sample_index) > 100) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    double phase_inc = (2.0 * M_PI * freq) / Fs;
    double current_phase = state->phase;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    alignas(32) float temp_gen[BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);

        for (int32_t k = 0; k < block_count; ++k) {
            float sample = 0.0f;
            switch (type) {
            case 0:
                sample = static_cast<float>(std::sin(current_phase));
                break;
            case 1:
                sample = (current_phase < M_PI) ? 1.0f : -1.0f;
                break;
            case 2:
                sample = static_cast<float>(2.0f / M_PI * std::asin(std::sin(current_phase)));
                break;
            case 3:
                sample = static_cast<float>(1.0 - (current_phase / M_PI));
                break;
            case 4:
                sample = g_dist(g_rng);
                break;
            case 5:
                float white = g_dist(g_rng);
                state->b0 = 0.99886f * state->b0 + white * 0.0555179f;
                state->b1 = 0.99332f * state->b1 + white * 0.0750759f;
                state->b2 = 0.96900f * state->b2 + white * 0.1538520f;
                state->b3 = 0.86650f * state->b3 + white * 0.3104856f;
                state->b4 = 0.55000f * state->b4 + white * 0.5329522f;
                state->b5 = -0.7616f * state->b5 - white * 0.0168980f;
                sample = state->b0 + state->b1 + state->b2 + state->b3 + state->b4 + state->b5 + state->b6 + white * 0.5362f;
                state->b6 = white * 0.115926f;
                sample *= 0.11f;
                break;
            }
            temp_gen[k] = sample;

            if (type <= 3) {
                current_phase += phase_inc;
                if (current_phase > 2.0 * M_PI) current_phase -= 2.0 * M_PI;
            }
        }

        Avx2Utils::CopyBufferAVX2(bufL.data() + i, temp_gen, block_count);
        Avx2Utils::CopyBufferAVX2(bufR.data() + i, temp_gen, block_count);
    }

    state->phase = current_phase;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

void CleanupGeneratorResources() {
    {
        std::lock_guard<std::mutex> lock(g_gen_state_mutex);
        g_gen_states.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_gen_param_cache_mutex);
        g_gen_param_cache.clear();
    }
}

FILTER_PLUGIN_TABLE filter_plugin_table_generator = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO | FILTER_PLUGIN_TABLE::FLAG_INPUT,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_generator,
    nullptr,
    func_proc_audio_generator
};
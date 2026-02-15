#include "Eap2Common.h"
#include "SynthCommon.h"
#include "Avx2Utils.h"
#include <map>
#include <mutex>
#include <cmath>

constexpr auto TOOL_NAME = L"Generator";

FILTER_ITEM_SELECT gen_type2(L"Type", 0, gen_type_list);
FILTER_ITEM_TRACK gen_freq2(L"周波数", 440.0, 20.0, 20000.0, 1.0);
FILTER_ITEM_TRACK gen_attack(L"Attack", 0.0, 0.0, 2000.0, 1.0);
FILTER_ITEM_TRACK gen_decay(L"Decay", 0.0, 0.0, 5000.0, 1.0);
FILTER_ITEM_TRACK gen_sustain(L"Sustain", 0.0, -60.0, 0.0, 0.1);
FILTER_ITEM_TRACK gen_release(L"Release", 0.0, 0.0, 5000.0, 1.0);
FILTER_ITEM_TRACK gen_timbre(L"Timbre", 0.0, 0.0, 1.0, 0.01);
FILTER_ITEM_TRACK gen_cutoff(L"Cutoff", 1.0, 0.0, 1.0, 0.01);
FILTER_ITEM_TRACK gen_reso(L"Resonance", 0.0, 0.0, 1.0, 0.01);
FILTER_ITEM_TRACK gen_detune(L"Detune", 0.0, 0.0, 1.0, 0.01);

struct GenData {
    char uuid[40] = { 0 };
    int32_t last_type = -1;
};
FILTER_ITEM_DATA<GenData> gen_data2(L"GEN_DATA");

void* filter_items_generator2[] = {
    &gen_type2,
    &gen_freq2,
    &gen_attack,
    &gen_decay,
    &gen_sustain,
    &gen_release,
    &gen_timbre,
    &gen_cutoff,
    &gen_reso,
    &gen_detune,
    &gen_data2,
    nullptr
};

struct GeneratorObjState {
    VoiceState voice;
    bool initialized = false;
    int64_t last_sample_index = -1;
};

static std::mutex g_gen_mutex;
static std::map<const void*, GeneratorObjState> g_gen_states;

bool func_proc_audio_generator2(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);
    int64_t current_obj_sample_index = audio->object->sample_index;
    int64_t total_obj_samples = audio->object->sample_total;
    SynthParams p;
    p.type = gen_type2.value;
    p.freq = static_cast<float>(gen_freq2.value);
    p.attack = gen_attack.value / 1000.0;
    p.decay = gen_decay.value / 1000.0;
    p.sustain = static_cast<float>(std::pow(10.0, gen_sustain.value / 20.0));
    p.release = gen_release.value / 1000.0;
    p.timbre = static_cast<float>(gen_timbre.value);
    p.filter_cutoff = static_cast<float>(gen_cutoff.value);
    p.filter_res = static_cast<float>(gen_reso.value);
    p.detune = static_cast<float>(gen_detune.value);
    VoiceState* voiceState = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_gen_mutex);
        GeneratorObjState& state = g_gen_states[audio->object];
        if (!state.initialized ||
            state.last_sample_index == -1 ||
            state.last_sample_index != current_obj_sample_index) {
            state.voice.init();
            state.initialized = true;
        }
        state.last_sample_index = current_obj_sample_index + total_samples;
        voiceState = &state.voice;
    }
    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    double total_duration_sec = (double)total_obj_samples / Fs;
    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }
    alignas(32) float temp_genL[BLOCK_SIZE];
    alignas(32) float temp_genR[BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        for (int32_t k = 0; k < block_count; ++k) {
            int64_t current_pos = current_obj_sample_index + i + k;
            double t = (double)current_pos / Fs;
            StereoSample s = GenerateSampleStereo(*voiceState, p, t, Fs, total_duration_sec);
            temp_genL[k] = static_cast<float>(s.l);
            temp_genR[k] = static_cast<float>(s.r);
        }
        Avx2Utils::CopyBufferAVX2(bufL.data() + i, temp_genL, block_count);
        Avx2Utils::CopyBufferAVX2(bufR.data() + i, temp_genR, block_count);
    }
    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);
    return true;
}

void CleanupGeneratorResources2() {
    std::lock_guard<std::mutex> lock(g_gen_mutex);
    g_gen_states.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_generator2 = {
    TYPE_AUDIO_MEDIA,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_generator2,
    nullptr,
    func_proc_audio_generator2
};
#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Phaser"

FILTER_ITEM_TRACK ph_rate(L"Rate", 0.5, 0.01, 10.0, 0.01);
FILTER_ITEM_TRACK ph_depth(L"Depth", 50.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK ph_feedback(L"Feedback", 40.0, 0.0, 95.0, 0.1);
FILTER_ITEM_TRACK ph_mix(L"Mix", 50.0, 0.0, 100.0, 0.1);

void* filter_items_phaser[] = {
    &ph_rate,
    &ph_depth,
    &ph_feedback,
    &ph_mix,
    nullptr
};

class PhaserAPF {
public:
    float x_z1 = 0.0f;
    float y_z1 = 0.0f;

    inline float process(float input, float a) {
        float output = -a * input + x_z1 + a * y_z1;
        if (std::abs(output) < 1e-20f) output = 0.0f;
        x_z1 = input;
        y_z1 = output;
        return output;
    }

    void clear() {
        x_z1 = 0.0f;
        y_z1 = 0.0f;
    }
};

struct PhaserState {
    static const int32_t STAGES = 6;
    PhaserAPF filtersL[STAGES];
    PhaserAPF filtersR[STAGES];

    float last_feedbackL = 0.0f;
    float last_feedbackR = 0.0f;

    double phase = 0.0;
    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() {
        for (int32_t i = 0; i < STAGES; ++i) { filtersL[i].clear(); filtersR[i].clear(); }
        last_feedbackL = 0.0f; last_feedbackR = 0.0f;
        phase = 0.0; initialized = true;
    }
    void clear() {
        if (initialized) {
            for (int32_t i = 0; i < STAGES; ++i) { filtersL[i].clear(); filtersR[i].clear(); }
            last_feedbackL = 0.0f; last_feedbackR = 0.0f;
            phase = 0.0;
        }
    }
};

static std::mutex g_ph_state_mutex;
static std::map<const void*, PhaserState> g_ph_states;
const int32_t BLOCK_SIZE = 64;

bool func_proc_audio_phaser(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    float rate = static_cast<float>(ph_rate.value);
    float depth = static_cast<float>(ph_depth.value) / 100.0f;
    float feedback = static_cast<float>(ph_feedback.value) / 100.0f;
    float mix_val = static_cast<float>(ph_mix.value);
    float mix = mix_val / 100.0f;

    if (mix_val == 0.0f) return true;

    PhaserState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_ph_state_mutex);
        state = &g_ph_states[audio->object];
        if (!state->initialized) state->init();
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    double lfo_inc = (2.0 * M_PI * rate) / Fs;

    float min_freq = 200.0f;
    float max_freq = 2000.0f;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    double current_phase = state->phase;

    alignas(32) float temp_wet_L[BLOCK_SIZE];
    alignas(32) float temp_wet_R[BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* p_dry_L = bufL.data() + i;
        float* p_dry_R = bufR.data() + i;

        for (int32_t k = 0; k < block_count; ++k) {
            float lfo = (float)(std::sin(current_phase) + 1.0f) * 0.5f;
            current_phase += lfo_inc;
            if (current_phase > 2.0 * M_PI) current_phase -= 2.0 * M_PI;

            float freq = min_freq + (max_freq - min_freq) * lfo * depth;
            float tan_val = (float)std::tan(M_PI * freq / Fs);
            float a = (tan_val - 1.0f) / (tan_val + 1.0f);

            float in_l = p_dry_L[k] + state->last_feedbackL * feedback;
            float out_l = in_l;
            for (int32_t s = 0; s < PhaserState::STAGES; ++s) {
                out_l = state->filtersL[s].process(out_l, a);
            }
            state->last_feedbackL = out_l;
            temp_wet_L[k] = out_l;

            float in_r = p_dry_R[k] + state->last_feedbackR * feedback;
            float out_r = in_r;
            for (int32_t s = 0; s < PhaserState::STAGES; ++s) {
                out_r = state->filtersR[s].process(out_r, a);
            }
            state->last_feedbackR = out_r;
            temp_wet_R[k] = out_r;
        }

        Avx2Utils::MixAudioAVX2(p_dry_L, temp_wet_L, block_count, 1.0f - mix, mix, 1.0f);
        Avx2Utils::MixAudioAVX2(p_dry_R, temp_wet_R, block_count, 1.0f - mix, mix, 1.0f);
    }

    state->phase = current_phase;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_phaser = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_phaser,
    nullptr,
    func_proc_audio_phaser
};
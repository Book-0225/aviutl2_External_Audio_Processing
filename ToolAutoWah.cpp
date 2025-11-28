#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include <cmath>
#include <map>
#include "Avx2Utils.h"

#define TOOL_NAME L"Auto Wah"

FILTER_ITEM_TRACK wah_sens(L"Sensitivity", 50.0, 0.0, 100.0, 1.0);
FILTER_ITEM_TRACK wah_base(L"Base Freq", 200.0, 50.0, 5000.0, 1.0);
FILTER_ITEM_TRACK wah_range(L"Range", 2000.0, 100.0, 10000.0, 1.0);
FILTER_ITEM_TRACK wah_res(L"Resonance", 3.0, 0.1, 20.0, 0.1);
FILTER_ITEM_TRACK wah_mix(L"Mix", 100.0, 0.0, 100.0, 1.0);

void* filter_items_autowah[] = {
    &wah_sens,
    &wah_base,
    &wah_range,
    &wah_res,
    &wah_mix,
    nullptr
};

struct AutoWahBiquad {
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    inline float process_ret(float in, float b0, float b1, float b2, float a1, float a2) {
        float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        if (std::abs(out) < 1e-20f) out = 0.0f;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        return out;
    }
};

struct AutoWahState {
    AutoWahBiquad filterL, filterR;
    float envelope = 0.0f;
    bool initialized = false;
    int64_t last_sample_index = -1;
    float c_b0 = 0.0f, c_b1 = 0.0f, c_b2 = 0.0f, c_a1 = 0.0f, c_a2 = 0.0f;
};

static std::mutex g_wah_mutex;
static std::map<const void*, AutoWahState> g_wah_states;

const int BLOCK_SIZE = 64;
const int CONTROL_RATE = 8;

bool func_proc_audio_autowah(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;

    float sens = (float)wah_sens.value / 20.0f;
    float base_f = (float)wah_base.value;
    float range_f = (float)wah_range.value;
    float q = (float)wah_res.value;
    float mix = (float)wah_mix.value / 100.0f;
    double sr = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;

    float att_coeff = std::exp(-1.0f / (30.0f * 0.001f * (float)sr));
    float rel_coeff = std::exp(-1.0f / (150.0f * 0.001f * (float)sr));

    AutoWahState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_wah_mutex);
        state = &g_wah_states[audio->object];
        if (state->last_sample_index != -1 && state->last_sample_index + total_samples != audio->object->sample_index) {
            state->filterL = AutoWahBiquad();
            state->filterR = AutoWahBiquad();
            state->envelope = 0.0f;
        }
        state->last_sample_index = audio->object->sample_index;
        state->initialized = true;
    }

    int channels = (std::min)(2, audio->object->channel_num);
    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }
    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1); else Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    float current_env = state->envelope;
    float c_b0 = state->c_b0, c_b1 = state->c_b1, c_b2 = state->c_b2;
    float c_a1 = state->c_a1, c_a2 = state->c_a2;

    if (!state->initialized || state->c_b0 == 0.0f) {
        float target_freq = base_f + (current_env * range_f);
        if (target_freq > (float)sr * 0.45f) target_freq = (float)sr * 0.45f;
        float omega = 2.0f * (float)M_PI * target_freq / (float)sr;
        float sn = std::sin(omega);
        float cs = std::cos(omega);
        float alpha = sn / (2.0f * q);
        float norm = 1.0f + alpha;
        float norm_inv = 1.0f / norm;
        c_b0 = ((1.0f - cs) * 0.5f) * norm_inv;
        c_b1 = (1.0f - cs) * norm_inv;
        c_b2 = c_b0;
        c_a1 = (-2.0f * cs) * norm_inv;
        c_a2 = (1.0f - alpha) * norm_inv;
    }

    alignas(32) float temp_wet_L[BLOCK_SIZE];
    alignas(32) float temp_wet_R[BLOCK_SIZE];

    for (int i = 0; i < total_samples; i += BLOCK_SIZE) {
        int block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* pL = bufL.data() + i;
        float* pR = bufR.data() + i;

        for (int k = 0; k < block_count; ++k) {
            float inL = pL[k];
            float inR = pR[k];
            float input_level = (std::abs(inL) + std::abs(inR)) * 0.5f;
            float input_drive = input_level * sens;

            if (input_drive > current_env)
                current_env = att_coeff * (current_env - input_drive) + input_drive;
            else
                current_env = rel_coeff * (current_env - input_drive) + input_drive;

            if ((i + k) % CONTROL_RATE == 0) {
                float target_freq = base_f + (current_env * range_f);
                if (target_freq > (float)sr * 0.45f) target_freq = (float)sr * 0.45f;

                float omega = 2.0f * (float)M_PI * target_freq / (float)sr;
                float sn = std::sin(omega);
                float cs = std::cos(omega);
                float alpha = sn / (2.0f * q);
                float norm = 1.0f + alpha;
                float norm_inv = 1.0f / norm;

                c_b0 = ((1.0f - cs) * 0.5f) * norm_inv;
                c_b1 = (1.0f - cs) * norm_inv;
                c_b2 = c_b0;
                c_a1 = (-2.0f * cs) * norm_inv;
                c_a2 = (1.0f - alpha) * norm_inv;
            }

            temp_wet_L[k] = state->filterL.process_ret(inL, c_b0, c_b1, c_b2, c_a1, c_a2);
            temp_wet_R[k] = state->filterR.process_ret(inR, c_b0, c_b1, c_b2, c_a1, c_a2);
        }

        Avx2Utils::MixAudioAVX2(pL, temp_wet_L, block_count, 1.0f - mix, mix, 1.0f);
        Avx2Utils::MixAudioAVX2(pR, temp_wet_R, block_count, 1.0f - mix, mix, 1.0f);
    }

    state->envelope = current_env;
    state->c_b0 = c_b0; state->c_b1 = c_b1; state->c_b2 = c_b2;
    state->c_a1 = c_a1; state->c_a2 = c_a2;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_autowah = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_autowah,
    nullptr,
    func_proc_audio_autowah
};
#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include <cmath>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Spectral Gate"

FILTER_ITEM_TRACK spec_gate_threshold(L"Threshold (dB)", -40.0, -80.0, 0.0, 0.1);
FILTER_ITEM_TRACK spec_gate_attack(L"Attack (ms)", 10.0, 1.0, 500.0, 1.0);
FILTER_ITEM_TRACK spec_gate_release(L"Release (ms)", 100.0, 1.0, 2000.0, 1.0);
FILTER_ITEM_TRACK spec_gate_mix(L"Mix", 100.0, 0.0, 100.0, 0.1);

void* filter_items_spectral_gate[] = {
    &spec_gate_threshold,
    &spec_gate_attack,
    &spec_gate_release,
    &spec_gate_mix,
    nullptr
};

struct HighpassFilter {
    float x1 = 0.0f, y1 = 0.0f;
    float a1 = 0.0f, b0 = 0.0f, b1 = 0.0f;

    void design(float cutoff_freq, double sample_rate) {
        float omega = 2.0f * (float)M_PI * cutoff_freq / (float)sample_rate;
        float sin_omega = std::sin(omega);
        float cos_omega = std::cos(omega);
        float alpha = sin_omega / (2.0f * 0.707107f);
        float denom = 1.0f + alpha;
        b0 = (1.0f + cos_omega) / (2.0f * denom);
        b1 = -(1.0f + cos_omega) / denom;
        a1 = (-2.0f * cos_omega) / denom;
    }

    float process(float input) {
        float output = b0 * input + b1 * x1 - a1 * y1;
        if (std::abs(output) < 1e-20f) output = 0.0f;
        x1 = input;
        y1 = output;
        return output;
    }
};

struct SpectralGateState {
    HighpassFilter hpL, hpR;
    float envelope = 0.0f;
    bool initialized = false;
    int64_t last_sample_index = -1;
};

static std::mutex g_spectral_gate_mutex;
static std::map<const void*, SpectralGateState> g_spectral_gate_states;

const int32_t BLOCK_SIZE = 64;

bool func_proc_audio_spectral_gate(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;

    float threshold_db = (float)spec_gate_threshold.value;
    float attack_ms = (float)spec_gate_attack.value;
    float release_ms = (float)spec_gate_release.value;
    float mix = (float)spec_gate_mix.value / 100.0f;
    
    double sr = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;

    SpectralGateState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_spectral_gate_mutex);
        state = &g_spectral_gate_states[audio->object];
        
        if (!state->initialized || state->last_sample_index != audio->object->sample_index) {
            state->hpL.design(100.0f, sr);
            state->hpR.design(100.0f, sr);
            state->initialized = true;
        }
        state->last_sample_index = audio->object->sample_index + total_samples;
    }

    float threshold_linear = std::pow(10.0f, threshold_db / 20.0f);
    float attack_coeff = std::exp(-1.0f / (attack_ms * (float)sr / 1000.0f + 1.0f));
    float release_coeff = std::exp(-1.0f / (release_ms * (float)sr / 1000.0f + 1.0f));

    int32_t channels = (std::min)(2, audio->object->channel_num);
    thread_local std::vector<float> bufL, bufR;
    thread_local std::vector<float> envL_buf, envR_buf, max_env_buf, gate_envelope;
    
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
        envL_buf.resize(total_samples);
        envR_buf.resize(total_samples);
        max_env_buf.resize(total_samples);
        gate_envelope.resize(total_samples);
    }
    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) std::fill(bufR.begin(), bufR.end(), 0.0f);

    {
        std::lock_guard<std::mutex> lock(g_spectral_gate_mutex);
        for (int32_t i = 0; i < total_samples; ++i) {
            envL_buf[i] = std::abs(state->hpL.process(bufL[i]));
            if (channels >= 2) {
                envR_buf[i] = std::abs(state->hpR.process(bufR[i]));
            }
        }
    }

    if (channels >= 2) {
        Avx2Utils::MaxBufferAVX2(max_env_buf.data(), envL_buf.data(), envR_buf.data(), total_samples);
    } else {
        Avx2Utils::CopyBufferAVX2(max_env_buf.data(), envL_buf.data(), total_samples);
    }

    Avx2Utils::FillBufferAVX2(gate_envelope.data(), total_samples, state->envelope);
    Avx2Utils::EnvelopeFollowerAVX2(gate_envelope.data(), max_env_buf.data(), total_samples, attack_coeff, release_coeff);
    
    state->envelope = gate_envelope[total_samples - 1];

    Avx2Utils::ThresholdAVX2(max_env_buf.data(), gate_envelope.data(), total_samples, threshold_linear);

    std::vector<float> temp_wet_L(bufL.begin(), bufL.end());
    std::vector<float> temp_wet_R(bufR.begin(), bufR.end());
    
    Avx2Utils::MultiplyBufferAVX2(temp_wet_L.data(), temp_wet_L.data(), max_env_buf.data(), total_samples);
    if (channels >= 2) {
        Avx2Utils::MultiplyBufferAVX2(temp_wet_R.data(), temp_wet_R.data(), max_env_buf.data(), total_samples);
    }

    if (channels >= 1) {
        Avx2Utils::MixAudioAVX2(bufL.data(), temp_wet_L.data(), total_samples, mix, 1.0f - mix, 1.0f);
    }
    if (channels >= 2) {
        Avx2Utils::MixAudioAVX2(bufR.data(), temp_wet_R.data(), total_samples, mix, 1.0f - mix, 1.0f);
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_spectral_gate = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_spectral_gate,
    nullptr,
    func_proc_audio_spectral_gate
};

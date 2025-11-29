#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Dynamics"

FILTER_ITEM_TRACK dyn_gate_thresh(L"Gate Threshold", -60.0, -90.0, 0.0, 0.1);
FILTER_ITEM_TRACK dyn_gate_rel(L"Gate Release", 200.0, 10.0, 2000.0, 10.0);
FILTER_ITEM_TRACK dyn_comp_thresh(L"Comp Threshold", 0.0, -60.0, 0.0, 0.1);
FILTER_ITEM_TRACK dyn_comp_ratio(L"Comp Ratio", 1.0, 1.0, 20.0, 0.1);
FILTER_ITEM_TRACK dyn_comp_attack(L"Comp Attack", 5.0, 0.1, 100.0, 0.1);
FILTER_ITEM_TRACK dyn_comp_release(L"Comp Release", 50.0, 10.0, 1000.0, 1.0);
FILTER_ITEM_TRACK dyn_comp_makeup(L"Makeup Gain", 0.0, 0.0, 30.0, 0.1);
FILTER_ITEM_TRACK dyn_limiter(L"Limiter", 0.0, -20.0, 0.0, 0.1);

void* filter_items_dynamics[] = {
    &dyn_gate_thresh,
    &dyn_gate_rel,
    &dyn_comp_thresh,
    &dyn_comp_ratio,
    &dyn_comp_attack,
    &dyn_comp_release,
    &dyn_comp_makeup,
    &dyn_limiter,
    nullptr
};

struct DynamicsState {
    double gate_gain = 1.0;
    double comp_envelope = 0.0;
    int64_t last_sample_index = -1;
};

static std::mutex g_dyn_state_mutex;
static std::map<const void*, DynamicsState> g_dyn_states;
const int32_t BLOCK_SIZE = 64;

bool func_proc_audio_dynamics(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    double gate_th_db = dyn_gate_thresh.value;
    double gate_rel_ms = dyn_gate_rel.value;
    double comp_th_db = dyn_comp_thresh.value;
    double comp_ratio = dyn_comp_ratio.value;
    double comp_att_ms = dyn_comp_attack.value;
    double comp_rel_ms = dyn_comp_release.value;
    double comp_makeup_db = dyn_comp_makeup.value;
    double lim_db = dyn_limiter.value;

    if (gate_th_db <= -90.0 && comp_ratio == 1.0 && comp_makeup_db == 0.0 && lim_db >= 0.0) {
        return true;
    }

    DynamicsState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dyn_state_mutex);
        state = &g_dyn_states[audio->object];
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->gate_gain = 1.0; state->comp_envelope = 0.0;
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;

    double gate_th_lin = std::pow(10.0, gate_th_db / 20.0);
    double gate_rel_coef = std::exp(-1.0 / (gate_rel_ms * 0.001 * Fs));
    double gate_atk_coef = std::exp(-1.0 / (10.0 * 0.001 * Fs));

    double comp_att_coef = std::exp(-1.0 / (comp_att_ms * 0.001 * Fs));
    double comp_rel_coef = std::exp(-1.0 / (comp_rel_ms * 0.001 * Fs));

    double makeup_lin = std::pow(10.0, comp_makeup_db / 20.0);
    bool use_comp = (comp_ratio > 1.0 || comp_makeup_db > 0.0);

    float lim_lin = static_cast<float>(std::pow(10.0, lim_db / 20.0));

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    double current_gate_gain = state->gate_gain;
    double current_comp_env = state->comp_envelope;

    alignas(32) float temp_gain[BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* pL = bufL.data() + i;
        float* pR = bufR.data() + i;

        for (int32_t k = 0; k < block_count; ++k) {
            float l = pL[k];
            float r = pR[k];
            double in_abs = std::abs(l);
            if (channels >= 2) in_abs = (std::max)(in_abs, (double)std::abs(r));

            double gate_target = (in_abs > gate_th_lin) ? 1.0 : 0.0;
            if (gate_target > current_gate_gain)
                current_gate_gain += gate_atk_coef * (gate_target - current_gate_gain);
            else
                current_gate_gain += gate_rel_coef * (gate_target - current_gate_gain);

            double gated_abs = in_abs * current_gate_gain;

            double comp_gain = 1.0;
            if (use_comp) {
                if (gated_abs > current_comp_env)
                    current_comp_env += comp_att_coef * (gated_abs - current_comp_env);
                else
                    current_comp_env += comp_rel_coef * (gated_abs - current_comp_env);

                double env_db = (current_comp_env > 1.0e-6) ? 20.0 * std::log10(current_comp_env) : -120.0;
                double gain_reduction_db = 0.0;

                if (env_db > comp_th_db) {
                    gain_reduction_db = (env_db - comp_th_db) * (1.0 / comp_ratio - 1.0);
                }
                comp_gain = std::pow(10.0, gain_reduction_db / 20.0);
                comp_gain *= makeup_lin;
            }

            temp_gain[k] = static_cast<float>(current_gate_gain * comp_gain);
        }

        Avx2Utils::MultiplyBufferAVX2(pL, temp_gain, block_count);
        Avx2Utils::MultiplyBufferAVX2(pR, temp_gain, block_count);

        if (lim_db < 0.0) {
            Avx2Utils::HardClipAVX2(pL, block_count, -lim_lin, lim_lin);
            Avx2Utils::HardClipAVX2(pR, block_count, -lim_lin, lim_lin);
        }
    }

    state->gate_gain = current_gate_gain;
    state->comp_envelope = current_comp_env;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_dynamics = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_dynamics,
    nullptr,
    func_proc_audio_dynamics
};
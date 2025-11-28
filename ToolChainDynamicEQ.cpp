#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include "ChainManager.h"
#include <cmath>
#include <map>
#include "Avx2Utils.h"

#define TOOL_NAME L"Chain Dynamic EQ"

FILTER_ITEM_TRACK deq_id(L"ID", 1.0, 1.0, ChainManager::MAX_CHAINS, 1.0);
FILTER_ITEM_TRACK deq_freq(L"Frequency", 1000.0, 20.0, 20000.0, 1.0);
FILTER_ITEM_TRACK deq_q(L"Q", 1.0, 0.1, 20.0, 0.1);
FILTER_ITEM_TRACK deq_gain(L"Reduction", 6.0, 0.0, 24.0, 0.1);
FILTER_ITEM_TRACK deq_thresh(L"Threshold", -20.0, -60.0, 0.0, 0.1);
FILTER_ITEM_TRACK deq_att(L"Attack", 10.0, 0.0, 500.0, 1.0);
FILTER_ITEM_TRACK deq_rel(L"Release", 100.0, 0.0, 2000.0, 1.0);

void* filter_items_chain_dyn_eq[] = {
    &deq_id, &deq_freq, &deq_q, &deq_gain, &deq_thresh, &deq_att, &deq_rel, nullptr
};

struct DynEqBiquad {
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    inline void process(float& sample, float b0, float b1, float b2, float a1, float a2) {
        float in = sample;
        float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        if (std::abs(out) < 1e-20f) out = 0.0f;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        sample = out;
    }
};

struct DynEqState {
    DynEqBiquad filterL, filterR;
    double envelope = 0.0;
    int64_t last_sample_index = -1;
    uint32_t last_update_count = 0;
    int missed_count = 0;
    float c_b0 = 1.0f, c_b1 = 0.0f, c_b2 = 0.0f, c_a1 = 0.0f, c_a2 = 0.0f;
};

static std::mutex g_dyneq_mutex;
static std::map<const void*, DynEqState> g_dyneq_states;

const int BLOCK_SIZE = 64;
const int CONTROL_RATE = 8;

bool func_proc_audio_chain_dyn_eq(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;

    int bus_idx = static_cast<int>(deq_id.value) - 1;
    if (bus_idx < 0 || bus_idx >= ChainManager::MAX_CHAINS) return true;

    double freq = deq_freq.value;
    double q = deq_q.value;
    double max_reduction_db = -deq_gain.value;
    double thresh_db = deq_thresh.value;
    double att_ms = deq_att.value;
    double rel_ms = deq_rel.value;

    DynEqState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dyneq_mutex);
        state = &g_dyneq_states[audio->object];
        if (state->last_sample_index != -1 && state->last_sample_index + total_samples != audio->object->sample_index) {
            state->filterL = DynEqBiquad(); state->filterR = DynEqBiquad();
            state->envelope = 0.0;
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    double att_coef = std::exp(-1.0 / (att_ms * 0.001 * Fs));
    double rel_coef = std::exp(-1.0 / (rel_ms * 0.001 * Fs));
    double omega = 2.0 * M_PI * freq / Fs;
    double sn = std::sin(omega);
    double cs = std::cos(omega);
    double alpha = sn / (2.0 * q);

    double sidechain_input = 0.0;
    {
        std::lock_guard<std::mutex> lock(ChainManager::chains_mutex);
        if (ChainManager::chains[bus_idx].update_count != state->last_update_count) {
            sidechain_input = (double)ChainManager::chains[bus_idx].level;
            state->last_update_count = ChainManager::chains[bus_idx].update_count;
            state->missed_count = 0;
        }
        else {
            if (state->missed_count <= 2) state->missed_count++;
            if (state->missed_count <= 1) sidechain_input = (double)ChainManager::chains[bus_idx].level;
            else sidechain_input = 0.0;
        }
    }

    int channels = (std::min)(2, audio->object->channel_num);
    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }
    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1); else Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    double current_env = state->envelope;
    double trigger_abs = sidechain_input;

    float c_b0 = state->c_b0, c_b1 = state->c_b1, c_b2 = state->c_b2;
    float c_a1 = state->c_a1, c_a2 = state->c_a2;

    for (int i = 0; i < total_samples; i += BLOCK_SIZE) {
        int block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* pL = bufL.data() + i;
        float* pR = bufR.data() + i;

        for (int k = 0; k < block_count; ++k) {
            if (trigger_abs > current_env)
                current_env += att_coef * (trigger_abs - current_env);
            else
                current_env += rel_coef * (trigger_abs - current_env);

            if ((i + k) % CONTROL_RATE == 0) {
                double env_db = (current_env > 1.0e-6) ? 20.0 * std::log10(current_env) : -120.0;
                double current_gain_db = 0.0;

                if (env_db > thresh_db) {
                    double over_db = env_db - thresh_db;
                    double ratio = (std::min)(1.0, over_db / 20.0);
                    current_gain_db = max_reduction_db * ratio;
                }

                double A = std::pow(10.0, current_gain_db * 0.025);
                double norm = 1.0 + alpha / A;
                double norm_inv = 1.0 / norm;

                c_b0 = (float)((1.0 + alpha * A) * norm_inv);
                c_b1 = (float)((-2.0 * cs) * norm_inv);
                c_b2 = (float)((1.0 - alpha * A) * norm_inv);
                c_a1 = (float)((-2.0 * cs) * norm_inv);
                c_a2 = (float)((1.0 - alpha / A) * norm_inv);
            }

            state->filterL.process(pL[k], c_b0, c_b1, c_b2, c_a1, c_a2);
            state->filterR.process(pR[k], c_b0, c_b1, c_b2, c_a1, c_a2);
        }
    }

    state->envelope = current_env;
    state->c_b0 = c_b0; state->c_b1 = c_b1; state->c_b2 = c_b2;
    state->c_a1 = c_a1; state->c_a2 = c_a2;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_chain_dyn_eq = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_chain_dyn_eq,
    nullptr,
    func_proc_audio_chain_dyn_eq
};
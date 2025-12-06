#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include "ChainManager.h"
#include <cmath>
#include <map>
#include "Avx2Utils.h"

#define TOOL_NAME L"Chain Filter"

FILTER_ITEM_TRACK cfilt_id(L"ID", 1.0, 1.0, ChainManager::MAX_ID, 1.0);
FILTER_ITEM_TRACK cfilt_base(L"Base Freq", 400.0, 20.0, 20000.0, 1.0);
FILTER_ITEM_TRACK cfilt_mod(L"Mod Depth", 2000.0, -10000.0, 10000.0, 1.0);
FILTER_ITEM_TRACK cfilt_q(L"Resonance", 2.0, 0.1, 20.0, 0.1);
FILTER_ITEM_TRACK cfilt_att(L"Attack", 10.0, 0.0, 500.0, 1.0);
FILTER_ITEM_TRACK cfilt_rel(L"Release", 100.0, 0.0, 2000.0, 1.0);

void* filter_items_chain_filter[] = {
    &cfilt_id,
    &cfilt_base,
    &cfilt_mod,
    &cfilt_q,
    &cfilt_att,
    &cfilt_rel,
    nullptr
};

struct ChainFilterBiquad {
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

struct ChainFilterState {
    ChainFilterBiquad filterL, filterR;
    double envelope = 0.0;
    int64_t last_sample_index = -1;
    std::array<uint32_t, ChainManager::MAX_PER_ID> last_update_count = { 0 };
    std::array<int32_t, ChainManager::MAX_PER_ID> missed_count = { 0 };
    float c_b0 = 1.0f, c_b1 = 0.0f, c_b2 = 0.0f, c_a1 = 0.0f, c_a2 = 0.0f;
};

static std::mutex g_cfilt_mutex;
static std::map<const void*, ChainFilterState> g_cfilt_states;

const int32_t BLOCK_SIZE = 64;
const int32_t CONTROL_RATE = 8;

bool func_proc_audio_chain_filter(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;

    int32_t id_idx = static_cast<int32_t>(cfilt_id.value) - 1;
    if (id_idx < 0 || id_idx >= ChainManager::MAX_ID) return true;

    double base_freq = cfilt_base.value;
    double mod_depth = cfilt_mod.value;
    double q = cfilt_q.value;
    double att_ms = cfilt_att.value;
    double rel_ms = cfilt_rel.value;

    ChainFilterState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_cfilt_mutex);
        state = &g_cfilt_states[audio->object];
        if (state->last_sample_index != -1 && state->last_sample_index + total_samples != audio->object->sample_index) {
            state->filterL = ChainFilterBiquad();
            state->filterR = ChainFilterBiquad();
            state->envelope = 0.0;
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    double att_coef = 1.0 - std::exp(-1.0 / ((std::max)(0.1, att_ms) * 0.001 * Fs));
    double rel_coef = 1.0 - std::exp(-1.0 / ((std::max)(0.1, rel_ms) * 0.001 * Fs));

    double sidechain_input = 0.0;
    {
        std::lock_guard<std::mutex> lock(ChainManager::chains_mutexes[id_idx]);
        std::array<double, ChainManager::MAX_PER_ID> levels;
        auto& chain = ChainManager::chains[id_idx];
        for (int32_t i = 0; i < ChainManager::MAX_PER_ID; i++) {
            if (chain.effect_id[i] != -1) {
                if (chain.update_count[i] != state->last_update_count[i]) {
                    levels[i] = (double)chain.level[i];
                    state->last_update_count[i] = chain.update_count[i];
                    state->missed_count[i] = 0;
                }
                else {
                    if (state->missed_count[i] < INT32_MAX) state->missed_count[i]++;
                    if (state->missed_count[i] >= 10) chain.effect_id[i] = -1;
                    if (state->missed_count[i] <= 1) levels[i] = (double)chain.level[i];
                    else levels[i] = 0.0;
                }
            }
            else {
                levels[i] = 0.0;
                state->missed_count[i] = 0;
            }
        }
        sidechain_input = *std::max_element(levels.begin(), levels.end());
    }

    int32_t channels = (std::min)(2, audio->object->channel_num);
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

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* pL = bufL.data() + i;
        float* pR = bufR.data() + i;

        for (int32_t k = 0; k < block_count; ++k) {
            if (trigger_abs > current_env)
                current_env += att_coef * (trigger_abs - current_env);
            else
                current_env += rel_coef * (trigger_abs - current_env);

            if ((i + k) % CONTROL_RATE == 0) {
                double target_freq = base_freq + (current_env * mod_depth);
                if (target_freq < 20.0) target_freq = 20.0;
                if (target_freq > Fs * 0.45) target_freq = Fs * 0.45;

                double omega = 2.0 * M_PI * target_freq / Fs;
                double sn = std::sin(omega);
                double cs = std::cos(omega);
                double alpha = sn / (2.0 * q);
                double norm = 1.0 + alpha;
                double norm_inv = 1.0 / norm;

                c_b0 = (float)(((1.0 - cs) * 0.5) * norm_inv);
                c_b1 = (float)((1.0 - cs) * norm_inv);
                c_b2 = c_b0;
                c_a1 = (float)((-2.0 * cs) * norm_inv);
                c_a2 = (float)((1.0 - alpha) * norm_inv);
            }

            state->filterL.process(pL[k], c_b0, c_b1, c_b2, c_a1, c_a2);
            state->filterR.process(pR[k], c_b0, c_b1, c_b2, c_a1, c_a2);
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_cfilt_mutex);
        state->envelope = current_env;
        state->c_b0 = c_b0; state->c_b1 = c_b1; state->c_b2 = c_b2;
        state->c_a1 = c_a1; state->c_a2 = c_a2;
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

void CleanupChainFilterResources() {
    g_cfilt_states.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_chain_filter = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_chain_filter,
    nullptr,
    func_proc_audio_chain_filter
};
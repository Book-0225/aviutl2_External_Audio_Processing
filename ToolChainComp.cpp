#include "Eap2Common.h"
#include "ChainManager.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Chain Comp"

FILTER_ITEM_TRACK chain_comp_id(L"ID", 1.0, 1.0, ChainManager::MAX_ID, 1.0);
FILTER_ITEM_TRACK chain_comp_thresh(L"Threshold", -20.0, -60.0, 0.0, 0.1);
FILTER_ITEM_TRACK chain_comp_ratio(L"Ratio", 4.0, 1.0, 20.0, 0.1);
FILTER_ITEM_TRACK chain_comp_attack(L"Attack", 5.0, 0.1, 100.0, 0.1);
FILTER_ITEM_TRACK chain_comp_release(L"Release", 50.0, 10.0, 1000.0, 1.0);
FILTER_ITEM_TRACK chain_comp_makeup(L"Makeup", 0.0, 0.0, 20.0, 0.1);

void* filter_items_chain_comp[] = {
    &chain_comp_id,
    &chain_comp_thresh,
    &chain_comp_ratio,
    &chain_comp_attack,
    &chain_comp_release,
    &chain_comp_makeup,
    nullptr
};

struct ChainCompState {
    double comp_envelope = 0.0;
    int64_t last_sample_index = -1;
    std::array<uint32_t, ChainManager::MAX_PER_ID> last_update_count = { 0 };
    std::array<int32_t, ChainManager::MAX_PER_ID> missed_count = { 0 };
};

static std::mutex g_chain_state_mutex;
static std::map<const void*, ChainCompState> g_chain_states;
const int32_t BLOCK_SIZE = 64;

bool func_proc_audio_chain_comp(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    int32_t id_idx = static_cast<int32_t>(chain_comp_id.value) - 1;
    double comp_th_db = chain_comp_thresh.value;
    double comp_ratio = chain_comp_ratio.value;
    double comp_att_ms = chain_comp_attack.value;
    double comp_rel_ms = chain_comp_release.value;
    double comp_makeup_db = chain_comp_makeup.value;

    if (id_idx < 0 || id_idx >= ChainManager::MAX_ID) return true;
    if (comp_ratio == 1.0 && comp_makeup_db == 0.0) return true;

    ChainCompState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_chain_state_mutex);
        state = &g_chain_states[audio->object];
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->comp_envelope = 0.0;
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    double comp_att_coef = 1.0 - std::exp(-1.0 / ((std::max)(0.1, comp_att_ms) * 0.001 * Fs));
    double comp_rel_coef = 1.0 - std::exp(-1.0 / ((std::max)(0.1, comp_rel_ms) * 0.001 * Fs));
    double makeup_lin = std::pow(10.0, comp_makeup_db / 20.0);

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

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

    double current_comp_env = state->comp_envelope;
    double trigger_abs = sidechain_input;

    alignas(32) float temp_gain[BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* pL = bufL.data() + i;
        float* pR = bufR.data() + i;

        for (int32_t k = 0; k < block_count; ++k) {
            if (trigger_abs > current_comp_env)
                current_comp_env += comp_att_coef * (trigger_abs - current_comp_env);
            else
                current_comp_env += comp_rel_coef * (trigger_abs - current_comp_env);

            double env_db = (current_comp_env > 1.0e-6) ? 20.0 * std::log10(current_comp_env) : -120.0;
            double gain_reduction_db = 0.0;
            if (env_db > comp_th_db) {
                gain_reduction_db = (env_db - comp_th_db) * (1.0 / comp_ratio - 1.0);
            }
            double comp_gain = std::pow(10.0, gain_reduction_db / 20.0);
            comp_gain *= makeup_lin;

            temp_gain[k] = static_cast<float>(comp_gain);
        }

        Avx2Utils::MultiplyBufferAVX2(pL, temp_gain, block_count);
        Avx2Utils::MultiplyBufferAVX2(pR, temp_gain, block_count);
    }

    {
        std::lock_guard<std::mutex> lock(g_chain_state_mutex);
        state->comp_envelope = current_comp_env;
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

void CleanupChainCompResources() {
    g_chain_states.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_chain_comp = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_chain_comp,
    nullptr,
    func_proc_audio_chain_comp
};
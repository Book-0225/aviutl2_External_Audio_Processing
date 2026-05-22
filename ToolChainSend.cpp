#include "Avx2Utils.h"
#include "ChainManager.h"
#include "Eap2Common.h"

#include <algorithm>
#include <vector>

constexpr auto TOOL_NAME = L"Chain Send";

FILTER_ITEM_TRACK send_id(L"ID", 1.0, 1.0, ChainManager::MAX_ID, 1.0);
FILTER_ITEM_TRACK send_gain(L"Send Gain", 100.0, 0.0, 200.0, 0.1);
FILTER_ITEM_TRACK send_offset(L"Offset", 0.0, 0.0, 600.0, 0.001);
FILTER_ITEM_TRACK send_duration(L"Duration", 0.0, 0.0, 600.0, 0.001);

void* filter_items_chain_send[] = {
    &send_id,
    &send_gain,
    &send_offset,
    &send_duration,
    nullptr
};

bool func_proc_audio_chain_send(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    int32_t id_idx = static_cast<int32_t>(send_id.value) - 1;
    if (id_idx < 0 || id_idx >= ChainManager::MAX_ID) return true;

    float gain_val = static_cast<float>(send_gain.value / 100.0);
    const double inv_sample_rate = 1.0 / audio->scene->sample_rate;
    const int64_t offset_samples =
        static_cast<int64_t>(send_offset.value * audio->scene->sample_rate);
    const int64_t chunk_start = audio->object->sample_index;
    const int64_t chunk_end = chunk_start + total_samples;
    const int64_t valid_begin = offset_samples;
    const int64_t valid_end = (send_duration.value > 0.0)
                                  ? offset_samples + static_cast<int64_t>(send_duration.value * audio->scene->sample_rate)
                                  : INT64_MAX;
    const int32_t skip = static_cast<int32_t>(
        std::clamp(valid_begin - chunk_start, INT64_C(0), static_cast<int64_t>(total_samples)));
    const int32_t end = static_cast<int32_t>(
        std::clamp(valid_end - chunk_start, INT64_C(0), static_cast<int64_t>(total_samples)));
    const int32_t valid_samples = end - skip;
    if (valid_samples <= 0) return true;
    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    float max_peak = Avx2Utils::GetPeakAbsAVX2(bufL.data() + skip, valid_samples);

    if (channels >= 2) {
        float peak_r = Avx2Utils::GetPeakAbsAVX2(bufR.data() + skip, valid_samples);
        if (peak_r > max_peak) max_peak = peak_r;
    }

    max_peak *= gain_val;

    {
        std::lock_guard<std::mutex> lock(ChainManager::chains_mutexes[id_idx]);
        auto& chain = ChainManager::chains[id_idx];
        const auto& ids = chain.effect_id;
        auto it = std::find(ids.begin(), ids.end(), audio->object->effect_id);
        if (it != ids.end()) {
            auto data_idx = std::distance(ids.begin(), it);
            chain.level[data_idx] = max_peak;
            chain.update_count[data_idx]++;
        } else {
            auto free_it = std::find(ids.begin(), ids.end(), -1);
            if (free_it != ids.end()) {
                auto free_idx = std::distance(ids.begin(), free_it);
                chain.effect_id[free_idx] = audio->object->effect_id;
                chain.level[free_idx] = max_peak;
                chain.update_count[free_idx] = 1;
            }
        }
    }

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_chain_send = {
    TYPE_AUDIO_FILTER_OBJECT,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_chain_send,
    nullptr,
    func_proc_audio_chain_send
};
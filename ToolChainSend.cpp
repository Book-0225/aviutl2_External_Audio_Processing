#include "Eap2Common.h"
#include "ChainManager.h"
#include <vector>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Chain Send"

FILTER_ITEM_TRACK send_id(L"ID", 1.0, 1.0, ChainManager::MAX_CHAINS, 1.0);
FILTER_ITEM_TRACK send_gain(L"Send Gain", 100.0, 0.0, 200.0, 0.1);

void* filter_items_send[] = {
    &send_id,
    &send_gain,
    nullptr
};

bool func_proc_audio_chain_send(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    int bus_idx = static_cast<int>(send_id.value) - 1;
    if (bus_idx < 0 || bus_idx >= ChainManager::MAX_CHAINS) return true;

    double gain_val = send_gain.value / 100.0f;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);

    float max_peak = Avx2Utils::GetPeakAbsAVX2(bufL.data(), total_samples);

    if (channels >= 2) {
        float peak_r = Avx2Utils::GetPeakAbsAVX2(bufR.data(), total_samples);
        if (peak_r > max_peak) max_peak = peak_r;
    }

    max_peak *= (float)gain_val;

    {
        std::lock_guard<std::mutex> lock(ChainManager::chains_mutex);
        ChainManager::chains[bus_idx].level = max_peak;
        ChainManager::chains[bus_idx].update_count++;
    }

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_chain_send = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_send,
    nullptr,
    func_proc_audio_chain_send
};
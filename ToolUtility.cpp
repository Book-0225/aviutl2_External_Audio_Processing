#include "Eap2Common.h"
#include <vector>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Utility"

FILTER_ITEM_TRACK tool_gain(L"Gain", 100.0, 0.0, 500.0, 0.1);
FILTER_ITEM_TRACK tool_pan(L"Pan(L-R)", 0.0, -100.0, 100.0, 0.1);
FILTER_ITEM_CHECK tool_swap(L"Swap(L/R)", false);
FILTER_ITEM_CHECK tool_inv_l(L"Invert L", false);
FILTER_ITEM_CHECK tool_inv_r(L"Invert R", false);

FILTER_ITEM_SELECT::ITEM mono_mode_list[] = {
    { L"Stereo (Off)", 0 },
    { L"Mix (L+R)", 1 },
    { L"Left to Stereo", 2 },
    { L"Right to Stereo", 3 },
    { nullptr, 0 }
};

FILTER_ITEM_SELECT tool_mono_mode(L"Mono Mode", 0, mono_mode_list);

void* filter_items_utility[] = {
    &tool_gain,
    &tool_pan,
    &tool_swap,
    &tool_inv_l,
    &tool_inv_r,
    &tool_mono_mode,
    nullptr
};

bool func_proc_audio_utility(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    float gain_val = static_cast<float>(tool_gain.value);
    float pan_val = static_cast<float>(tool_pan.value);
    bool do_swap = tool_swap.value;
    bool do_inv_l = tool_inv_l.value;
    bool do_inv_r = tool_inv_r.value;

    int32_t mono_mode = tool_mono_mode.value;

    if (gain_val == 100.0f && pan_val == 0.0f && !do_swap && !do_inv_l && !do_inv_r && mono_mode == 0) {
        return true;
    }

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    if (do_swap && channels >= 2) {
        Avx2Utils::SwapChannelsAVX2(bufL.data(), bufR.data(), total_samples);
    }

    if (do_inv_l) Avx2Utils::InvertBufferAVX2(bufL.data(), total_samples);
    if (do_inv_r && channels >= 2) Avx2Utils::InvertBufferAVX2(bufR.data(), total_samples);

    if (channels >= 2) {
        switch (mono_mode) {
        case 1:
            Avx2Utils::MatrixMixStereoAVX2(
                bufL.data(), bufR.data(),
                bufL.data(), bufR.data(),
                total_samples,
                0.5f, 0.5f, 0.5f, 0.5f
            );
            break;
        case 2:
            Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);
            break;
        case 3:
            Avx2Utils::CopyBufferAVX2(bufL.data(), bufR.data(), total_samples);
            break;
        default:
            break;
        }
    }

    float gain_ratio = gain_val / 100.0f;
    float pan_l = (pan_val > 0.0f) ? (1.0f - pan_val / 100.0f) : 1.0f;
    float pan_r = (pan_val < 0.0f) ? (1.0f + pan_val / 100.0f) : 1.0f;

    float final_scale_l = pan_l * gain_ratio;
    float final_scale_r = pan_r * gain_ratio;

    if (final_scale_l != 1.0f) Avx2Utils::ScaleBufferAVX2(bufL.data(), bufL.data(), total_samples, final_scale_l);
    if (channels >= 2 && final_scale_r != 1.0f) Avx2Utils::ScaleBufferAVX2(bufR.data(), bufR.data(), total_samples, final_scale_r);

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_utility = {
    TYPE_AUDIO_FILTER_OBJECT,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_utility,
    nullptr,
    func_proc_audio_utility
};
#include "Eap2Common.h"
#include "Avx2Utils.h"
#include <vector>
#include <algorithm>

#define TOOL_NAME L"Stereo"

FILTER_ITEM_TRACK tool_width(L"Width", 100.0, 0.0, 200.0, 0.1);
FILTER_ITEM_TRACK tool_mid(L"Mid Level", 100.0, 0.0, 200.0, 0.1);
FILTER_ITEM_TRACK tool_side(L"Side Level", 100.0, 0.0, 200.0, 0.1);

void* filter_items_stereo[] = {
    &tool_width,
    &tool_mid,
    &tool_side,
    nullptr
};

bool func_proc_audio_stereo(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    float width_val = static_cast<float>(tool_width.value);
    float mid_val = static_cast<float>(tool_mid.value);
    float side_val = static_cast<float>(tool_side.value);

    if (width_val == 100.0f && mid_val == 100.0f && side_val == 100.0f) {
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

    float width_ratio = width_val / 100.0f;
    float mid_ratio = mid_val / 100.0f;
    float side_ratio = side_val / 100.0f;

    if (channels >= 2) {
        float term_common = 0.5f * mid_ratio;
        float term_diff = 0.5f * width_ratio * side_ratio;

        float coeff_same = term_common + term_diff;
        float coeff_swap = term_common - term_diff;

        Avx2Utils::MatrixMixStereoAVX2(bufL.data(), bufR.data(), bufL.data(), bufR.data(), total_samples, coeff_same, coeff_swap, coeff_swap, coeff_same);
    }
    else {
        Avx2Utils::ScaleBufferAVX2(bufL.data(), bufL.data(), total_samples, mid_ratio);
        Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_stereo = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_stereo,
    nullptr,
    func_proc_audio_stereo
};
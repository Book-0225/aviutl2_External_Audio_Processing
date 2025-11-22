#include "Eap2Common.h"
#include <vector>
#include <algorithm>
#include <regex>

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
    if (bufL.size() < total_samples) { bufL.resize(total_samples); bufR.resize(total_samples); }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) bufR = bufL;

    float width_ratio = width_val / 100.0f;
    float mid_ratio = mid_val / 100.0f;
    float side_ratio = side_val / 100.0f;

    for (int i = 0; i < total_samples; ++i) {
        float l = bufL[i];
        float r = bufR[i];

        if (channels >= 2) {
            float mid = (l + r) * 0.5f;
            float side = (l - r) * 0.5f;

            side *= width_ratio;

            mid *= mid_ratio;
            side *= side_ratio;

            l = mid + side;
            r = mid - side;
        }
        else {
            l *= mid_ratio;
            r *= mid_ratio;
        }

        bufL[i] = l;
        bufR[i] = r;
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_stereo = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    []() {
        static std::wstring s = std::regex_replace(tool_name, std::wregex(regex_tool_name), TOOL_NAME);
        return s.c_str();
    }(),
    L"音声効果",
    []() {
        static std::wstring s = std::regex_replace(filter_info, std::wregex(regex_info_name), TOOL_NAME);
        return s.c_str();
    }(),
    filter_items_stereo,
    nullptr,
    func_proc_audio_stereo
};
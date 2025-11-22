#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <algorithm>
#include <regex>

#define TOOL_NAME L"Utility"

FILTER_ITEM_TRACK tool_gain(L"Gain", 100.0, 0.0, 500.0, 0.1);
FILTER_ITEM_TRACK tool_pan(L"Pan(L-R)", 0.0, -100.0, 100.0, 0.1);
FILTER_ITEM_CHECK tool_swap(L"Swap(L/R)", false);
FILTER_ITEM_CHECK tool_inv_l(L"Invert L", false);
FILTER_ITEM_CHECK tool_inv_r(L"Invert R", false);
FILTER_ITEM_CHECK tool_mono(L"Mono Mix", false);

void* filter_items_utility[] = {
    &tool_gain,
    &tool_pan,
    &tool_swap,
    &tool_inv_l,
	&tool_inv_r,
    &tool_mono,
    nullptr
};

bool func_proc_audio_utility(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    float gain_val = static_cast<float>(tool_gain.value);
    float pan_val = static_cast<float>(tool_pan.value);
    bool do_swap = tool_swap.value;
    bool do_inv_l = tool_inv_l.value;
    bool do_inv_r = tool_inv_r.value;
    bool do_mono = tool_mono.value;

    if (gain_val == 100.0f && pan_val == 0.0f && !do_swap && !do_inv_l && !do_mono) {
        return true;
    }

    thread_local std::vector<float> bufL, bufR;
    bufL.resize(total_samples);
    bufR.resize(total_samples);

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) bufR = bufL;

    float gain_ratio = gain_val / 100.0f;
    float pan_l = (pan_val > 0.0f) ? (1.0f - pan_val / 100.0f) : 1.0f;
    float pan_r = (pan_val < 0.0f) ? (1.0f + pan_val / 100.0f) : 1.0f;

    for (int i = 0; i < total_samples; ++i) {
        float l = bufL[i];
        float r = bufR[i];

        if (do_swap) std::swap(l, r);

        if (do_inv_l) l = -l;
		if (do_inv_r) r = -r;

        if (do_mono) {
            float m = (l + r) * 0.5f;
            l = m;
            r = m;
        }

        l *= pan_l;
        r *= pan_r;

        l *= gain_ratio;
        r *= gain_ratio;

        bufL[i] = l;
        bufR[i] = r;
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_utility = {
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
    filter_items_utility,
    nullptr,
    func_proc_audio_utility
};
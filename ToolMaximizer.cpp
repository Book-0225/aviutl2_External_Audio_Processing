#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <regex>

#define TOOL_NAME L"Maximizer"

FILTER_ITEM_TRACK max_threshold(L"Threshold", 0.0, -40.0, 0.0, 0.1);
FILTER_ITEM_TRACK max_ceiling(L"Ceiling", -0.1, -20.0, 0.0, 0.1);
FILTER_ITEM_TRACK max_release(L"Release", 50.0, 1.0, 1000.0, 1.0);
FILTER_ITEM_TRACK max_lookahead(L"Lookahead", 5.0, 0.0, 20.0, 0.1);

void* filter_items_maximizer[] = {
    &max_threshold,
    &max_ceiling,
    &max_release,
    &max_lookahead,
    nullptr
};

const int MAX_LOOKAHEAD_BUFFER = 4096;

struct MaximizerState {
    std::vector<float> bufferL;
    std::vector<float> bufferR;
    int write_pos = 0;
    double envelope = 0.0;
    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() {
        bufferL.assign(MAX_LOOKAHEAD_BUFFER, 0.0f);
        bufferR.assign(MAX_LOOKAHEAD_BUFFER, 0.0f);
        write_pos = 0;
        envelope = 0.0;
        initialized = true;
    }

    void clear() {
        if (initialized) {
            std::fill(bufferL.begin(), bufferL.end(), 0.0f);
            std::fill(bufferR.begin(), bufferR.end(), 0.0f);
            write_pos = 0;
            envelope = 0.0;
        }
    }
};

static std::mutex g_max_state_mutex;
static std::map<const void*, MaximizerState> g_max_states;

bool func_proc_maximizer(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    double threshold_db = max_threshold.value;
    double ceiling_db = max_ceiling.value;
    double release_ms = max_release.value;
    double lookahead_ms = max_lookahead.value;

    if (threshold_db >= 0.0 && ceiling_db >= 0.0) {
        return true;
    }

    MaximizerState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_max_state_mutex);
        state = &g_max_states[audio->object];

        if (!state->initialized) {
            state->init();
        }

        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;

    double makeup_gain = std::pow(10.0, -threshold_db / 20.0);
    
    double ceiling_lin = std::pow(10.0, ceiling_db / 20.0);

    double release_coef = std::exp(-1.0 / (release_ms * 0.001 * Fs));

    int lookahead_samples = static_cast<int>(lookahead_ms * 0.001 * Fs);
    if (lookahead_samples >= MAX_LOOKAHEAD_BUFFER) lookahead_samples = MAX_LOOKAHEAD_BUFFER - 1;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < total_samples) { bufL.resize(total_samples); bufR.resize(total_samples); }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) bufR = bufL;

    std::vector<float>& bL = state->bufferL;
    std::vector<float>& bR = state->bufferR;
    int w_pos = state->write_pos;
    double current_env = state->envelope;
    const int buf_mask = MAX_LOOKAHEAD_BUFFER;

    for (int i = 0; i < total_samples; ++i) {
        float l = bufL[i];
        float r = bufR[i];

        l *= (float)makeup_gain;
        r *= (float)makeup_gain;

        bL[w_pos] = l;
        bR[w_pos] = r;

        double in_peak = std::abs(l);
        if (channels >= 2) in_peak = (std::max)(in_peak, (double)std::abs(r));

        if (in_peak > current_env) {
            current_env = in_peak;
        } else {
            current_env = in_peak + release_coef * (current_env - in_peak);
        }

        int r_pos = w_pos - lookahead_samples;
        if (r_pos < 0) r_pos += MAX_LOOKAHEAD_BUFFER;
        
        float out_l = bL[r_pos];
        float out_r = bR[r_pos];

        double gain = 1.0;
        if (current_env > ceiling_lin) {
            gain = ceiling_lin / current_env;
        }

        out_l *= (float)gain;
        out_r *= (float)gain;

        bufL[i] = out_l;
        bufR[i] = out_r;

        w_pos++;
        if (w_pos >= MAX_LOOKAHEAD_BUFFER) w_pos = 0;
    }

    state->write_pos = w_pos;
    state->envelope = current_env;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_maximizer = {
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
    filter_items_maximizer,
    nullptr,
    func_proc_maximizer
};
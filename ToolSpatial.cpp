#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <regex>

#define TOOL_NAME L"Spatial"

FILTER_ITEM_TRACK sp_time(L"Delay Time", 0.0, 0.0, 1000.0, 1.0);
FILTER_ITEM_TRACK sp_feedback(L"Feedback", 0.0, 0.0, 90.0, 1.0);
FILTER_ITEM_TRACK sp_mix(L"Delay Mix", 0.0, 0.0, 100.0, 1.0);
FILTER_ITEM_TRACK sp_pseudo(L"Pseudo Width", 0.0, 0.0, 40.0, 0.1);

void* filter_items_spatial[] = {
    &sp_time,
    &sp_feedback,
    &sp_mix,
    &sp_pseudo,
    nullptr
};

const int MAX_BUFFER_SIZE = 48000 * 2;

struct SpatialState {
    std::vector<float> bufferL;
    std::vector<float> bufferR;
    int write_pos = 0;
    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() {
        bufferL.assign(MAX_BUFFER_SIZE, 0.0f);
        bufferR.assign(MAX_BUFFER_SIZE, 0.0f);
        write_pos = 0;
        initialized = true;
    }

    void clear() {
        if (initialized) {
            std::fill(bufferL.begin(), bufferL.end(), 0.0f);
            std::fill(bufferR.begin(), bufferR.end(), 0.0f);
            write_pos = 0;
        }
    }
};

static std::mutex g_sp_state_mutex;
static std::map<const void*, SpatialState> g_sp_states;

bool func_proc_audio_spatial(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    float d_time = static_cast<float>(sp_time.value);
    float d_fb = static_cast<float>(sp_feedback.value);
    float d_mix = static_cast<float>(sp_mix.value);
    float p_width = static_cast<float>(sp_pseudo.value);

    if (d_time == 0.0f && p_width == 0.0f) {
        return true;
    }

    SpatialState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_sp_state_mutex);
        state = &g_sp_states[audio->object];

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

    int delay_samples = static_cast<int>(d_time * 0.001 * Fs);
    float fb_ratio = d_fb / 100.0f;
    float wet_ratio = d_mix / 100.0f;
    float dry_ratio = 1.0f - wet_ratio;

    if (delay_samples >= MAX_BUFFER_SIZE) delay_samples = MAX_BUFFER_SIZE - 1;

    int pseudo_samples = static_cast<int>(p_width * 0.001 * Fs);
    if (pseudo_samples >= MAX_BUFFER_SIZE) pseudo_samples = MAX_BUFFER_SIZE - 1;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < total_samples) { bufL.resize(total_samples); bufR.resize(total_samples); }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) bufR = bufL;

    int w_pos = state->write_pos;
    const int buf_size = MAX_BUFFER_SIZE;
    std::vector<float>& bL = state->bufferL;
    std::vector<float>& bR = state->bufferR;

    for (int i = 0; i < total_samples; ++i) {
        float l = bufL[i];
        float r = bufR[i];
        if (pseudo_samples > 0) {
            float mono_now = (l + r) * 0.5f;
            l = mono_now;

            int r_read_pos = w_pos - pseudo_samples;
            if (r_read_pos < 0) r_read_pos += buf_size;

            float delayed_mono_hist = (bL[r_read_pos] + bR[r_read_pos]) * 0.5f;
            r = delayed_mono_hist;
        }
        if (delay_samples > 0) {
            int d_read_pos = w_pos - delay_samples;
            if (d_read_pos < 0) d_read_pos += buf_size;

            float delay_l = bL[d_read_pos];
            float delay_r = bR[d_read_pos];

            float out_l = l * dry_ratio + delay_l * wet_ratio;
            float out_r = r * dry_ratio + delay_r * wet_ratio;

            float next_l = l + delay_l * fb_ratio;
            float next_r = r + delay_r * fb_ratio;

            if (next_l > 2.0f) next_l = 2.0f; else if (next_l < -2.0f) next_l = -2.0f;
            if (next_r > 2.0f) next_r = 2.0f; else if (next_r < -2.0f) next_r = -2.0f;

            bL[w_pos] = next_l;
            bR[w_pos] = next_r;

            l = out_l;
            r = out_r;
        }
        else {
            bL[w_pos] = l;
            bR[w_pos] = r;
        }

        w_pos++;
        if (w_pos >= buf_size) w_pos = 0;

        bufL[i] = l;
        bufR[i] = r;
    }

    state->write_pos = w_pos;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_spatial = {
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
    filter_items_spatial,
    nullptr,
    func_proc_audio_spatial
};
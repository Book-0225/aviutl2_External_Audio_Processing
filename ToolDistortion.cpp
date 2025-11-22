#define _USE_MATH_DEFINES

#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <regex>

#define TOOL_NAME L"Distortion"

FILTER_ITEM_CHECK dist_overdrive(L"Overdrive", true);
FILTER_ITEM_CHECK dist_fuzz(L"Fuzz", false);
FILTER_ITEM_CHECK dist_bitcrush(L"Bitcrush", false);
FILTER_ITEM_TRACK dist_drive(L"Drive", 0.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK dist_tone(L"Tone", 20000.0, 100.0, 20000.0, 10.0);
FILTER_ITEM_TRACK dist_bits(L"Bits", 24.0, 1.0, 24.0, 0.1);
FILTER_ITEM_TRACK dist_downsample(L"Downsample", 1.0, 1.0, 50.0, 0.1);
FILTER_ITEM_TRACK dist_mix(L"Mix", 100.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK dist_output(L"Output", 0.0, -20.0, 20.0, 0.1);

void* filter_items_distortion[] = {
    &dist_overdrive,
    &dist_fuzz,
    &dist_bitcrush,
    &dist_drive,
    &dist_tone,
    &dist_bits,
    &dist_downsample,
    &dist_mix,
    &dist_output,
    nullptr
};

struct DistortionState {
    float lp_hist_l = 0.0f;
    float lp_hist_r = 0.0f;
    float sample_hold_l = 0.0f;
    float sample_hold_r = 0.0f;
    int sample_counter = 0;
    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() {
        lp_hist_l = 0.0f;
        lp_hist_r = 0.0f;
        sample_hold_l = 0.0f;
        sample_hold_r = 0.0f;
        sample_counter = 0;
        initialized = true;
    }

    void clear() {
        if (initialized) {
            lp_hist_l = 0.0f;
            lp_hist_r = 0.0f;
            sample_hold_l = 0.0f;
            sample_hold_r = 0.0f;
            sample_counter = 0;
        }
    }
};

static std::mutex g_dist_state_mutex;
static std::map<const void*, DistortionState> g_dist_states;

inline float soft_clip(float x) {
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    return std::tanh(x);
}

inline float hard_clip(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x;
}

inline float fuzz_shape(float x, float drive) {
    float val = x * (1.0f + drive * 10.0f);
    if (val > 0.8f) val = 0.8f;
    if (val < -0.8f) val = -0.8f;
    return val;
}

bool func_proc_audio_distortion(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    bool is_overdrive = dist_overdrive.value;
    bool is_fuzz = dist_fuzz.value;
    bool is_bitcrush = dist_bitcrush.value;
    
    float drive = static_cast<float>(dist_drive.value);
    float tone_freq = static_cast<float>(dist_tone.value);
    float bits = static_cast<float>(dist_bits.value);
    float downsample = static_cast<float>(dist_downsample.value);
    float mix = static_cast<float>(dist_mix.value) / 100.0f;
    float output_db = static_cast<float>(dist_output.value);
    float output_gain = std::pow(10.0f, output_db / 20.0f);

    if (mix == 0.0f) return true;

    DistortionState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dist_state_mutex);
        state = &g_dist_states[audio->object];

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

    float alpha = static_cast<float>(2.0 * M_PI * tone_freq / Fs);
    if (alpha > 1.0f) alpha = 1.0f;

    float step_size = 1.0f / std::pow(2.0f, bits);
    int ds_factor = static_cast<int>(downsample);
    if (ds_factor < 1) ds_factor = 1;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < total_samples) { bufL.resize(total_samples); bufR.resize(total_samples); }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) bufR = bufL;

    float& lp_l = state->lp_hist_l;
    float& lp_r = state->lp_hist_r;
    float& sh_l = state->sample_hold_l;
    float& sh_r = state->sample_hold_r;
    int& s_cnt = state->sample_counter;

    for (int i = 0; i < total_samples; ++i) {
        float l = bufL[i];
        float r = bufR[i];
        
        float dry_l = l;
        float dry_r = r;

        float proc_l = l;
        float proc_r = r;

        if (is_overdrive) {
            float d_gain = 1.0f + drive * 0.5f;
            proc_l = soft_clip(proc_l * d_gain);
            proc_r = soft_clip(proc_r * d_gain);
        }
        if (is_fuzz) {
            float f_drive = drive * 0.1f;
            proc_l = fuzz_shape(proc_l, f_drive);
            proc_r = fuzz_shape(proc_r, f_drive);
        }
        if (is_bitcrush) {
            if (s_cnt % ds_factor == 0) {
                if (bits < 24.0f) {
                    proc_l = std::floor(proc_l / step_size) * step_size;
                    proc_r = std::floor(proc_r / step_size) * step_size;
                }
                sh_l = proc_l;
                sh_r = proc_r;
            } else {
                proc_l = sh_l;
                proc_r = sh_r;
            }
            s_cnt++;
        }

        if (tone_freq < 20000.0f) {
            lp_l += alpha * (proc_l - lp_l);
            lp_r += alpha * (proc_r - lp_r);
            proc_l = lp_l;
            proc_r = lp_r;
        }

        float out_l = dry_l * (1.0f - mix) + proc_l * mix;
        float out_r = dry_r * (1.0f - mix) + proc_r * mix;

        out_l *= output_gain;
        out_r *= output_gain;

        bufL[i] = out_l;
        bufR[i] = out_r;
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_distortion = {
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
    filter_items_distortion,
    nullptr,
    func_proc_audio_distortion
};
#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Distortion"

FILTER_ITEM_CHECK dist_overdrive(L"Overdrive", true);
FILTER_ITEM_CHECK dist_fuzz(L"Fuzz", false);
FILTER_ITEM_CHECK dist_bitcrush(L"Bitcrush", false);
FILTER_ITEM_TRACK dist_drive(L"Drive", 0.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK dist_tone(L"Tone", 20000.0, 100.0, 20000.0, 1.0);
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
    int32_t sample_counter = 0;
    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() {
        lp_hist_l = 0.0f; lp_hist_r = 0.0f;
        sample_hold_l = 0.0f; sample_hold_r = 0.0f;
        sample_counter = 0; initialized = true;
    }
    void clear() { if (initialized) init(); }
};

static std::mutex g_dist_state_mutex;
static std::map<const void*, DistortionState> g_dist_states;
const int32_t BLOCK_SIZE = 64;

bool func_proc_audio_distortion(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

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
        if (!state->initialized) state->init();
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    float alpha = static_cast<float>(2.0 * M_PI * tone_freq / Fs);
    if (alpha > 1.0f) alpha = 1.0f;
    bool tone_active = (tone_freq < 19000.0f);

    float step_size = 1.0f / std::pow(2.0f, bits);
    int32_t ds_factor = static_cast<int32_t>(downsample);
    if (ds_factor < 1) ds_factor = 1;
    bool ds_active = (ds_factor > 1);
    bool quant_active = (bits < 24.0f);

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    alignas(32) float temp_wet_L[BLOCK_SIZE];
    alignas(32) float temp_wet_R[BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* p_dry_L = bufL.data() + i;
        float* p_dry_R = bufR.data() + i;

        Avx2Utils::CopyBufferAVX2(temp_wet_L, p_dry_L, block_count);
        Avx2Utils::CopyBufferAVX2(temp_wet_R, p_dry_R, block_count);

        if (is_overdrive) {
            float d_gain = 1.0f + drive * 0.5f;
            Avx2Utils::SoftClipTanhAVX2(temp_wet_L, block_count, d_gain);
            Avx2Utils::SoftClipTanhAVX2(temp_wet_R, block_count, d_gain);
        }

        if (is_fuzz) {
            float f_drive = drive * 0.1f;
            Avx2Utils::FuzzShapeAVX2(temp_wet_L, block_count, f_drive);
            Avx2Utils::FuzzShapeAVX2(temp_wet_R, block_count, f_drive);
        }

        if (is_bitcrush) {
            if (quant_active) {
                if (!ds_active) {
                    Avx2Utils::QuantizeAVX2(temp_wet_L, block_count, step_size);
                    Avx2Utils::QuantizeAVX2(temp_wet_R, block_count, step_size);
                }
            }

            if (ds_active) {
                for (int32_t k = 0; k < block_count; ++k) {
                    if (state->sample_counter % ds_factor == 0) {
                        float l = temp_wet_L[k];
                        float r = temp_wet_R[k];
                        if (quant_active) {
                            l = std::floor(l / step_size) * step_size;
                            r = std::floor(r / step_size) * step_size;
                        }
                        state->sample_hold_l = l;
                        state->sample_hold_r = r;
                    }
                    temp_wet_L[k] = state->sample_hold_l;
                    temp_wet_R[k] = state->sample_hold_r;
                    state->sample_counter++;
                }
            }
        }

        if (tone_active) {
            for (int32_t k = 0; k < block_count; ++k) {
                state->lp_hist_l += alpha * (temp_wet_L[k] - state->lp_hist_l);
                state->lp_hist_r += alpha * (temp_wet_R[k] - state->lp_hist_r);
                temp_wet_L[k] = state->lp_hist_l;
                temp_wet_R[k] = state->lp_hist_r;
            }
        }

        Avx2Utils::MixAudioAVX2(p_dry_L, temp_wet_L, block_count, 1.0f - mix, mix, output_gain);
        Avx2Utils::MixAudioAVX2(p_dry_R, temp_wet_R, block_count, 1.0f - mix, mix, output_gain);
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

void CleanupDistortionResources() {
    g_dist_states.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_distortion = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_distortion,
    nullptr,
    func_proc_audio_distortion
};
#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Modulation"

FILTER_ITEM_CHECK mod_chorus(L"Chorus", true);
FILTER_ITEM_CHECK mod_flanger(L"Flanger", false);
FILTER_ITEM_CHECK mod_tremolo(L"Tremolo", false);
FILTER_ITEM_TRACK mod_rate(L"Rate", 1.0, 0.01, 20.0, 0.01);
FILTER_ITEM_TRACK mod_depth(L"Depth", 50.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK mod_feedback(L"Feedback", 0.0, 0.0, 95.0, 0.1);
FILTER_ITEM_TRACK mod_delay(L"Delay", 10.0, 0.1, 50.0, 0.1);
FILTER_ITEM_TRACK mod_mix(L"Mix", 50.0, 0.0, 100.0, 0.1);

void* filter_items_modulation[] = {
    &mod_chorus,
    &mod_flanger,
    &mod_tremolo,
    &mod_rate,
    &mod_depth,
    &mod_feedback,
    &mod_delay,
    &mod_mix,
    nullptr
};

const int32_t MAX_BUFFER_SIZE = 48000 * 2;
const int32_t BLOCK_SIZE = 64;

struct ModulationState {
    std::vector<float> bufferL;
    std::vector<float> bufferR;
    int32_t write_pos = 0;
    double phase = 0.0;
    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() {
        bufferL.assign(MAX_BUFFER_SIZE, 0.0f); bufferR.assign(MAX_BUFFER_SIZE, 0.0f);
        write_pos = 0; phase = 0.0; initialized = true;
    }
    void clear() {
        if (initialized) {
            Avx2Utils::FillBufferAVX2(bufferL.data(), bufferL.size(), 0.0f);
            Avx2Utils::FillBufferAVX2(bufferR.data(), bufferR.size(), 0.0f);
            write_pos = 0; phase = 0.0;
        }
    }
};

static std::mutex g_mod_state_mutex;
static std::map<const void*, ModulationState> g_mod_states;

inline float interpolate(const float* buffer, double index, int32_t size) {
    int32_t i = static_cast<int32_t>(index);
    float frac = static_cast<float>(index - i);
    if (i >= size) i -= size;
    int32_t i_next = i + 1;
    if (i_next >= size) i_next = 0;
    return buffer[i] * (1.0f - frac) + buffer[i_next] * frac;
}

bool func_proc_audio_modulation(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    bool is_chorus = mod_chorus.value;
    bool is_flanger = mod_flanger.value;
    bool is_tremolo = mod_tremolo.value;
    bool is_delay_mod = is_chorus || is_flanger;

    float rate = static_cast<float>(mod_rate.value);
    float depth = static_cast<float>(mod_depth.value) / 100.0f;
    float feedback = static_cast<float>(mod_feedback.value) / 100.0f;
    float base_delay_ms = static_cast<float>(mod_delay.value);
    float mix_val = static_cast<float>(mod_mix.value);
    float mix = mix_val / 100.0f;

    if (!is_delay_mod && !is_tremolo) return true;

    ModulationState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mod_state_mutex);
        state = &g_mod_states[audio->object];
        if (!state->initialized) state->init();
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    double lfo_inc = (2.0 * M_PI * rate) / Fs;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    int32_t w_pos = state->write_pos;
    const int32_t buf_size = MAX_BUFFER_SIZE;
    float* bL = state->bufferL.data();
    float* bR = state->bufferR.data();
    double current_phase = state->phase;

    float base_delay_samples = static_cast<float>(base_delay_ms * 0.001 * Fs);

    alignas(32) float temp_wet_L[BLOCK_SIZE];
    alignas(32) float temp_wet_R[BLOCK_SIZE];
    alignas(32) float temp_tremolo_mod[BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* p_dry_L = bufL.data() + i;
        float* p_dry_R = bufR.data() + i;

        if (is_delay_mod) {
            for (int32_t k = 0; k < block_count; ++k) {
                float lfo = static_cast<float>(std::sin(current_phase));
                if (!is_tremolo) {
                    current_phase += lfo_inc;
                    if (current_phase > 2.0 * M_PI) current_phase -= 2.0 * M_PI;
                }

                float mod_delay_samples = base_delay_samples * (1.0f + lfo * depth * 0.5f);
                if (mod_delay_samples < 1.0f) mod_delay_samples = 1.0f;
                if (mod_delay_samples > MAX_BUFFER_SIZE - 100) mod_delay_samples = MAX_BUFFER_SIZE - 100;

                double read_pos = w_pos - mod_delay_samples;
                if (read_pos < 0) read_pos += buf_size;

                float delayed_l = interpolate(bL, read_pos, buf_size);
                float delayed_r = interpolate(bR, read_pos, buf_size);

                float next_l = p_dry_L[k] + delayed_l * feedback;
                float next_r = p_dry_R[k] + delayed_r * feedback;
                if (next_l > 2.0f) next_l = 2.0f; else if (next_l < -2.0f) next_l = -2.0f;
                if (next_r > 2.0f) next_r = 2.0f; else if (next_r < -2.0f) next_r = -2.0f;

                bL[w_pos] = next_l;
                bR[w_pos] = next_r;

                temp_wet_L[k] = delayed_l;
                temp_wet_R[k] = delayed_r;

                if (is_tremolo) {
                    temp_tremolo_mod[k] = 1.0f - depth * (0.5f * (lfo + 1.0f));

                    current_phase += lfo_inc;
                    if (current_phase > 2.0 * M_PI) current_phase -= 2.0 * M_PI;
                }

                w_pos++;
                if (w_pos >= buf_size) w_pos = 0;
            }

            Avx2Utils::MixAudioAVX2(p_dry_L, temp_wet_L, block_count, 1.0f - mix, mix, 1.0f);
            Avx2Utils::MixAudioAVX2(p_dry_R, temp_wet_R, block_count, 1.0f - mix, mix, 1.0f);

        }
        else {
            for (int32_t k = 0; k < block_count; ++k) {
                bL[w_pos] = p_dry_L[k];
                bR[w_pos] = p_dry_R[k];

                if (is_tremolo) {
                    float lfo = static_cast<float>(std::sin(current_phase));
                    temp_tremolo_mod[k] = 1.0f - depth * (0.5f * (lfo + 1.0f));
                    current_phase += lfo_inc;
                    if (current_phase > 2.0 * M_PI) current_phase -= 2.0 * M_PI;
                }

                w_pos++;
                if (w_pos >= buf_size) w_pos = 0;
            }
        }

        if (is_tremolo) {
            Avx2Utils::MultiplyBufferAVX2(p_dry_L, temp_tremolo_mod, block_count);
            Avx2Utils::MultiplyBufferAVX2(p_dry_R, temp_tremolo_mod, block_count);
        }
    }

    state->write_pos = w_pos;
    state->phase = current_phase;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_modulation = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_modulation,
    nullptr,
    func_proc_audio_modulation
};
#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Pitch Shift"

FILTER_ITEM_TRACK ps_pitch(L"Pitch", 0.0, -12.0, 12.0, 0.1);
FILTER_ITEM_TRACK ps_mix(L"Mix", 100.0, 0.0, 100.0, 0.1);

void* filter_items_pitch_shift[] = {
    &ps_pitch,
    &ps_mix,
    nullptr
};

const int MAX_BUFFER_SIZE = 48000;
const int BLOCK_SIZE = 64;

struct PitchShiftState {
    std::vector<float> bufferL;
    std::vector<float> bufferR;
    int write_pos = 0;
    double read_pos_a = 0.0;

    static const int WINDOW_SIZE = 4096;

    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() {
        bufferL.assign(MAX_BUFFER_SIZE, 0.0f);
        bufferR.assign(MAX_BUFFER_SIZE, 0.0f);
        write_pos = 0;
        read_pos_a = 0.0;
        initialized = true;
    }
    void clear() {
        if (initialized) {
            std::fill(bufferL.begin(), bufferL.end(), 0.0f);
            std::fill(bufferR.begin(), bufferR.end(), 0.0f);
            write_pos = 0;
            read_pos_a = 0.0;
        }
    }
};

static std::mutex g_ps_state_mutex;
static std::map<const void*, PitchShiftState> g_ps_states;

inline float interpolate_circ(const float* buffer, double index, int size) {
    int i = static_cast<int>(index);
    float frac = static_cast<float>(index - i);
    if (i >= size) i -= size;

    int i_next = i + 1;
    if (i_next >= size) i_next = 0;

    return buffer[i] * (1.0f - frac) + buffer[i_next] * frac;
}

bool func_proc_audio_pitch_shift(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    float pitch = static_cast<float>(ps_pitch.value);
    float mix_val = static_cast<float>(ps_mix.value);
    float mix = mix_val / 100.0f;

    if (pitch == 0.0f && mix < 1.0f) return true;
    if (mix <= 0.001f) return true;

    PitchShiftState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_ps_state_mutex);
        state = &g_ps_states[audio->object];
        if (!state->initialized) state->init();
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    float rate = std::pow(2.0f, pitch / 12.0f);
    double dt = (1.0 - rate) / PitchShiftState::WINDOW_SIZE;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    int w_pos = state->write_pos;
    const int buf_size = MAX_BUFFER_SIZE;
    float* bL = state->bufferL.data();
    float* bR = state->bufferR.data();

    double r_pos_a = state->read_pos_a;
    const double win_size = static_cast<double>(PitchShiftState::WINDOW_SIZE);
    alignas(32) float temp_wet_L[BLOCK_SIZE];
    alignas(32) float temp_wet_R[BLOCK_SIZE];

    for (int i = 0; i < total_samples; i += BLOCK_SIZE) {
        int block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* p_dry_L = bufL.data() + i;
        float* p_dry_R = bufR.data() + i;

        for (int k = 0; k < block_count; ++k) {
            bL[w_pos] = p_dry_L[k];
            bR[w_pos] = p_dry_R[k];

            r_pos_a += dt;
            if (r_pos_a >= 1.0) r_pos_a -= 1.0;
            if (r_pos_a < 0.0) r_pos_a += 1.0;

            double r_pos_b = r_pos_a + 0.5;
            if (r_pos_b >= 1.0) r_pos_b -= 1.0;

            double delay_a = r_pos_a * win_size;
            double delay_b = r_pos_b * win_size;

            double read_idx_a = w_pos - delay_a;
            if (read_idx_a < 0) read_idx_a += buf_size;

            double read_idx_b = w_pos - delay_b;
            if (read_idx_b < 0) read_idx_b += buf_size;

            float gain_a = (r_pos_a < 0.5) ? static_cast<float>(2.0 * r_pos_a) : static_cast<float>(2.0 * (1.0 - r_pos_a));
            float gain_b = (r_pos_b < 0.5) ? static_cast<float>(2.0 * r_pos_b) : static_cast<float>(2.0 * (1.0 - r_pos_b));

            float out_l = interpolate_circ(bL, read_idx_a, buf_size) * gain_a
                + interpolate_circ(bL, read_idx_b, buf_size) * gain_b;

            float out_r = interpolate_circ(bR, read_idx_a, buf_size) * gain_a
                + interpolate_circ(bR, read_idx_b, buf_size) * gain_b;

            temp_wet_L[k] = out_l;
            temp_wet_R[k] = out_r;

            w_pos++;
            if (w_pos >= buf_size) w_pos = 0;
        }

        Avx2Utils::MixAudioAVX2(p_dry_L, temp_wet_L, block_count, 1.0f - mix, mix, 1.0f);
        Avx2Utils::MixAudioAVX2(p_dry_R, temp_wet_R, block_count, 1.0f - mix, mix, 1.0f);
    }

    state->write_pos = w_pos;
    state->read_pos_a = r_pos_a;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_pitch_shift = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_pitch_shift,
    nullptr,
    func_proc_audio_pitch_shift
};
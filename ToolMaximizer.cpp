#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h"

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

const int32_t MAX_LOOKAHEAD_BUFFER = 4096;
const int32_t BLOCK_SIZE = 64;

struct MaximizerState {
    std::vector<float> bufferL;
    std::vector<float> bufferR;
    int32_t write_pos = 0;
    double envelope = 0.0;
    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() {
        bufferL.assign(MAX_LOOKAHEAD_BUFFER, 0.0f); bufferR.assign(MAX_LOOKAHEAD_BUFFER, 0.0f);
        write_pos = 0; envelope = 0.0; initialized = true;
    }

    void clear() {
        if (initialized) {
            Avx2Utils::FillBufferAVX2(bufferL.data(), bufferL.size(), 0.0f);
            Avx2Utils::FillBufferAVX2(bufferR.data(), bufferR.size(), 0.0f);
            write_pos = 0; envelope = 0.0;
        }
    }
};

static std::mutex g_max_state_mutex;
static std::map<const void*, MaximizerState> g_max_states;

bool func_proc_maximizer(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    double threshold_db = max_threshold.value;
    double ceiling_db = max_ceiling.value;
    double release_ms = max_release.value;
    double lookahead_ms = max_lookahead.value;

    if (threshold_db >= 0.0 && ceiling_db >= 0.0) return true;

    MaximizerState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_max_state_mutex);
        state = &g_max_states[audio->object];
        if (!state->initialized) state->init();
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    float makeup_gain = static_cast<float>(std::pow(10.0, -threshold_db / 20.0));
    double ceiling_lin = std::pow(10.0, ceiling_db / 20.0);
    double release_coef = std::exp(-1.0 / (release_ms * 0.001 * Fs));

    int32_t lookahead_samples = static_cast<int32_t>(lookahead_ms * 0.001 * Fs);
    if (lookahead_samples >= MAX_LOOKAHEAD_BUFFER) lookahead_samples = MAX_LOOKAHEAD_BUFFER - 1;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    int32_t w_pos = state->write_pos;
    double current_env = state->envelope;
    const int32_t buf_size = MAX_LOOKAHEAD_BUFFER;

    thread_local std::vector<float> peak_buf, gain_buf;
    if (peak_buf.size() < static_cast<size_t>(total_samples)) {
        peak_buf.resize(total_samples);
        gain_buf.resize(total_samples);
    }

    alignas(32) float temp_out_L[BLOCK_SIZE];
    alignas(32) float temp_out_R[BLOCK_SIZE];

    Avx2Utils::ScaleBufferAVX2(bufL.data(), bufL.data(), total_samples, makeup_gain);
    Avx2Utils::ScaleBufferAVX2(bufR.data(), bufR.data(), total_samples, makeup_gain);
    Avx2Utils::PeakDetectStereoAVX2(peak_buf.data(), bufL.data(), bufR.data(), total_samples);

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* pL = bufL.data() + i;
        float* pR = bufR.data() + i;
        float* p_peak = peak_buf.data() + i;

        Avx2Utils::WriteRingBufferAVX2(state->bufferL, pL, buf_size, w_pos, block_count);
        Avx2Utils::WriteRingBufferAVX2(state->bufferR, pR, buf_size, w_pos, block_count);

        for (int32_t k = 0; k < block_count; ++k) {
            double in_peak = (double)p_peak[k];

            if (in_peak > current_env) {
                current_env = in_peak;
            }
            else {
                current_env = in_peak + release_coef * (current_env - in_peak);
            }

            double gain = 1.0;
            if (current_env > ceiling_lin) {
                gain = ceiling_lin / current_env;
            }
            gain_buf[i + k] = static_cast<float>(gain);
        }

        int32_t r_pos = w_pos - lookahead_samples;
        if (r_pos < 0) r_pos += buf_size;

        Avx2Utils::ReadRingBufferAVX2(temp_out_L, state->bufferL, buf_size, r_pos, block_count);
        Avx2Utils::ReadRingBufferAVX2(temp_out_R, state->bufferR, buf_size, r_pos, block_count);
        Avx2Utils::MultiplyBufferAVX2(pL, temp_out_L, gain_buf.data() + i, block_count);
        Avx2Utils::MultiplyBufferAVX2(pR, temp_out_R, gain_buf.data() + i, block_count);
        w_pos += block_count;
        if (w_pos >= buf_size) w_pos -= buf_size;
    }

    state->write_pos = w_pos;
    state->envelope = current_env;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

void CleanupMaximizerResources() {
    g_max_states.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_maximizer = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_maximizer,
    nullptr,
    func_proc_maximizer
};
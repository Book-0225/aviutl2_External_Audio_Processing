#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <random>
#include "Avx2Utils.h"

#define TOOL_NAME L"Generator"

FILTER_ITEM_SELECT::ITEM gen_type_list[] = {
    { L"Sine", 0 },
    { L"Square", 1 },
    { L"Triangle", 2 },
    { L"Saw", 3 },
    { L"White Noise", 4 },
    { L"Pink Noise", 5 },
    { nullptr, 0 }
};
FILTER_ITEM_SELECT gen_type(L"Waveform", 0, gen_type_list);
FILTER_ITEM_TRACK gen_freq(L"Frequency", 440.0, 20.0, 20000.0, 1.0);

void* filter_items_generator[] = {
    &gen_type,
    &gen_freq,
    nullptr
};

const int32_t BLOCK_SIZE = 64;

struct GeneratorState {
    double phase = 0.0;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    bool initialized = false;
    int64_t last_sample_index = -1;

    void init() { phase = 0.0; b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f; initialized = true; }
    void clear() { if (initialized) init(); }
};

static std::mutex g_gen_state_mutex;
static std::map<const void*, GeneratorState> g_gen_states;
static std::mt19937 g_rng(12345);
static std::uniform_real_distribution<float> g_dist(-1.0f, 1.0f);

bool func_proc_audio_generator(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    int32_t type = gen_type.value;
    float freq = static_cast<float>(gen_freq.value);

    GeneratorState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_gen_state_mutex);
        state = &g_gen_states[audio->object];
        if (!state->initialized) state->init();
        if (state->last_sample_index != -1 &&
            std::abs(state->last_sample_index + total_samples - audio->object->sample_index) > 100) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    double phase_inc = (2.0 * M_PI * freq) / Fs;
    double current_phase = state->phase;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    alignas(32) float temp_gen[BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);

        for (int32_t k = 0; k < block_count; ++k) {
            float sample = 0.0f;
            switch (type) {
            case 0:
                sample = static_cast<float>(std::sin(current_phase));
                break;
            case 1:
                sample = (current_phase < M_PI) ? 1.0f : -1.0f;
                break;
            case 2:
                sample = static_cast<float>(2.0f / M_PI * std::asin(std::sin(current_phase)));
                break;
            case 3:
                sample = static_cast<float>(1.0 - (current_phase / M_PI));
                break;
            case 4:
                sample = g_dist(g_rng);
                break;
            case 5:
                float white = g_dist(g_rng);
                state->b0 = 0.99886f * state->b0 + white * 0.0555179f;
                state->b1 = 0.99332f * state->b1 + white * 0.0750759f;
                state->b2 = 0.96900f * state->b2 + white * 0.1538520f;
                state->b3 = 0.86650f * state->b3 + white * 0.3104856f;
                state->b4 = 0.55000f * state->b4 + white * 0.5329522f;
                state->b5 = -0.7616f * state->b5 - white * 0.0168980f;
                sample = state->b0 + state->b1 + state->b2 + state->b3 + state->b4 + state->b5 + state->b6 + white * 0.5362f;
                state->b6 = white * 0.115926f;
                sample *= 0.11f;
                break;
            }
            temp_gen[k] = sample;

            if (type <= 3) {
                current_phase += phase_inc;
                if (current_phase > 2.0 * M_PI) current_phase -= 2.0 * M_PI;
            }
        }

        Avx2Utils::CopyBufferAVX2(bufL.data() + i, temp_gen, block_count);
        Avx2Utils::CopyBufferAVX2(bufR.data() + i, temp_gen, block_count);
    }

    state->phase = current_phase;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_generator = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO | FILTER_PLUGIN_TABLE::FLAG_INPUT,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_generator,
    nullptr,
    func_proc_audio_generator
};
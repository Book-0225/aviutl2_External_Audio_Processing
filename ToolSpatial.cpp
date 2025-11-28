#include "Eap2Common.h"
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h"

#define TOOL_NAME L"Spatial"

FILTER_ITEM_TRACK sp_time(L"Delay Time", 0.0, 0.0, 1000.0, 1.0);
FILTER_ITEM_TRACK sp_feedback(L"Feedback", 0.0, 0.0, 90.0, 1.0);
FILTER_ITEM_TRACK sp_mix(L"Delay Mix", 0.0, 0.0, 100.0, 1.0);
FILTER_ITEM_TRACK sp_pseudo(L"Pseudo Width", 0.0, 0.0, 40.0, 0.1);

void* filter_items_spatial[] = {
    &sp_time, &sp_feedback, &sp_mix, &sp_pseudo, nullptr
};

const int MAX_BUFFER_SIZE = 48000 * 2;
const int BLOCK_SIZE = 64;

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

inline void ReadRingBufferBlock(
    float* out, const std::vector<float>& buf,
    int w_pos, int delay_samples, int count)
{
    const int buf_size = static_cast<int>(buf.size());
    int r_pos = w_pos - delay_samples;
    if (r_pos < 0) r_pos += buf_size;

    int first_chunk = (std::min)(count, buf_size - r_pos);
    Avx2Utils::CopyBufferAVX2(out, buf.data() + r_pos, first_chunk);

    if (first_chunk < count) {
        Avx2Utils::CopyBufferAVX2(out + first_chunk, buf.data(), count - first_chunk);
    }
}

inline void WriteRingBufferBlock(
    std::vector<float>& buf, const float* in,
    int w_pos, int count)
{
    const int buf_size = static_cast<int>(buf.size());
    int first_chunk = (std::min)(count, buf_size - w_pos);

    Avx2Utils::CopyBufferAVX2(buf.data() + w_pos, in, first_chunk);

    if (first_chunk < count) {
        Avx2Utils::CopyBufferAVX2(buf.data(), in + first_chunk, count - first_chunk);
    }
}


bool func_proc_audio_spatial(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    float d_time = static_cast<float>(sp_time.value);
    float d_fb = static_cast<float>(sp_feedback.value);
    float d_mix = static_cast<float>(sp_mix.value);
    float p_width = static_cast<float>(sp_pseudo.value);

    if (d_time == 0.0f && p_width == 0.0f) return true;

    SpatialState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_sp_state_mutex);
        state = &g_sp_states[audio->object];
        if (!state->initialized) state->init();
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    int delay_samples = static_cast<int>(d_time * 0.001 * Fs);
    if (delay_samples >= MAX_BUFFER_SIZE) delay_samples = MAX_BUFFER_SIZE - 1;

    int pseudo_samples = static_cast<int>(p_width * 0.001 * Fs);
    if (pseudo_samples >= MAX_BUFFER_SIZE) pseudo_samples = MAX_BUFFER_SIZE - 1;

    float fb_ratio = d_fb / 100.0f;
    float wet_ratio = d_mix / 100.0f;
    float dry_ratio = 1.0f - wet_ratio;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    alignas(32) float temp_in_L[BLOCK_SIZE];
    alignas(32) float temp_in_R[BLOCK_SIZE];
    alignas(32) float temp_delay_L[BLOCK_SIZE];
    alignas(32) float temp_delay_R[BLOCK_SIZE];
    alignas(32) float temp_next_L[BLOCK_SIZE];
    alignas(32) float temp_next_R[BLOCK_SIZE];
    alignas(32) float temp_mono[BLOCK_SIZE];

    int current_w_pos = state->write_pos;
    const int buf_size = MAX_BUFFER_SIZE;

    for (int i = 0; i < total_samples; i += BLOCK_SIZE) {
        int block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* pL = bufL.data() + i;
        float* pR = bufR.data() + i;

        if (pseudo_samples > 0) {
            Avx2Utils::MatrixMixStereoAVX2(temp_mono, temp_mono, pL, pR, block_count, 0.5f, 0.5f, 0.0f, 0.0f);
            Avx2Utils::CopyBufferAVX2(temp_in_L, temp_mono, block_count);
            ReadRingBufferBlock(temp_delay_L, state->bufferL, current_w_pos, pseudo_samples, block_count);
            ReadRingBufferBlock(temp_delay_R, state->bufferR, current_w_pos, pseudo_samples, block_count);
            Avx2Utils::MatrixMixStereoAVX2(temp_in_R, temp_in_R, temp_delay_L, temp_delay_R, block_count, 0.5f, 0.5f, 0.0f, 0.0f);
        }
        else {
            Avx2Utils::CopyBufferAVX2(temp_in_L, pL, block_count);
            Avx2Utils::CopyBufferAVX2(temp_in_R, pR, block_count);
        }

        if (delay_samples > 0) {
            ReadRingBufferBlock(temp_delay_L, state->bufferL, current_w_pos, delay_samples, block_count);
            ReadRingBufferBlock(temp_delay_R, state->bufferR, current_w_pos, delay_samples, block_count);
            Avx2Utils::CopyBufferAVX2(temp_next_L, temp_in_L, block_count);
            Avx2Utils::CopyBufferAVX2(temp_next_R, temp_in_R, block_count);
            Avx2Utils::AccumulateScaledAVX2(temp_next_L, temp_delay_L, block_count, fb_ratio);
            Avx2Utils::AccumulateScaledAVX2(temp_next_R, temp_delay_R, block_count, fb_ratio);
            Avx2Utils::HardClipAVX2(temp_next_L, block_count, -2.0f, 2.0f);
            Avx2Utils::HardClipAVX2(temp_next_R, block_count, -2.0f, 2.0f);
            WriteRingBufferBlock(state->bufferL, temp_next_L, current_w_pos, block_count);
            WriteRingBufferBlock(state->bufferR, temp_next_R, current_w_pos, block_count);
            Avx2Utils::MixAudioAVX2(pL, temp_delay_L, block_count, wet_ratio, dry_ratio, 1.0f);
            Avx2Utils::ScaleBufferAVX2(pL, temp_in_L, block_count, dry_ratio);
            Avx2Utils::AccumulateScaledAVX2(pL, temp_delay_L, block_count, wet_ratio);
            Avx2Utils::ScaleBufferAVX2(pR, temp_in_R, block_count, dry_ratio);
            Avx2Utils::AccumulateScaledAVX2(pR, temp_delay_R, block_count, wet_ratio);
        }
        else {
            WriteRingBufferBlock(state->bufferL, temp_in_L, current_w_pos, block_count);
            WriteRingBufferBlock(state->bufferR, temp_in_R, current_w_pos, block_count);
            Avx2Utils::CopyBufferAVX2(pL, temp_in_L, block_count);
            Avx2Utils::CopyBufferAVX2(pR, temp_in_R, block_count);
        }
        current_w_pos += block_count;
        if (current_w_pos >= buf_size) current_w_pos -= buf_size;
    }
    state->write_pos = current_w_pos;
    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_spatial = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_spatial,
    nullptr,
    func_proc_audio_spatial
};
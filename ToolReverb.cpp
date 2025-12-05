#include "Eap2Common.h"
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include "Avx2Utils.h" 

#define TOOL_NAME L"Reverb"

FILTER_ITEM_TRACK rev_time(L"Room Size", 50.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK rev_damping(L"Damping", 50.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK rev_predelay(L"Pre-Delay", 0.0, 0.0, 200.0, 1.0);
FILTER_ITEM_TRACK rev_mix(L"Mix", 30.0, 0.0, 100.0, 0.1);

void* filter_items_reverb[] = {
    &rev_time,
    &rev_damping,
    &rev_predelay,
    &rev_mix,
    nullptr
};

const int32_t MAX_BUFFER_SIZE = 48000 * 4;
const int32_t PROCESS_BLOCK_SIZE = 64;

class CombFilter {
public:
    std::vector<float> buffer;
    int32_t buf_size = 0;
    int32_t write_pos = 0;
    float feedback = 0.0f;
    float damp = 0.0f;
    float filter_store = 0.0f;

    void set_buffer_size(int32_t size) {
        if (buf_size != size) {
            buffer.assign(size, 0.0f);
            buf_size = size;
            write_pos = 0;
            filter_store = 0.0f;
        }
    }

    inline float process_sample(float input) {
        float output = buffer[write_pos];
        filter_store = output * (1.0f - damp) + filter_store * damp;
        buffer[write_pos] = input + filter_store * feedback;

        write_pos++;
        if (write_pos >= buf_size) write_pos = 0;
        return output;
    }

    void process_block(float* out, const float* in, int32_t count) {
        if (buf_size == 0) {
            Avx2Utils::FillBufferAVX2(out, count, 0.0f);
            return;
        }

        for (int32_t i = 0; i < count; ++i) {
            out[i] = process_sample(in[i]);
        }
    }

    void clear() {
        Avx2Utils::FillBufferAVX2(buffer.data(), buffer.size(), 0.0f);
        filter_store = 0.0f;
        write_pos = 0;
    }
};

class AllPassFilter {
public:
    std::vector<float> buffer;
    int32_t buf_size = 0;
    int32_t write_pos = 0;
    float feedback = 0.5f;

    void set_buffer_size(int32_t size) {
        if (buf_size != size) {
            buffer.assign(size, 0.0f);
            buf_size = size;
            write_pos = 0;
        }
    }

    inline float process_sample(float input) {
        float buf_out = buffer[write_pos];
        float output = -input + buf_out;
        buffer[write_pos] = input + (buf_out * feedback);

        write_pos++;
        if (write_pos >= buf_size) write_pos = 0;
        return output;
    }

    void process_block(float* data, int32_t count) {
        if (buf_size == 0) return;
        for (int32_t i = 0; i < count; ++i) {
            data[i] = process_sample(data[i]);
        }
    }

    void clear() {
        Avx2Utils::FillBufferAVX2(buffer.data(), buffer.size(), 0.0f);
        write_pos = 0;
    }
};

struct ReverbState {
    static const int32_t NUM_COMBS = 4;
    static const int32_t NUM_ALLPASS = 2;

    CombFilter combsL[NUM_COMBS];
    CombFilter combsR[NUM_COMBS];
    AllPassFilter allpassL[NUM_ALLPASS];
    AllPassFilter allpassR[NUM_ALLPASS];

    std::vector<float> pre_delay_bufL;
    std::vector<float> pre_delay_bufR;
    int32_t pre_delay_write_pos = 0;

    bool initialized = false;
    int64_t last_sample_index = -1;

    const int32_t comb_tuningsL[4] = { 1116, 1188, 1277, 1356 };
    const int32_t comb_tuningsR[4] = { 1116 + 23, 1188 + 23, 1277 + 23, 1356 + 23 };
    const int32_t allpass_tuningsL[2] = { 556, 441 };
    const int32_t allpass_tuningsR[2] = { 556 + 23, 441 + 23 };

    void init(double sample_rate) {
        double sr_scale = sample_rate / 44100.0;

        for (int32_t i = 0; i < NUM_COMBS; ++i) {
            combsL[i].set_buffer_size(static_cast<int32_t>(comb_tuningsL[i] * sr_scale));
            combsR[i].set_buffer_size(static_cast<int32_t>(comb_tuningsR[i] * sr_scale));
        }

        for (int32_t i = 0; i < NUM_ALLPASS; ++i) {
            allpassL[i].set_buffer_size(static_cast<int32_t>(allpass_tuningsL[i] * sr_scale));
            allpassR[i].set_buffer_size(static_cast<int32_t>(allpass_tuningsR[i] * sr_scale));
        }

        pre_delay_bufL.assign(MAX_BUFFER_SIZE, 0.0f);
        pre_delay_bufR.assign(MAX_BUFFER_SIZE, 0.0f);
        pre_delay_write_pos = 0;

        initialized = true;
    }

    void update_params(float room_size, float damping) {
        float fb = 0.7f + (room_size / 100.0f) * 0.28f;
        float d = damping / 100.0f * 0.4f;

        for (int32_t i = 0; i < NUM_COMBS; ++i) {
            combsL[i].feedback = fb;
            combsR[i].feedback = fb;
            combsL[i].damp = d;
            combsR[i].damp = d;
        }
    }

    void clear() {
        if (!initialized) return;
        for (int32_t i = 0; i < NUM_COMBS; ++i) { combsL[i].clear(); combsR[i].clear(); }
        for (int32_t i = 0; i < NUM_ALLPASS; ++i) { allpassL[i].clear(); allpassR[i].clear(); }
        Avx2Utils::FillBufferAVX2(pre_delay_bufL.data(), pre_delay_bufL.size(), 0.0f);
        Avx2Utils::FillBufferAVX2(pre_delay_bufR.data(), pre_delay_bufR.size(), 0.0f);
        pre_delay_write_pos = 0;
    }
};

static std::mutex g_rev_state_mutex;
static std::map<const void*, ReverbState> g_rev_states;

inline void ProcessPreDelayBlock(
    float* out, const float* in,
    std::vector<float>& buf, int32_t& w_pos,
    int32_t delay_samples, int32_t count)
{
    const int32_t buf_size = static_cast<int32_t>(buf.size());
    int32_t r_pos = w_pos - delay_samples;
    if (r_pos < 0) r_pos += buf_size;

    for (int32_t i = 0; i < count; ++i) {
        buf[w_pos] = in[i];
        out[i] = buf[r_pos];

        w_pos++;
        if (w_pos >= buf_size) w_pos = 0;
        r_pos++;
        if (r_pos >= buf_size) r_pos = 0;
    }
}

bool func_proc_audio_reverb(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    float room_size = static_cast<float>(rev_time.value);
    float damping = static_cast<float>(rev_damping.value);
    float pre_delay_ms = static_cast<float>(rev_predelay.value);
    float mix_percent = static_cast<float>(rev_mix.value);
    float mix = mix_percent / 100.0f;

    if (mix <= 0.0f) return true;

    ReverbState* state = nullptr;
    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;

    {
        std::lock_guard<std::mutex> lock(g_rev_state_mutex);
        state = &g_rev_states[audio->object];

        if (!state->initialized) {
            state->init(Fs);
        }

        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    state->update_params(room_size, damping);

    int32_t pre_delay_samples = static_cast<int32_t>(pre_delay_ms * 0.001 * Fs);
    if (pre_delay_samples >= MAX_BUFFER_SIZE) pre_delay_samples = MAX_BUFFER_SIZE - 1;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    alignas(32) float temp_wet_in[PROCESS_BLOCK_SIZE];
    alignas(32) float temp_comb_out[PROCESS_BLOCK_SIZE];
    alignas(32) float temp_accum[PROCESS_BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += PROCESS_BLOCK_SIZE) {
        int32_t block_size = (std::min)(PROCESS_BLOCK_SIZE, total_samples - i);

        float* p_dry_l = bufL.data() + i;
        float* p_dry_r = bufR.data() + i;

        int32_t current_w_pos = state->pre_delay_write_pos;
        int32_t next_w_pos = current_w_pos;

        {
            int32_t w_pos_l = current_w_pos;

            ProcessPreDelayBlock(temp_wet_in, p_dry_l, state->pre_delay_bufL, w_pos_l, pre_delay_samples, block_size);

            next_w_pos = w_pos_l;

            Avx2Utils::ScaleBufferAVX2(temp_wet_in, temp_wet_in, block_size, 0.2f);
            Avx2Utils::FillBufferAVX2(temp_accum, block_size, 0.0f);

            for (int32_t k = 0; k < ReverbState::NUM_COMBS; ++k) {
                state->combsL[k].process_block(temp_comb_out, temp_wet_in, block_size);
                Avx2Utils::AccumulateAVX2(temp_accum, temp_comb_out, block_size);
            }

            for (int32_t k = 0; k < ReverbState::NUM_ALLPASS; ++k) {
                state->allpassL[k].process_block(temp_accum, block_size);
            }

            Avx2Utils::MixAudioAVX2(temp_accum, p_dry_l, block_size, mix, 1.0f - mix, 1.0f);
            Avx2Utils::CopyBufferAVX2(p_dry_l, temp_accum, block_size);
        }
        {
            int32_t w_pos_r = current_w_pos;
            ProcessPreDelayBlock(temp_wet_in, p_dry_r, state->pre_delay_bufR, w_pos_r, pre_delay_samples, block_size);
            Avx2Utils::ScaleBufferAVX2(temp_wet_in, temp_wet_in, block_size, 0.2f);
            Avx2Utils::FillBufferAVX2(temp_accum, block_size, 0.0f);

            for (int32_t k = 0; k < ReverbState::NUM_COMBS; ++k) {
                state->combsR[k].process_block(temp_comb_out, temp_wet_in, block_size);
                Avx2Utils::AccumulateAVX2(temp_accum, temp_comb_out, block_size);
            }

            for (int32_t k = 0; k < ReverbState::NUM_ALLPASS; ++k) {
                state->allpassR[k].process_block(temp_accum, block_size);
            }

            Avx2Utils::MixAudioAVX2(temp_accum, p_dry_r, block_size, mix, 1.0f - mix, 1.0f);
            Avx2Utils::CopyBufferAVX2(p_dry_r, temp_accum, block_size);
        }
        state->pre_delay_write_pos = next_w_pos;
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_reverb = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_reverb,
    nullptr,
    func_proc_audio_reverb
};
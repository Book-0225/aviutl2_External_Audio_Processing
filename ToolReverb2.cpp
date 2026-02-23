#include "Eap2Common.h"
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <cmath>
#include "Avx2Utils.h"

constexpr auto TOOL_NAME = L"Reverb";

FILTER_ITEM_TRACK rev_decay(L"Decay", 85.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK rev_size(L"Size", 100.0, 10.0, 250.0, 1.0);
FILTER_ITEM_TRACK rev_diffusion(L"Diffusion", 70.0, 0.0, 100.0, 1.0);
FILTER_ITEM_TRACK rev_highcut(L"High Cut", 18.0, 1.0, 20.0, 0.1);
FILTER_ITEM_TRACK rev_predelay2(L"Pre-Delay", 20.0, 0.0, 200.0, 1.0);
FILTER_ITEM_TRACK rev_mod_depth(L"Modulation", 40.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK rev_mix2(L"Mix", 40.0, 0.0, 100.0, 0.1);

void* filter_items_reverb2[] = {
    &rev_decay,
    &rev_size,
    &rev_diffusion,
    &rev_highcut,
    &rev_predelay2,
    &rev_mod_depth,
    &rev_mix2,
    nullptr
};

const int32_t MAX_BUFFER_SIZE = 96000 * 4;
const int32_t PROCESS_BLOCK_SIZE = 64;

float Lerp(float a, float b, float t) {
    return a + t * (b - a);
}

class OnePoleLPF {
public:
    float store = 0.0f;
    float a0 = 1.0f;
    float b1 = 0.0f;

    void set_cutoff(float cutoff, float sample_rate) {
        if (cutoff >= sample_rate * 0.49f) {
            a0 = 1.0f; b1 = 0.0f;
            return;
        }
        float costh = 2.0f - cosf(2.0f * 3.14159f * cutoff / sample_rate);
        b1 = costh - sqrtf(costh * costh - 1.0f);
        a0 = 1.0f - b1;
    }

    void set_damping(float damping) {
        b1 = damping;
        a0 = 1.0f - b1;
    }

    inline float process(float in) {
        store = in * a0 + store * b1;
        return store;
    }
};

class ModDelayLine {
public:
    std::vector<float> buffer;
    int32_t size = 0;
    int32_t w_pos = 0;

    void init(int32_t length) {
        size = length;
        buffer.assign(static_cast<int64_t>(size) + 1, 0.0f);
        w_pos = 0;
    }

    inline void write(float in) {
        buffer[w_pos] = in;
        w_pos++;
        if (w_pos >= size) w_pos = 0;
    }

    inline float read_interpolated(float delay_samples) {
        float r_pos_f = w_pos - delay_samples;
        while (r_pos_f < 0.0f) r_pos_f += static_cast<float>(size);
        while (r_pos_f >= static_cast<float>(size)) r_pos_f -= static_cast<float>(size);
        int32_t r_idx = static_cast<int32_t>(r_pos_f);
        float frac = r_pos_f - r_idx;
        int32_t r_idx_next = r_idx + 1;
        if (r_idx_next >= size) r_idx_next = 0;
        return Lerp(buffer[r_idx], buffer[r_idx_next], frac);
    }

    void clear() {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        w_pos = 0;
    }

    inline float read_at_future_wpos(int32_t future_offset, float delay_samples) {
        float r_pos_f = (w_pos + future_offset) - delay_samples;
        while (r_pos_f < 0.0f) r_pos_f += size;
        while (r_pos_f >= (float)size) r_pos_f -= size;
        int32_t r_idx = (int32_t)r_pos_f;
        float frac = r_pos_f - r_idx;
        int32_t r_next = (r_idx + 1 >= size) ? 0 : r_idx + 1;
        return Lerp(buffer[r_idx], buffer[r_next], frac);
    }

    void bulk_write(const float* src, int32_t count) {
        for (int i = 0; i < count; ++i) {
            buffer[w_pos++] = src[i];
            if (w_pos >= size) w_pos = 0;
        }
    }
};

class VariableAllPass {
public:
    ModDelayLine delay;
    float feedback = 0.5f;

    void init(int32_t length) {
        delay.init(length);
    }

    inline float process_scaled(float in, float base_delay, float size_scale) {
        float current_delay_len = base_delay * size_scale;
        if (current_delay_len > static_cast<float>(delay.size - 2)) current_delay_len = static_cast<float>(delay.size - 2);
        float buf_out = delay.read_interpolated(current_delay_len);
        float vn = in + (buf_out * feedback);
        delay.write(vn);
        return buf_out - (vn * feedback);
    }

    void clear() {
        delay.clear();
    }
};

struct ReverbState2 {
    VariableAllPass diffusers[4];
    const int32_t diff_tunings[4] = { 142, 107, 379, 277 };
    ModDelayLine delayL, delayR;
    OnePoleLPF dampL, dampR;
    VariableAllPass tankAP_L, tankAP_R;
    ModDelayLine postDelayL, postDelayR;
    const float tL_d1_base = 672.0f;
    const float tL_ap_base = 1800.0f;
    const float tL_d2_base = 4453.0f;
    const float tR_d1_base = 908.0f;
    const float tR_ap_base = 2656.0f;
    const float tR_d2_base = 4217.0f;
    const float tapL_base[7] = { 266, 2974, 1913, 1996, 1990, 187, 1066 };
    const float tapR_base[7] = { 353, 3627, 1228, 2673, 2111, 335, 121 };
    std::vector<float> pre_delay_buf;
    int32_t pre_delay_w = 0;
    float lfo_phase = 0.0f;
    float lfo_inc = 0.0f;
    OnePoleLPF inputLPF;
    bool initialized = false;
    int64_t last_sample_index = -1;
    double current_sr = 44100.0;

    void init(double sample_rate) {
        current_sr = sample_rate;
        double scale = sample_rate / 44100.0;
        double max_size_mult = 3.0;
        for (int i = 0; i < 4; ++i) diffusers[i].init(static_cast<int32_t>((diff_tunings[i] * scale * 2.0)));
        delayL.init(static_cast<int32_t>((tL_d1_base * scale * max_size_mult) + 1024));
        delayR.init(static_cast<int32_t>((tR_d1_base * scale * max_size_mult) + 1024));
        tankAP_L.init(static_cast<int32_t>((tL_ap_base * scale * max_size_mult)));
        tankAP_R.init(static_cast<int32_t>((tR_ap_base * scale * max_size_mult)));
        postDelayL.init(static_cast<int32_t>((tL_d2_base * scale * max_size_mult)));
        postDelayR.init(static_cast<int32_t>((tR_d2_base * scale * max_size_mult)));
        pre_delay_buf.assign(MAX_BUFFER_SIZE, 0.0f);
        pre_delay_w = 0;
        lfo_phase = 0.0f;
        lfo_inc = static_cast<float>(2.0 * M_PI * 1.0 / sample_rate);
        initialized = true;
    }

    void clear() {
        if (!initialized) return;
        for (auto& d : diffusers) d.clear();
        delayL.clear(); delayR.clear();
        tankAP_L.clear(); tankAP_R.clear();
        postDelayL.clear(); postDelayR.clear();
        Avx2Utils::FillBufferAVX2(pre_delay_buf.data(), pre_delay_buf.size(), 0.0f);
        dampL.store = 0.0f; dampR.store = 0.0f;
        inputLPF.store = 0.0f;
    }

    void process_diffusers_block(float* io, int32_t count, float total_scale, float diff_scale, float g) {
        alignas(32) float del[4][PROCESS_BLOCK_SIZE];
        alignas(32) float s_buf[PROCESS_BLOCK_SIZE];
        for (int k = 0; k < 4; ++k) {
            float dlen = diff_tunings[k] * total_scale * diff_scale;
            for (int n = 0; n < count; ++n)
                del[k][n] = diffusers[k].delay.read_at_future_wpos(n, dlen);
        }
        const __m256 vg = _mm256_set1_ps(g);
        for (int k = 0; k < 4; ++k) {
            int n = 0;
            for (; n + 8 <= count; n += 8) {
                __m256 x_v = _mm256_load_ps(io + n);
                __m256 d_v = _mm256_load_ps(del[k] + n);
                __m256 s_v = _mm256_fmadd_ps(vg, d_v, x_v);
                __m256 y_v = _mm256_fnmadd_ps(vg, s_v, d_v);
                _mm256_store_ps(s_buf + n, s_v);
                _mm256_store_ps(io + n, y_v);
            }
            for (; n < count; ++n) {
                float s = io[n] + g * del[k][n];
                io[n] = del[k][n] - g * s;
                s_buf[n] = s;
            }
            diffusers[k].delay.bulk_write(s_buf, count);
        }
    }

    void process_block(float* outL, float* outR, const float* inL, const float* inR, int32_t count, float decay, float size_percent, float diffusion, float highcut_khz, float predelay_ms, float mod_depth) {
        float decay_val = decay;
        float size_scale = size_percent / 100.0f;
        float total_scale = static_cast<float>(current_sr / 44100.0);
        float diff_fb_in = 0.75f * (diffusion / 100.0f);
        float diff_fb_tank = 0.6f * (diffusion / 100.0f);
        float damp_val = 0.02f + (1.0f - decay_val) * 0.5f;
        inputLPF.set_cutoff(highcut_khz * 1000.0f, static_cast<float>(current_sr));
        dampL.set_damping(damp_val);
        dampR.set_damping(damp_val);
        for (int i = 0; i < 4; ++i) diffusers[i].feedback = diff_fb_in;
        tankAP_L.feedback = -diff_fb_tank;
        tankAP_R.feedback = -diff_fb_tank;
        int32_t pre_samps = static_cast<int32_t>((predelay_ms * 0.001f * current_sr));
        if (pre_samps >= MAX_BUFFER_SIZE) pre_samps = MAX_BUFFER_SIZE - 1;
        int32_t pre_r_idx = pre_delay_w - pre_samps;
        if (pre_r_idx < 0) pre_r_idx += MAX_BUFFER_SIZE;
        int32_t pd_size = static_cast<int32_t>(pre_delay_buf.size());
        float mod_amp = mod_depth * 15.0f;

        alignas(32) float diff_io[PROCESS_BLOCK_SIZE];
        float diff_scale = 1.0f + (size_scale - 1.0f) * 0.25f;
        for (int i = 0; i < count; ++i) {
            float mono = (inL[i] + inR[i]) * 0.5f;
            pre_delay_buf[pre_delay_w] = mono;
            float s = pre_delay_buf[pre_r_idx];
            if (++pre_delay_w >= pd_size) pre_delay_w = 0;
            if (++pre_r_idx >= pd_size) pre_r_idx = 0;
            diff_io[i] = inputLPF.process(s);
        }
        process_diffusers_block(diff_io, count, total_scale, diff_scale, diff_fb_in);


        for (int i = 0; i < count; ++i) {
            float mono_in = (inL[i] + inR[i]) * 0.5f;
            pre_delay_buf[pre_delay_w] = mono_in;
            float input_sample = diff_io[i];
            pre_delay_w++;
            if (pre_delay_w >= pd_size) pre_delay_w = 0;
            pre_r_idx++;
            if (pre_r_idx >= pd_size) pre_r_idx = 0;
            input_sample = inputLPF.process(input_sample);
            lfo_phase += lfo_inc;
            if (lfo_phase > 6.283185f) lfo_phase -= 6.283185f;
            float lfo_sin = sinf(lfo_phase);
            float lfo_cos = cosf(lfo_phase);
            float fbL = postDelayL.read_interpolated(tL_d2_base * total_scale * size_scale);
            float fbR = postDelayR.read_interpolated(tR_d2_base * total_scale * size_scale);
            float tank_in_L = input_sample + fbR * decay_val;
            float tank_in_R = input_sample + fbL * decay_val;
            float modL = lfo_sin * mod_amp;
            delayL.write(tank_in_L);
            float d1L = delayL.read_interpolated((tL_d1_base * total_scale * size_scale) + modL);
            float dampL_out = dampL.process(d1L);
            float apL_out = tankAP_L.process_scaled(dampL_out, tL_ap_base * total_scale, size_scale);
            postDelayL.write(apL_out);
            float modR = lfo_cos * mod_amp;
            delayR.write(tank_in_R);
            float d1R = delayR.read_interpolated((tR_d1_base * total_scale * size_scale) + modR);
            float dampR_out = dampR.process(d1R);
            float apR_out = tankAP_R.process_scaled(dampR_out, tR_ap_base * total_scale, size_scale);
            postDelayR.write(apR_out);
            float accL = 0.0f;
            float accR = 0.0f;
            accL += delayL.read_interpolated(tapL_base[0] * total_scale * size_scale);
            accL += delayL.read_interpolated(tapL_base[1] * total_scale * size_scale);
            accL -= tankAP_L.delay.read_interpolated(tapL_base[2] * total_scale * size_scale);
            accL += postDelayL.read_interpolated(tapL_base[3] * total_scale * size_scale);
            accL -= delayR.read_interpolated(tapL_base[4] * total_scale * size_scale);
            accL -= tankAP_R.delay.read_interpolated(tapL_base[5] * total_scale * size_scale);
            accL -= postDelayR.read_interpolated(tapL_base[6] * total_scale * size_scale);
            accR += delayR.read_interpolated(tapR_base[0] * total_scale * size_scale);
            accR += delayR.read_interpolated(tapR_base[1] * total_scale * size_scale);
            accR -= tankAP_R.delay.read_interpolated(tapR_base[2] * total_scale * size_scale);
            accR += postDelayR.read_interpolated(tapR_base[3] * total_scale * size_scale);
            accR -= delayL.read_interpolated(tapR_base[4] * total_scale * size_scale);
            accR -= tankAP_L.delay.read_interpolated(tapR_base[5] * total_scale * size_scale);
            accR -= postDelayL.read_interpolated(tapR_base[6] * total_scale * size_scale);
            outL[i] = accL * 0.6f;
            outR[i] = accR * 0.6f;
        }
    }
};

static std::mutex g_rev_state_mutex;
static std::map<const void*, ReverbState2> g_rev_states;

bool func_proc_audio_reverb2(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);
    float p_decay_norm = static_cast<float>(rev_decay.value / 100.0);
    p_decay_norm *= p_decay_norm;
    float p_decay = 0.30f + (p_decay_norm * 0.63f);
    float p_size = static_cast<float>(rev_size.value);
    float p_diffusion = static_cast<float>(rev_diffusion.value);
    float p_highcut = static_cast<float>(rev_highcut.value);
    float p_predelay = static_cast<float>(rev_predelay2.value);
    float p_mod = static_cast<float>(rev_mod_depth.value / 100.0);
    float p_mix = static_cast<float>(rev_mix2.value / 100.0);

    if (p_mix <= 0.0f) return true;

    ReverbState2* state = nullptr;
    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;

    {
        std::lock_guard<std::mutex> lock(g_rev_state_mutex);
        state = &g_rev_states[audio->object];

        if (!state->initialized || state->current_sr != Fs) state->init(Fs);
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->clear();
        }
        state->last_sample_index = audio->object->sample_index;
    }

    std::vector<float> bufL, bufR;
    if (bufL.size() < (size_t)total_samples) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    alignas(32) float wetL[PROCESS_BLOCK_SIZE];
    alignas(32) float wetR[PROCESS_BLOCK_SIZE];

    for (int32_t i = 0; i < total_samples; i += PROCESS_BLOCK_SIZE) {
        int32_t block_size = (std::min)(PROCESS_BLOCK_SIZE, total_samples - i);
        float* p_dry_l = bufL.data() + i;
        float* p_dry_r = bufR.data() + i;
        state->process_block(wetL, wetR, p_dry_l, p_dry_r, block_size, p_decay, p_size, p_diffusion, p_highcut, p_predelay, p_mod);
        Avx2Utils::MixAudioAVX2(wetL, p_dry_l, block_size, p_mix, 1.0f - p_mix, 1.0f);
        Avx2Utils::CopyBufferAVX2(p_dry_l, wetL, block_size);
        Avx2Utils::MixAudioAVX2(wetR, p_dry_r, block_size, p_mix, 1.0f - p_mix, 1.0f);
        Avx2Utils::CopyBufferAVX2(p_dry_r, wetR, block_size);
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

void CleanupReverbResources2() {
    g_rev_states.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_reverb2 = {
    TYPE_AUDIO_FILTER_OBJECT,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_reverb2,
    nullptr,
    func_proc_audio_reverb2
};
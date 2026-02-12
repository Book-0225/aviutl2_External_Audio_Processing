#pragma once
#include "Eap2Common.h"
#include <cmath>
#include <array>
#include <algorithm>

static const int32_t BLOCK_SIZE = 64;

enum GenType {
    GEN_SINE = 0,
    GEN_SQUARE,
    GEN_TRIANGLE,
    GEN_SAW,
    GEN_NOISE,
    GEN_PINK,
    GEN_KARPLUS,
    GEN_FM,
    GEN_PIANO,
    GEN_MUSICBOX,
    GEN_8BIT,
    GEN_KICK,
    GEN_SUPERSAW
};

static FILTER_ITEM_SELECT::ITEM gen_type_list[] = {
    { L"[Basic] Sine", GEN_SINE },
    { L"[Basic] Square", GEN_SQUARE },
    { L"[Basic] Triangle", GEN_TRIANGLE },
    { L"[Basic] Saw", GEN_SAW },
    { L"[Noise] White", GEN_NOISE },
    { L"[Noise] Pink", GEN_PINK },
    { L"[Phys] Karplus", GEN_KARPLUS },
    { L"[Digi] FM Basic", GEN_FM },
    { L"[Inst] Grand Piano", GEN_PIANO },
    { L"[Inst] Music Box", GEN_MUSICBOX },
    { L"[Retro] 8-bit Pulse", GEN_8BIT },
    { L"[Drum] Kick", GEN_KICK },
    { L"[Syn] Super Saw", GEN_SUPERSAW },
    { nullptr }
};

struct FastRNG {
    uint32_t state = 123456789;
    inline uint32_t next() {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return state = x;
    }
    inline float nextFloat() {
        return (next() * (2.0f / 4294967296.0f)) - 1.0f;
    }
};

struct StereoSample {
    double l;
    double r;

    StereoSample operator*(float a) const { return { l * a, r * a }; }
    StereoSample operator+(const StereoSample& b) const { return { l + b.l, r + b.r }; }
    void operator+=(const StereoSample& b) { l += b.l; r += b.r; }
    StereoSample operator/(float c) const { return { l / c, r / c }; }
};

inline double poly_blep(double t, double dt) {
    if (t < dt) {
        t /= dt; return t + t - t * t - 1.0;
    }
    else if (t > 1.0 - dt) {
        t = (t - 1.0) / dt; return t * t + t + t + 1.0;
    }
    return 0.0;
}

struct SvfFilter {
    float ic1eq = 0, ic2eq = 0;

    void reset() {
        ic1eq = 0;
        ic2eq = 0;
    }

    StereoSample process(StereoSample in, float cutoff, float q) {
        if (cutoff > 0.49f) cutoff = 0.49f;
        if (cutoff < 0.001f) cutoff = 0.001f;
        if (q < 0.5f) q = 0.5f;
        double g = std::tan(M_PI * cutoff);
        double k = 1.0f / q;
        double a1 = 1.0f / (1.0f + g * (g + k));
        double a2 = g * a1;
        double a3 = g * a2;
        double v3_l = in.l - ic2eq;
        double v1_l = a1 * ic1eq + a2 * v3_l;
        double v2_l = ic2eq + a2 * ic1eq + a3 * v3_l;
        ic1eq = 2.0f * static_cast<float>(v1_l) - ic1eq;
        ic2eq = 2.0f * static_cast<float>(v2_l) - ic2eq;
        float out_l = static_cast<float>(v2_l);
        return { out_l, out_l };
    }

    float l_ic1 = 0, l_ic2 = 0;
    float r_ic1 = 0, r_ic2 = 0;

    StereoSample processStereo(StereoSample in, float cutoff, float q) {
        if (cutoff > 0.49f) cutoff = 0.49f; if (cutoff < 0.0001f) cutoff = 0.0001f;
        double g = std::tan(M_PI * cutoff);
        double k = 1.0f / q;
        double a1 = 1.0f / (1.0f + g * (g + k));
        double a2 = g * a1;
        double a3 = g * a2;
        double l_v3 = in.l - l_ic2;
        double l_v1 = a1 * l_ic1 + a2 * l_v3;
        double l_v2 = l_ic2 + a2 * l_ic1 + a3 * l_v3;
        l_ic1 = 2.0f * static_cast<float>(l_v1) - l_ic1;
        l_ic2 = 2.0f * static_cast<float>(l_v2) - l_ic2;
        double r_v3 = in.r - r_ic2;
        double r_v1 = a1 * r_ic1 + a2 * r_v3;
        double r_v2 = r_ic2 + a2 * r_ic1 + a3 * r_v3;
        r_ic1 = 2.0f * static_cast<float>(r_v1) - r_ic1;
        r_ic2 = 2.0f * static_cast<float>(r_v2)-r_ic2;
        return { static_cast<float>(l_v2), static_cast<float>(r_v2) };
    }
};

struct DelayLine {
    static const size_t MAX_DELAY = 4096;
    std::array<float, MAX_DELAY> buffer{};
    size_t write_ptr = 0;
    size_t length = 0;
    void set_length(size_t len) { length = (std::min)(len, MAX_DELAY); if (length < 2)length = 2; }
    void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); write_ptr = 0; }
    float process_karplus(float damping) {
        if (length == 0)return 0;
        size_t r = (write_ptr + 1) % length;
        float out = (buffer[write_ptr] + buffer[r]) * 0.5f * damping;
        buffer[write_ptr] = out; write_ptr++; if (write_ptr >= length)write_ptr = 0;
        return buffer[r];
    }
    void fill_noise(FastRNG& rng) { for (size_t i = 0; i < length; ++i) buffer[i] = rng.nextFloat(); }
};

struct VoiceState {
    double phase = 0.0;
    double phase_inc = 0.0;
    double phase_L = 0.0;
    double phase_R = 0.0;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    DelayLine string_delay;
    bool string_plucked = false;
    bool active = false;
    int32_t noteNumber = -1;
    float velocity = 0.0f;
    double noteOnTime = 0.0;
    double noteOffTime = -1.0;
    float pan = 0.5f;
    float volume = 1.0f;
    float modWheel = 0.0f;
    FastRNG rng;
    SvfFilter filter;

    void init() {
        phase = 0.0; phase_L = 0.0; phase_R = 0.0;
        phase_inc = 0.0;
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0;
        string_delay.clear();
        string_plucked = false;
        active = false;
        filter.reset();
        pan = 0.5f;
        volume = 1.0f;
        modWheel = 0.0f;
    }

    void reset_filter() {
        filter.reset();
    }
};

struct SynthParams {
    int32_t type;
    float freq;
    double attack;
    double decay;
    float sustain;
    double release;
    float timbre;
    float filter_cutoff;
    float filter_res;
    float detune;
};

inline float CalculateEnvelope(double time, double duration, double attack, double decay, float sustain_level, double release) {
    if (time > duration) {
        double release_time = time - duration;
        if (release_time >= release) return 0.0f;
        float base_level = sustain_level;
        if (duration < attack) base_level = (attack > 1e-5) ? static_cast<float>(duration / attack) : 1.0f;
        else if (duration < attack + decay) base_level = 1.0f - static_cast<float>((duration - attack) / decay) * (1.0f - sustain_level);
        return base_level * (1.0f - static_cast<float>(release_time / release));
    }
    if (time < attack) return (attack > 1e-5) ? static_cast<float>(time / attack) : 1.0f;
    else if (time < attack + decay) return 1.0f - static_cast<float>((time - attack) / decay) * (1.0f - sustain_level);
    return sustain_level;
}

inline StereoSample GenerateSampleStereo(VoiceState& state, const SynthParams& p, double time, double sampleRate, double duration) {
    double current_freq = p.freq;
    if (p.type == GEN_KICK) {
        double pitch_env = std::exp(-time * 20.0);
        current_freq = p.freq * (0.2 + 3.0 * pitch_env);
    }
    if (state.modWheel > 0.01f) {
        double vib_freq = 6.0;
        double vib_depth = state.modWheel * 0.03;
        current_freq *= (1.0 + std::sin(time * M_PI * 2 * vib_freq) * vib_depth);
    }
    state.phase_inc = current_freq / sampleRate;
    if (p.type == GEN_KARPLUS && !state.string_plucked) {
        size_t delay_len = static_cast<size_t>(sampleRate / p.freq);
        state.string_delay.set_length(delay_len);
        state.string_delay.fill_noise(state.rng);
        state.string_plucked = true;
    }
    StereoSample sample = { 0.0f, 0.0f };
    double t = state.phase;
    double dt = state.phase_inc;
    switch (p.type) {
    case GEN_SINE: {
        float v = static_cast<float>(std::sin(t * M_PI * 2));
        sample = { v, v };
        break;
    }
    case GEN_SQUARE: {
        float v = (t < 0.5) ? 1.0f : -1.0f;
        v += static_cast<float>(poly_blep(t, dt));
        v -= static_cast<float>(poly_blep(std::fmod(t + 0.5, 1.0), dt));
        sample = { v, v };
        break;
    }
    case GEN_TRIANGLE: {
        float v = static_cast<float>(-1.0f + (2.0f * t));
        v = 2.0f * (std::abs(v) - 0.5f);
        v *= 2.0f;
        sample = { v, v };
        break;
    }
    case GEN_SAW: {
        float v = static_cast<float>(1.0f - 2.0f * t);
        v -= static_cast<float>(poly_blep(t, dt));
        sample = { v, v };
        break;
    }
    case GEN_NOISE: {
        float v = state.rng.nextFloat();
        sample = { v, v };
        break;
    }
    case GEN_PINK: {
        float w = state.rng.nextFloat();
        state.b0 = 0.99886f * state.b0 + w * 0.0555179f;
        state.b1 = 0.99332f * state.b1 + w * 0.0750759f;
        state.b2 = 0.96900f * state.b2 + w * 0.1538520f;
        state.b3 = 0.86650f * state.b3 + w * 0.3104856f;
        state.b4 = 0.55000f * state.b4 + w * 0.5329522f;
        state.b5 = -0.7616f * state.b5 - w * 0.0168980f;
        float v = (state.b0 + state.b1 + state.b2 + state.b3 + state.b4 + state.b5 + state.b6 + w * 0.5362f) * 0.11f;
        sample = { v, v };
        state.b6 = w * 0.115926f;
        break;
    }
    case GEN_KARPLUS: {
        float damping = 0.98f + (0.019f * p.timbre);
        float v = state.string_delay.process_karplus(damping);
        sample = { v, v };
        break;
    }
    case GEN_FM: {
        double idx = 2.0 + (p.timbre * 10.0);
        double mod = std::sin(t * M_PI * 4.0);
        float v = static_cast<float>(std::sin(t * M_PI * 2 + mod * idx));
        sample = { v, v };
        break;
    }
    case GEN_SUPERSAW: {
        float vC = static_cast<float>(1.0f - 2.0f * t);
        vC -= static_cast<float>(poly_blep(t, dt));
        double detune = 1.002 + (p.detune * 0.02);
        state.phase_L += state.phase_inc * (1.0 / detune);
        state.phase_L -= std::floor(state.phase_L);
        float vL = static_cast<float>(1.0f - 2.0f * state.phase_L);
        vL -= static_cast<float>(poly_blep(state.phase_L, state.phase_inc / detune));
        state.phase_R += state.phase_inc * detune;
        state.phase_R -= std::floor(state.phase_R);
        float vR = static_cast<float>(1.0f - 2.0f * state.phase_R);
        vR -= static_cast<float>(poly_blep(state.phase_R, state.phase_inc * detune));
        sample.l = (vC * 0.5f + vL * 0.5f) * 0.7f;
        sample.r = (vC * 0.5f + vR * 0.5f) * 0.7f;
        break;
    }
    default: {
        float v = static_cast<float>(std::sin(t * M_PI * 2));
        sample = { v, v };
        break;
    }
    }
    state.phase += state.phase_inc;
    state.phase -= std::floor(state.phase);
    float cutoff_hz = 20.0f * std::pow(1000.0f, p.filter_cutoff);
    float norm_cutoff = static_cast<float>(cutoff_hz / sampleRate);
    float resonance = 0.5f + p.filter_res * 15.0f;
    sample = state.filter.processStereo(sample, norm_cutoff, resonance);
    if (p.type != GEN_KICK) {
        float env = CalculateEnvelope(time, duration, p.attack, p.decay, p.sustain, p.release);
        sample = sample * env;
    }
    double angle = state.pan * (M_PI / 2.0f);
    double pan_l = std::cos(angle);
    double pan_r = std::sin(angle);
    sample.l *= pan_l * state.volume;
    sample.r *= pan_r * state.volume;
    return sample / 2;
}
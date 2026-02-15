#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include "Avx2Utils.h"

constexpr auto TOOL_NAME = L"EQ";

FILTER_ITEM_GROUP cut_group(L"Cut Filters", false);
FILTER_ITEM_TRACK eq_hpf(L"Low Cut", 0.0, 0.0, 2000.0, 1.0);
FILTER_ITEM_TRACK eq_lpf(L"High Cut", 20000.0, 500.0, 20000.0, 1.0);
FILTER_ITEM_GROUP eq_group(L"EQ Bands", true);
FILTER_ITEM_TRACK eq_low(L"Low Gain", 0.0, -20.0, 20.0, 0.1);
FILTER_ITEM_TRACK eq_ml(L"M-Low Gain", 0.0, -20.0, 20.0, 0.1);
FILTER_ITEM_TRACK eq_mid(L"Mid Gain", 0.0, -20.0, 20.0, 0.1);
FILTER_ITEM_TRACK eq_mh(L"M-High Gain", 0.0, -20.0, 20.0, 0.1);
FILTER_ITEM_TRACK eq_high(L"High Gain", 0.0, -20.0, 20.0, 0.1);
FILTER_ITEM_TRACK eq_low_freq(L"Low Freq", 100.0, 20.0, 1000.0, 1.0);
FILTER_ITEM_TRACK eq_ml_freq(L"M-Low Freq", 350.0, 100.0, 5000.0, 1.0);
FILTER_ITEM_TRACK eq_mid_freq(L"Mid Freq", 1000.0, 200.0, 10000.0, 1.0);
FILTER_ITEM_TRACK eq_mh_freq(L"M-High Freq", 3500.0, 1000.0, 20000.0, 1.0);
FILTER_ITEM_TRACK eq_high_freq(L"High Freq", 10000.0, 2000.0, 20000.0, 1.0);

void* filter_items_eq[] = {
    &cut_group,
    &eq_hpf,
    &eq_lpf,
    &eq_group,
    &eq_low,
    &eq_ml,
    &eq_mid,
    &eq_mh,
    &eq_high,
    &eq_low_freq,
    &eq_ml_freq,
    &eq_mid_freq,
    &eq_mh_freq,
    &eq_high_freq,
    nullptr
};

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    void setPassThrough() {
        b0 = 1.0; b1 = 0.0; b2 = 0.0; a1 = 0.0; a2 = 0.0;
    }

    void resetState() {
        x1 = 0.0; x2 = 0.0; y1 = 0.0; y2 = 0.0;
    }

    inline float process(float in) {
        double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        if (fabs(out) < 1.0e-15) out = 0.0;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        return static_cast<float>(out);
    }

    void copyCoeffsFrom(const Biquad& other) {
        b0 = other.b0;
        b1 = other.b1;
        b2 = other.b2;
        a1 = other.a1;
        a2 = other.a2;
    }

    void calcHPF(double Fs, double f0) {
        if (f0 <= 0.0) { setPassThrough(); return; }
        double w0 = 2.0 * M_PI * f0 / Fs;
        double alpha = sin(w0) / (2.0 * 0.7071);
        double cosw0 = cos(w0);
        double a0 = 1.0 + alpha;
        b0 = (1.0 + cosw0) / 2.0 / a0;
        b1 = -(1.0 + cosw0) / a0;
        b2 = (1.0 + cosw0) / 2.0 / a0;
        a1 = -2.0 * cosw0 / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void calcLPF(double Fs, double f0) {
        if (f0 >= Fs * 0.49) { setPassThrough(); return; }
        double w0 = 2.0 * M_PI * f0 / Fs;
        double alpha = sin(w0) / (2.0 * 0.7071);
        double cosw0 = cos(w0);
        double a0 = 1.0 + alpha;
        b0 = (1.0 - cosw0) / 2.0 / a0;
        b1 = (1.0 - cosw0) / a0;
        b2 = (1.0 - cosw0) / 2.0 / a0;
        a1 = -2.0 * cosw0 / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void calcLowShelf(double Fs, double f0, double dB_gain) {
        if (fabs(dB_gain) < 0.001) { setPassThrough(); return; }
        double A = pow(10.0, dB_gain / 40.0);
        double w0 = 2.0 * M_PI * f0 / Fs;
        double alpha = sin(w0) / 2.0 * sqrt((A + 1 / A) * (1 / 0.707 - 1) + 2);
        double cosw0 = cos(w0);
        double a0 = (A + 1) + (A - 1) * cosw0 + 2 * sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * cosw0 + 2 * sqrt(A) * alpha) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * cosw0) / a0;
        b2 = A * ((A + 1) - (A - 1) * cosw0 - 2 * sqrt(A) * alpha) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * cosw0) / a0;
        a2 = ((A + 1) + (A - 1) * cosw0 - 2 * sqrt(A) * alpha) / a0;
    }

    void calcHighShelf(double Fs, double f0, double dB_gain) {
        if (fabs(dB_gain) < 0.001) { setPassThrough(); return; }
        double A = pow(10.0, dB_gain / 40.0);
        double w0 = 2.0 * M_PI * f0 / Fs;
        double alpha = sin(w0) / 2.0 * sqrt((A + 1 / A) * (1 / 0.707 - 1) + 2);
        double cosw0 = cos(w0);
        double a0 = (A + 1) - (A - 1) * cosw0 + 2 * sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * cosw0 + 2 * sqrt(A) * alpha) / a0;
        b1 = -2 * A * ((A - 1) + (A + 1) * cosw0) / a0;
        b2 = A * ((A + 1) + (A - 1) * cosw0 - 2 * sqrt(A) * alpha) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * cosw0) / a0;
        a2 = ((A + 1) - (A - 1) * cosw0 - 2 * sqrt(A) * alpha) / a0;
    }

    void calcPeaking(double Fs, double f0, double dB_gain, double Q) {
        if (fabs(dB_gain) < 0.001) { setPassThrough(); return; }
        double A = pow(10.0, dB_gain / 40.0);
        double w0 = 2.0 * M_PI * f0 / Fs;
        double alpha = sin(w0) / (2.0 * Q);
        double cosw0 = cos(w0);
        double a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0;
        b1 = (-2.0 * cosw0) / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = (-2.0 * cosw0) / a0;
        a2 = (1.0 - alpha / A) / a0;
    }
};

static const int32_t FILTER_STAGES = 7;

struct EQState {
    Biquad filtersL[FILTER_STAGES];
    Biquad filtersR[FILTER_STAGES];
    int64_t last_sample_index = -1;
};

static std::mutex g_eq_state_mutex;
static std::map<const void*, EQState> g_eq_states;
const int32_t BLOCK_SIZE = 256;

bool func_proc_audio_eq(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    double val_hpf = eq_hpf.value;
    double val_lpf = eq_lpf.value;
    double val_low = eq_low.value;
    double val_ml = eq_ml.value;
    double val_mid = eq_mid.value;
    double val_mh = eq_mh.value;
    double val_high = eq_high.value;
    double val_low_freq = eq_low_freq.value;
    double val_ml_freq = eq_ml_freq.value;
    double val_mid_freq = eq_mid_freq.value;
    double val_mh_freq = eq_mh_freq.value;
    double val_high_freq = eq_high_freq.value;

    if (val_hpf == 0.0 && val_lpf == 20000.0 &&
        val_low == 0.0 && val_ml == 0.0 && val_mid == 0.0 &&
        val_mh == 0.0 && val_high == 0.0) {
        return true;
    }

    EQState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_eq_state_mutex);
        state = &g_eq_states[audio->object];
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            for (int32_t i = 0; i < FILTER_STAGES; ++i) {
                state->filtersL[i].resetState();
                state->filtersR[i].resetState();
            }
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;

    state->filtersL[0].calcHPF(Fs, val_hpf);
    state->filtersR[0].copyCoeffsFrom(state->filtersL[0]);
    state->filtersL[1].calcLPF(Fs, val_lpf);
    state->filtersR[1].copyCoeffsFrom(state->filtersL[1]);
    state->filtersL[2].calcLowShelf(Fs, val_low_freq, val_low);
    state->filtersR[2].copyCoeffsFrom(state->filtersL[2]);
    state->filtersL[3].calcPeaking(Fs, val_ml_freq, val_ml, 1.0);
    state->filtersR[3].copyCoeffsFrom(state->filtersL[3]);
    state->filtersL[4].calcPeaking(Fs, val_mid_freq, val_mid, 1.0);
    state->filtersR[4].copyCoeffsFrom(state->filtersL[4]);
    state->filtersL[5].calcPeaking(Fs, val_mh_freq, val_mh, 1.0);
    state->filtersR[5].copyCoeffsFrom(state->filtersL[5]);
    state->filtersL[6].calcHighShelf(Fs, val_high_freq, val_high);
    state->filtersR[6].copyCoeffsFrom(state->filtersL[6]);

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* pL = bufL.data() + i;
        float* pR = bufR.data() + i;

        for (int32_t k = 0; k < block_count; ++k) {
            float l = pL[k];
            float r = pR[k];

            for (int32_t s = 0; s < FILTER_STAGES; ++s) {
                l = state->filtersL[s].process(l);
                r = state->filtersR[s].process(r);
            }
            pL[k] = l;
            pR[k] = r;
        }
    }

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

void CleanupEQResources() {
    g_eq_states.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_eq = {
    TYPE_AUDIO_FILTER_OBJECT,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_eq,
    nullptr,
    func_proc_audio_eq
};
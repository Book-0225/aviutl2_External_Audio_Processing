#include "Avx2Utils.h"
#include "Eap2Common.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

constexpr auto TOOL_NAME = L"Pitch Shift";

static FILTER_ITEM_SELECT::ITEM ps_algo_list[] = {
    { L"グラニュラー", 0 },
    { L"位相ボコーダ", 1 },
    { nullptr }
};
FILTER_ITEM_SELECT ps_algo(L"アルゴリズム", 0, ps_algo_list);
FILTER_ITEM_TRACK ps_pitch(L"Pitch", 0.0, -24.0, 24.0, 0.01);
FILTER_ITEM_TRACK ps_speed(L"再生速度補正", 100.0, -2000.0, 2000.0, 0.01);
FILTER_ITEM_TRACK ps_mix(L"Mix", 100.0, 0.0, 100.0, 0.1);

void* filter_items_pitch_shift[] = {
    &ps_algo,
    &ps_pitch,
    &ps_speed,
    &ps_mix,
    nullptr
};

static constexpr int32_t BLOCK_SIZE = 64;

struct IPitchShiftState {
    int32_t algo_id = -1;
    int64_t last_sample_index = -1;

    virtual ~IPitchShiftState() = default;
    virtual void process(float* bufL, float* bufR, int32_t total_samples, float pitch_rate, float mix) = 0;
    virtual void clear() = 0;
};

struct GranularState : public IPitchShiftState {
    static constexpr int32_t WINDOW_SIZE = 4096;
    static constexpr int32_t MAX_BUF_SIZE = 96000;

    std::vector<float> bufferL, bufferR;
    int32_t write_pos = 0;
    double read_pos_a = 0.0;

    GranularState() {
        bufferL.assign(MAX_BUF_SIZE, 0.0f);
        bufferR.assign(MAX_BUF_SIZE, 0.0f);
    }

    void clear() override {
        std::fill(bufferL.begin(), bufferL.end(), 0.0f);
        std::fill(bufferR.begin(), bufferR.end(), 0.0f);
        write_pos = 0;
        read_pos_a = 0.0;
    }

    void process(float* dryL, float* dryR, int32_t total_samples, float pitch_rate, float mix) override {
        const double dt = (1.0 - static_cast<double>(pitch_rate)) / WINDOW_SIZE;
        const double win_size = static_cast<double>(WINDOW_SIZE);
        const int32_t buf_sz = MAX_BUF_SIZE;
        float* bL = bufferL.data();
        float* bR = bufferR.data();
        double r_pos_a = read_pos_a;
        int32_t w_pos = write_pos;

        alignas(32) float wet_L[BLOCK_SIZE];
        alignas(32) float wet_R[BLOCK_SIZE];

        for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
            const int32_t n = (std::min)(BLOCK_SIZE, total_samples - i);
            for (int32_t k = 0; k < n; ++k) {
                bL[w_pos] = dryL[i + k];
                bR[w_pos] = dryR[i + k];
                r_pos_a += dt;
                if (r_pos_a >= 1.0) r_pos_a -= 1.0;
                if (r_pos_a < 0.0) r_pos_a += 1.0;
                double r_pos_b = r_pos_a + 0.5;
                if (r_pos_b >= 1.0) r_pos_b -= 1.0;
                const float ga = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(M_PI * 2 * r_pos_a)));
                const float gb = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(M_PI * 2 * r_pos_b)));
                auto circ = [&](const float* buf, double idx) {
                    while (idx < 0.0) idx += buf_sz;
                    while (idx >= buf_sz) idx -= buf_sz;
                    const int32_t ii = static_cast<int32_t>(idx);
                    const float frac = static_cast<float>(idx - ii);
                    const int32_t ii1 = (ii + 1 < buf_sz) ? ii + 1 : 0;
                    return buf[ii] * (1.0f - frac) + buf[ii1] * frac;
                };
                double rda = w_pos - r_pos_a * win_size;
                double rdb = w_pos - r_pos_b * win_size;
                wet_L[k] = circ(bL, rda) * ga + circ(bL, rdb) * gb;
                wet_R[k] = circ(bR, rda) * ga + circ(bR, rdb) * gb;
                if (++w_pos >= buf_sz) w_pos = 0;
            }
            Avx2Utils::MixAudioAVX2(dryL + i, wet_L, n, 1.0f - mix, mix, 1.0f);
            Avx2Utils::MixAudioAVX2(dryR + i, wet_R, n, 1.0f - mix, mix, 1.0f);
        }
        write_pos = w_pos;
        read_pos_a = r_pos_a;
    }
};

static void fft_inplace(std::complex<float>* data, int32_t n, bool inverse) {
    for (int32_t i = 1, j = 0; i < n; ++i) {
        int32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (int32_t len = 2; len <= n; len <<= 1) {
        const float ang = static_cast<float>((inverse ? M_PI * 2 : M_PI * -2) / len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int32_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int32_t j = 0; j < (len >> 1); ++j) {
                auto u = data[i + j];
                auto v = data[i + j + (len >> 1)] * w;
                data[i + j] = u + v;
                data[i + j + (len >> 1)] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        const float inv_n = 1.0f / static_cast<float>(n);
        for (int32_t i = 0; i < n; ++i) data[i] *= inv_n;
    }
}

static inline float wrap_phase(float p) {
    while (p > M_PI) p -= static_cast<float>(M_PI * 2);
    while (p < -M_PI) p += static_cast<float>(M_PI * 2);
    return p;
}

struct PhaseVocoderState : public IPitchShiftState {
    static constexpr int32_t FFT_SIZE = 2048;
    static constexpr int32_t OVERLAP = 4;
    static constexpr int32_t HOP_SIZE = FFT_SIZE / OVERLAP;
    static constexpr int32_t NUM_BINS = FFT_SIZE / 2 + 1;
    static constexpr int32_t IN_SIZE = FFT_SIZE * 4;
    static constexpr int32_t OUT_SIZE = FFT_SIZE * 4;
    std::vector<float> in_L, in_R;
    int32_t in_write = 0;
    int32_t hop_counter = 0;
    std::vector<float> ana_phase_L, ana_phase_R;
    std::vector<float> syn_phase_L, syn_phase_R;
    std::vector<float> out_L, out_R;
    int32_t out_read = 0;
    int32_t out_write_pos = 0;
    int32_t out_available = 0;
    std::vector<float> hann;
    std::vector<std::complex<float>> fft_buf;
    std::vector<float> mag, ifreq, out_mag, out_ifreq;
    std::vector<int32_t> peak_owner;
    std::vector<float> pass1_syn_phase;
    std::vector<int32_t> peaks_buf;

    PhaseVocoderState() {
        init_buffers();
    }

    void init_buffers() {
        in_L.assign(IN_SIZE, 0.0f);
        in_R.assign(IN_SIZE, 0.0f);
        out_L.assign(OUT_SIZE, 0.0f);
        out_R.assign(OUT_SIZE, 0.0f);
        ana_phase_L.assign(NUM_BINS, 0.0f);
        ana_phase_R.assign(NUM_BINS, 0.0f);
        syn_phase_L.assign(NUM_BINS, 0.0f);
        syn_phase_R.assign(NUM_BINS, 0.0f);
        hann.resize(FFT_SIZE);
        for (int32_t i = 0; i < FFT_SIZE; ++i) hann[i] = 0.5f - 0.5f * static_cast<float>(std::cos(M_PI * 2 * i / FFT_SIZE));
        fft_buf.resize(FFT_SIZE);
        mag.resize(NUM_BINS);
        ifreq.resize(NUM_BINS);
        out_mag.resize(NUM_BINS);
        out_ifreq.resize(NUM_BINS);
        peak_owner.resize(NUM_BINS, 0);
        pass1_syn_phase.resize(NUM_BINS, 0.0f);
        peaks_buf.reserve(NUM_BINS / 4);
        hop_counter = 0;
        out_read = 0;
        out_write_pos = 0;
        out_available = 0;
    }

    void clear() override {
        init_buffers();
    }

    void process_frame(float pitch_rate) {
        if (static_cast<int32_t>(fft_buf.size()) != FFT_SIZE) {
            fft_buf.resize(FFT_SIZE);
            mag.resize(NUM_BINS);
            ifreq.resize(NUM_BINS);
            out_mag.resize(NUM_BINS);
            out_ifreq.resize(NUM_BINS);
            peak_owner.resize(NUM_BINS, 0);
            pass1_syn_phase.resize(NUM_BINS, 0.0f);
            peaks_buf.reserve(NUM_BINS / 4);
        }

        const float freq_per_bin = static_cast<float>(M_PI * 2 * HOP_SIZE / FFT_SIZE);
        const float ola_gain = 2.0f / 3.0f;

        auto do_channel = [&](const std::vector<float>& in_buf, std::vector<float>& ana_phase, std::vector<float>& syn_phase, std::vector<float>& out_buf) {
            const int32_t frame_start = (in_write - FFT_SIZE + IN_SIZE) % IN_SIZE;
            for (int32_t j = 0; j < FFT_SIZE; ++j) fft_buf[j] = { in_buf[(frame_start + j) % IN_SIZE] * hann[j], 0.0f };
            fft_inplace(fft_buf.data(), FFT_SIZE, false);
            for (int32_t k = 0; k < NUM_BINS; ++k) {
                const float phase = std::arg(fft_buf[k]);
                mag[k] = std::abs(fft_buf[k]);
                const float delta = wrap_phase(phase - ana_phase[k] - static_cast<float>(k) * freq_per_bin);
                ana_phase[k] = phase;
                ifreq[k] = static_cast<float>(k) + delta / freq_per_bin;
            }
            {
                peaks_buf.clear();
                if (mag[0] >= mag[1]) peaks_buf.push_back(0);
                for (int32_t k = 1; k < NUM_BINS - 1; ++k)
                    if (mag[k] >= mag[k - 1] && mag[k] >= mag[k + 1]) peaks_buf.push_back(k);
                if (mag[NUM_BINS - 1] >= mag[NUM_BINS - 2]) peaks_buf.push_back(NUM_BINS - 1);
                if (peaks_buf.empty()) peaks_buf.push_back(0);
                int32_t pi = 0;
                for (int32_t k = 0; k < NUM_BINS; ++k) {
                    while (pi + 1 < static_cast<int32_t>(peaks_buf.size()) && std::abs(k - peaks_buf[pi + 1]) < std::abs(k - peaks_buf[pi])) ++pi;
                    peak_owner[k] = peaks_buf[pi];
                }
            }
            std::fill(out_mag.begin(), out_mag.end(), 0.0f);
            std::fill(out_ifreq.begin(), out_ifreq.end(), 0.0f);
            for (int32_t out_k = 0; out_k < NUM_BINS; ++out_k) {
                const float in_k_f = static_cast<float>(out_k) / pitch_rate;
                const int32_t in_k0 = static_cast<int32_t>(in_k_f);
                if (in_k0 < 0 || in_k0 >= NUM_BINS) continue;
                const float frac = in_k_f - static_cast<float>(in_k0);
                const int32_t in_k1 = (in_k0 + 1 < NUM_BINS) ? in_k0 + 1 : in_k0;
                out_mag[out_k] = mag[in_k0] * (1.0f - frac) + mag[in_k1] * frac;
                const int32_t in_peak = peak_owner[in_k0];
                out_ifreq[out_k] = ifreq[in_peak] * pitch_rate;
            }
            for (int32_t k = 0; k < NUM_BINS; ++k) {
                syn_phase[k] = wrap_phase(syn_phase[k] + out_ifreq[k] * freq_per_bin);
                pass1_syn_phase[k] = syn_phase[k];
            }
            for (int32_t out_k = 0; out_k < NUM_BINS; ++out_k) {
                const float in_k_f = static_cast<float>(out_k) / pitch_rate;
                const int32_t in_k0 = (std::max)(0, (std::min)(NUM_BINS - 1, static_cast<int32_t>(in_k_f)));
                const int32_t in_peak = peak_owner[in_k0];
                const int32_t out_peak = (std::max)(0, (std::min)(NUM_BINS - 1, static_cast<int32_t>(std::round(static_cast<float>(in_peak) * pitch_rate))));
                if (out_k == out_peak) continue;
                const float rel_phase = ana_phase[in_k0] - ana_phase[in_peak];
                syn_phase[out_k] = wrap_phase(pass1_syn_phase[out_peak] + rel_phase);
            }
            for (int32_t k = 0; k < NUM_BINS; ++k) fft_buf[k] = std::polar(out_mag[k], syn_phase[k]);
            for (int32_t k = NUM_BINS; k < FFT_SIZE; ++k) fft_buf[k] = std::conj(fft_buf[FFT_SIZE - k]);
            fft_inplace(fft_buf.data(), FFT_SIZE, true);
            for (int32_t j = 0; j < FFT_SIZE; ++j) {
                const int32_t p = (out_write_pos + j) % OUT_SIZE;
                out_buf[p] += fft_buf[j].real() * hann[j] * ola_gain;
            }
        };
        do_channel(in_L, ana_phase_L, syn_phase_L, out_L);
        do_channel(in_R, ana_phase_R, syn_phase_R, out_R);
        out_write_pos = (out_write_pos + HOP_SIZE) % OUT_SIZE;
        out_available += HOP_SIZE;
    }

    void process(float* dryL, float* dryR, int32_t total_samples, float pitch_rate, float mix) override {
        for (int32_t i = 0; i < total_samples; ++i) {
            in_L[in_write] = dryL[i];
            in_R[in_write] = dryR[i];
            in_write = (in_write + 1) % IN_SIZE;
            if (++hop_counter >= HOP_SIZE) {
                hop_counter = 0;
                process_frame(pitch_rate);
            }
            float wet_l, wet_r;
            if (out_available > 0) {
                wet_l = out_L[out_read];
                wet_r = out_R[out_read];
                out_L[out_read] = 0.0f;
                out_R[out_read] = 0.0f;
                out_read = (out_read + 1) % OUT_SIZE;
                --out_available;
            } else {
                wet_l = wet_r = 0.0f;
            }
            dryL[i] = dryL[i] * (1.0f - mix) + wet_l * mix;
            dryR[i] = dryR[i] * (1.0f - mix) + wet_r * mix;
        }
    }
};

struct PitchShiftHandle {
    std::unique_ptr<IPitchShiftState> state;
    int64_t last_sample_index = -1;
};

static std::mutex g_ps_state_mutex;
static std::map<int64_t, std::shared_ptr<PitchShiftHandle>> g_ps_handles;

bool func_proc_audio_pitch_shift(FILTER_PROC_AUDIO* audio) {
    const int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    const int32_t channels = (std::min)(2, audio->object->channel_num);
    const int32_t algo = ps_algo.value;
    const float pitch = static_cast<float>(ps_pitch.value);
    const float speed = static_cast<float>(std::abs(ps_speed.value) / 100.0);
    const float mix = static_cast<float>(ps_mix.value / 100.0);

    float pitch_total = pitch;
    if (speed > 0.0f && speed != 1.0f) pitch_total -= 12.0f * std::log2(speed);
    if (mix <= 1e-6f || std::abs(pitch_total) < 1e-6f) return true;
    const float pitch_rate = std::pow(2.0f, pitch_total / 12.0f);

    std::shared_ptr<PitchShiftHandle> handle;
    {
        std::lock_guard<std::mutex> lock(g_ps_state_mutex);
        auto& h = g_ps_handles[audio->object->effect_id];
        if (!h) h = std::make_shared<PitchShiftHandle>();
        if (!h->state || h->state->algo_id != algo) {
            if (algo == 0) h->state = std::make_unique<GranularState>();
            else h->state = std::make_unique<PhaseVocoderState>();
            h->state->algo_id = algo;
            h->last_sample_index = -1;
        }
        if (h->last_sample_index != -1 && h->last_sample_index != audio->object->sample_index) h->state->clear();
        h->last_sample_index = audio->object->sample_index + total_samples;
        handle = h;
    }

    std::vector<float> bufL(total_samples), bufR(total_samples);

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    handle->state->process(bufL.data(), bufR.data(), total_samples, pitch_rate, mix);

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

void CleanupPitchShiftResources() {
    std::lock_guard<std::mutex> lock(g_ps_state_mutex);
    g_ps_handles.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_pitch_shift = {
    TYPE_AUDIO_FILTER_OBJECT,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_pitch_shift,
    nullptr,
    func_proc_audio_pitch_shift
};
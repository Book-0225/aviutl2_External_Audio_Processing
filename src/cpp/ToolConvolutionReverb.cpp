#include "Avx2Utils.h"
#include "ConvolutionEngine.h"
#include "Eap2Common.h"
#include "Reverb.h"
#include "StringUtils.h"
#include "WavLoader.h"

#include <map>
#include <thread>

constexpr auto TOOL_NAME = L"Convolution Reverb";

FILTER_ITEM_FILE conv_ir_file(L"IRファイル(L)", L"", L"WAV File (*.wav)\0*.wav\0");
FILTER_ITEM_FILE conv_ir_file_r(L"IRファイル(R)", L"", L"WAV File (*.wav)\0*.wav\0");
FILTER_ITEM_CHECK conv_normalize(L"正規化", true);
FILTER_ITEM_TRACK conv_ir_gain(L"IRゲイン", 0.0, -24.0, 24.0, 0.1, nullptr, 1.0);

static FILTER_ITEM_SELECT::ITEM conv_ch_mode_list[] = {
    { L"モノラルIR", 0 },
    { L"ステレオIR(独立)", 1 },
    { nullptr }
};
FILTER_ITEM_SELECT conv_ch_mode(L"チャンネルモード", 1, conv_ch_mode_list);
static FILTER_ITEM_SELECT::ITEM conv_ts4ch_order_list[] = {
    { L"標準 (LL,LR,RL,RR)", 0 },
    { L"入力別 (LL,RL,LR,RR)", 1 },
    { nullptr }
};
FILTER_ITEM_SELECT conv_ts4ch_order(L"チャンネル順", 0, conv_ts4ch_order_list);

FILTER_ITEM_TRACK conv_predelay(L"プリディレイ", 0.0, 0.0, 500.0, 1.0, nullptr, 1.0);
FILTER_ITEM_TRACK conv_highcut(L"ハイカット", 20.0, 1.0, 20.0, 0.1, nullptr, 1.0);
FILTER_ITEM_TRACK conv_lowcut(L"ローカット", 0.02, 0.02, 1.0, 0.01, nullptr, 1.0);
FILTER_ITEM_TRACK conv_mix(L"Mix", 40.0, 0.0, 100.0, 0.1, nullptr, 1.0);

void* filter_items_convolution_reverb[] = {
    &conv_ir_file,
    &conv_ir_file_r,
    &conv_normalize,
    &conv_ir_gain,
    &conv_ch_mode,
    &conv_ts4ch_order,
    &conv_predelay,
    &conv_highcut,
    &conv_lowcut,
    &conv_mix,
    nullptr
};

constexpr int32_t CONV_BLOCK_SIZE = 1024;
constexpr double MAX_IR_SECONDS = 8.0;
constexpr int32_t MAX_PREDELAY_SAMPLES_AT_48K = static_cast<int32_t>(500.0 * 48000.0 / 1000.0) + 16;

class SimpleDelayLine {
  public:
    void init(int32_t max_samples) {
        buffer.assign(static_cast<size_t>(max_samples) + 1, 0.0f);
        size = static_cast<int32_t>(buffer.size());
        w_pos = 0;
    }

    inline float process(float in, int32_t delay_samples) {
        if (size <= 1) return in;
        if (delay_samples >= size) delay_samples = size - 1;
        if (delay_samples < 0) delay_samples = 0;
        buffer[w_pos] = in;
        int32_t r_pos = w_pos - delay_samples;
        if (r_pos < 0) r_pos += size;
        float out = buffer[r_pos];
        if (++w_pos >= size) w_pos = 0;
        return out;
    }

    void clear() {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        w_pos = 0;
    }

  private:
    std::vector<float> buffer;
    int32_t size = 0;
    int32_t w_pos = 0;
};

struct ConvReverbState {
    std::atomic<bool> loading{ false };
    bool ready = false;
    bool true_stereo_mode = false;
    std::wstring loaded_path_l;
    std::wstring loaded_path_r;
    double loaded_sample_rate = 0.0;
    int32_t loaded_ts4ch_order = -1;
    bool stereo_ir = false;

    std::shared_ptr<const ConvReverb::IrSpectrum> spectrum_l;
    std::shared_ptr<const ConvReverb::IrSpectrum> spectrum_r;
    std::shared_ptr<const ConvReverb::TrueStereoSpectrum> ts_spectrum;

    ConvReverb::StreamingConvolver conv_l;
    ConvReverb::StreamingConvolver conv_r;
    ConvReverb::TrueStereoStreamingConvolver ts_conv;
    bool conv_initialized = false;

    SimpleDelayLine predelay_l;
    SimpleDelayLine predelay_r;
    OnePoleLPF hicut_l;
    OnePoleLPF hicut_r;
    OnePoleLPF lowcut_l;
    OnePoleLPF lowcut_r;

    int64_t last_sample_index = -1;
    bool delay_lines_initialized = false;

    void EnsureDelayLines() {
        if (delay_lines_initialized) return;
        predelay_l.init(MAX_PREDELAY_SAMPLES_AT_48K);
        predelay_r.init(MAX_PREDELAY_SAMPLES_AT_48K);
        delay_lines_initialized = true;
    }

    void ClearStreamState() {
        predelay_l.clear();
        predelay_r.clear();
        hicut_l.clear();
        hicut_r.clear();
        lowcut_l.clear();
        lowcut_r.clear();
        if (conv_initialized) {
            if (true_stereo_mode) {
                ts_conv.Clear();
            } else {
                conv_l.Clear();
                conv_r.Clear();
            }
        }
    }
};

static std::mutex g_conv_state_mutex;
static std::map<int64_t, ConvReverbState> g_conv_states;

static void SanitizeSamples(std::vector<float>& v);
static void PrepareChannel(std::vector<float>& v, int32_t src_sample_rate, double project_sr) {
    if (src_sample_rate > 0 && project_sr > 0 && src_sample_rate != static_cast<int32_t>(project_sr))
        v = WavLoader::ResampleLinear(v, src_sample_rate, static_cast<int32_t>(project_sr));
    int64_t max_len = static_cast<int64_t>(project_sr * MAX_IR_SECONDS);
    if (max_len > 0 && static_cast<int64_t>(v.size()) > max_len)
        v.resize(static_cast<size_t>(max_len));
    SanitizeSamples(v);
}

static bool LoadAndPrepareWav(const std::wstring& path, double project_sr, std::vector<float>& out_l, std::vector<float>& out_r) {
    WavLoader::WavData wav = WavLoader::Load(path);
    if (!wav.valid) return false;
    out_l = wav.left;
    out_r = wav.right;
    PrepareChannel(out_l, wav.sample_rate, project_sr);
    PrepareChannel(out_r, wav.sample_rate, project_sr);
    return true;
}

static void SanitizeSamples(std::vector<float>& v) {
    for (auto& s : v)
        if (!std::isfinite(s)) s = 0.0f;
}

static void SafetyClamp(float* buf, int32_t n, float ceiling) {
    for (int32_t i = 0; i < n; ++i) {
        float s = buf[i];
        if (std::isnan(s))
            buf[i] = 0.0f;
        else if (s > ceiling)
            buf[i] = ceiling;
        else if (s < -ceiling)
            buf[i] = -ceiling;
    }
}

static void NormalizeTogether(std::initializer_list<std::vector<float>*> arrays) {
    constexpr float kMaxNormalizeGain = 100.0f;
    float peak = 0.0f;
    for (auto* v : arrays)
        for (float s : *v)
            if (std::isfinite(s)) peak = (std::max)(peak, std::fabs(s));
    if (peak <= 1e-6f) return;
    constexpr float kTargetPeak = 0.06f;
    float g = (std::min)(kTargetPeak / peak, kMaxNormalizeGain);
    for (auto* v : arrays)
        for (auto& s : *v) s *= g;
}

static void LoadIRAsync(int64_t effect_id, std::wstring path_l, std::wstring path_r, double sample_rate, bool normalize, int32_t channel_mode, int32_t ts4ch_order) {
    bool true_stereo = false;
    bool ok = false;
    bool stereo_ir = false;
    std::shared_ptr<ConvReverb::IrSpectrum> spec_l, spec_r;
    std::shared_ptr<ConvReverb::TrueStereoSpectrum> ts_spec;

    if (!path_r.empty()) {
        true_stereo = true;
        std::vector<float> ir_ll, ir_lr, ir_rl, ir_rr;
        bool ok_l = LoadAndPrepareWav(path_l, sample_rate, ir_ll, ir_lr);
        bool ok_r = LoadAndPrepareWav(path_r, sample_rate, ir_rl, ir_rr);
        ok = ok_l && ok_r;
        if (ok) {
            if (normalize) NormalizeTogether({ &ir_ll, &ir_lr, &ir_rl, &ir_rr });
            ts_spec = std::make_shared<ConvReverb::TrueStereoSpectrum>();
            ts_spec->Build(ir_ll, ir_lr, ir_rl, ir_rr, CONV_BLOCK_SIZE);
        }
    } else {
        WavLoader::WavDataMulti multi = WavLoader::LoadMulti(path_l);
        if (multi.valid && multi.channels >= 4) {
            true_stereo = true;
            int32_t idx_ll = 0, idx_lr, idx_rl, idx_rr = 3;
            if (ts4ch_order == 1) {
                idx_lr = 2;
                idx_rl = 1;
            } else {
                idx_lr = 1;
                idx_rl = 2;
            }
            std::vector<float> ir_ll = multi.ch[static_cast<size_t>(idx_ll)];
            std::vector<float> ir_lr = multi.ch[static_cast<size_t>(idx_lr)];
            std::vector<float> ir_rl = multi.ch[static_cast<size_t>(idx_rl)];
            std::vector<float> ir_rr = multi.ch[static_cast<size_t>(idx_rr)];
            PrepareChannel(ir_ll, multi.sample_rate, sample_rate);
            PrepareChannel(ir_lr, multi.sample_rate, sample_rate);
            PrepareChannel(ir_rl, multi.sample_rate, sample_rate);
            PrepareChannel(ir_rr, multi.sample_rate, sample_rate);
            ok = true;
            if (normalize) NormalizeTogether({ &ir_ll, &ir_lr, &ir_rl, &ir_rr });
            ts_spec = std::make_shared<ConvReverb::TrueStereoSpectrum>();
            ts_spec->Build(ir_ll, ir_lr, ir_rl, ir_rr, CONV_BLOCK_SIZE);
        } else {
            ok = multi.valid;
            if (ok) {
                std::vector<float> ir_l = multi.ch[0];
                std::vector<float> ir_r = (multi.channels >= 2) ? multi.ch[1] : multi.ch[0];
                PrepareChannel(ir_l, multi.sample_rate, sample_rate);
                PrepareChannel(ir_r, multi.sample_rate, sample_rate);
                if (normalize) NormalizeTogether({ &ir_l, &ir_r });
                stereo_ir = (channel_mode == 1) && (multi.channels >= 2);
                spec_l = std::make_shared<ConvReverb::IrSpectrum>();
                spec_l->Build(ir_l, CONV_BLOCK_SIZE);
                if (stereo_ir) {
                    spec_r = std::make_shared<ConvReverb::IrSpectrum>();
                    spec_r->Build(ir_r, CONV_BLOCK_SIZE);
                } else {
                    spec_r = spec_l;
                }
            } else {
                DbgPrint(std::wstring(TOOL_NAME) + StringUtils::Utf8ToWide(multi.error), LOG_TYPE::LOG_WARN);
            }
        }
    }

    std::lock_guard<std::mutex> lock(g_conv_state_mutex);
    auto it = g_conv_states.find(effect_id);
    if (it == g_conv_states.end()) return;
    ConvReverbState& state = it->second;

    if (ok) {
        state.true_stereo_mode = true_stereo;
        state.loaded_path_l = path_l;
        state.loaded_path_r = path_r;
        state.loaded_sample_rate = sample_rate;
        state.loaded_ts4ch_order = ts4ch_order;

        if (true_stereo) {
            state.ts_spectrum = ts_spec;
            state.ts_conv.Init(state.ts_spectrum);
        } else {
            state.stereo_ir = stereo_ir;
            state.spectrum_l = spec_l;
            state.spectrum_r = spec_r;
            state.conv_l.Init(state.spectrum_l);
            state.conv_r.Init(state.spectrum_r);
        }
        state.conv_initialized = true;
        state.ready = true;
        state.ClearStreamState();
    } else {
        state.loaded_path_l = path_l;
        state.loaded_path_r = path_r;
        state.loaded_sample_rate = sample_rate;
        state.loaded_ts4ch_order = ts4ch_order;
    }
    state.loading = false;
}

bool func_proc_audio_convolution_reverb(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);
    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    std::wstring ir_path_l = (conv_ir_file.value && *conv_ir_file.value) ? std::filesystem::path(conv_ir_file.value).wstring() : L"";
    std::wstring ir_path_r = (conv_ir_file_r.value && *conv_ir_file_r.value) ? std::filesystem::path(conv_ir_file_r.value).wstring() : L"";
    bool normalize = conv_normalize.value;
    int32_t channel_mode = static_cast<int32_t>(conv_ch_mode.value);
    int32_t ts4ch_order = static_cast<int32_t>(conv_ts4ch_order.value);
    float p_ir_gain = powf(10.0f, static_cast<float>(conv_ir_gain.value) / 20.0f);
    float p_predelay_ms = static_cast<float>(conv_predelay.value);
    float p_highcut_khz = static_cast<float>(conv_highcut.value);
    float p_lowcut_khz = static_cast<float>(conv_lowcut.value);
    float p_mix = static_cast<float>(conv_mix.value / 100.0);

    ConvReverbState* state = nullptr;
    bool need_load = false;
    {
        std::lock_guard<std::mutex> lock(g_conv_state_mutex);
        state = &g_conv_states[audio->object->effect_id];
        state->EnsureDelayLines();
        if (state->last_sample_index != -1 && state->last_sample_index != audio->object->sample_index)
            state->ClearStreamState();
        state->last_sample_index = audio->object->sample_index + total_samples;
        bool path_changed = (ir_path_l != state->loaded_path_l) || (ir_path_r != state->loaded_path_r) || (state->loaded_sample_rate != Fs) || (state->loaded_ts4ch_order != ts4ch_order);
        if (!ir_path_l.empty() && path_changed && !state->loading) {
            state->loading = true;
            need_load = true;
        }
    }

    if (need_load) {
        std::thread([effect_id = audio->object->effect_id, ir_path_l, ir_path_r, Fs, normalize, channel_mode, ts4ch_order]() {
            LoadIRAsync(effect_id, ir_path_l, ir_path_r, Fs, normalize, channel_mode, ts4ch_order);
        }).detach();
    }

    if (ir_path_l.empty() || !state->ready || p_mix <= 0.0f) return true;

    state->hicut_l.set_cutoff(p_highcut_khz * 1000.0f, static_cast<float>(Fs));
    state->hicut_r.set_cutoff(p_highcut_khz * 1000.0f, static_cast<float>(Fs));
    state->lowcut_l.set_cutoff(p_lowcut_khz * 1000.0f, static_cast<float>(Fs));
    state->lowcut_r.set_cutoff(p_lowcut_khz * 1000.0f, static_cast<float>(Fs));
    int32_t predelay_samples = static_cast<int32_t>(p_predelay_ms * 0.001f * static_cast<float>(Fs));

    std::vector<float> bufL(total_samples), bufR(total_samples);
    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);
    SanitizeSamples(bufL);
    SanitizeSamples(bufR);

    std::vector<float> sendL(total_samples), sendR(total_samples);
    for (int32_t i = 0; i < total_samples; ++i) {
        sendL[i] = state->predelay_l.process(bufL[i], predelay_samples) * p_ir_gain;
        sendR[i] = state->predelay_r.process(bufR[i], predelay_samples) * p_ir_gain;
    }

    std::vector<float> wetL(total_samples), wetR(total_samples);
    if (state->true_stereo_mode) {
        state->ts_conv.Process(sendL.data(), sendR.data(), wetL.data(), wetR.data(), total_samples);
    } else {
        state->conv_l.Process(sendL.data(), wetL.data(), total_samples);
        state->conv_r.Process(sendR.data(), wetR.data(), total_samples);
    }

    for (int32_t i = 0; i < total_samples; ++i) {
        float l = state->hicut_l.process(wetL[i]);
        l = l - state->lowcut_l.process(l);
        wetL[i] = l;
        float r = state->hicut_r.process(wetR[i]);
        r = r - state->lowcut_r.process(r);
        wetR[i] = r;
    }

    Avx2Utils::MixAudioAVX2(wetL.data(), bufL.data(), total_samples, p_mix, 1.0f - p_mix, 1.0f);
    Avx2Utils::MixAudioAVX2(wetR.data(), bufR.data(), total_samples, p_mix, 1.0f - p_mix, 1.0f);

    constexpr float kOutputSafetyCeiling = 16.0f;
    SafetyClamp(wetL.data(), total_samples, kOutputSafetyCeiling);
    SafetyClamp(wetR.data(), total_samples, kOutputSafetyCeiling);

    if (channels >= 1) audio->set_sample_data(wetL.data(), 0);
    if (channels >= 2) audio->set_sample_data(wetR.data(), 1);

    return true;
}

void CleanupConvolutionReverbResources() {
    std::lock_guard<std::mutex> lock(g_conv_state_mutex);
    g_conv_states.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_convolution_reverb = {
    TYPE_AUDIO_FILTER_OBJECT,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_convolution_reverb,
    nullptr,
    func_proc_audio_convolution_reverb
};
#include "Eap2Common.h"
#include "MidiParser.h"
#include "SynthCommon.h"

#define TSF_IMPLEMENTATION
#include "tsf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

constexpr auto TOOL_NAME = L"MIDI Generator";

FILTER_ITEM_FILE midi_file(L"MIDIファイル", L"", L"MIDI File (*.mid)\0*.mid;*.midi\0");
FILTER_ITEM_SELECT::ITEM list_sync_mode[] = {
    { L"同期しない", 0 },
    { L"MIDIにBPMを同期", 1 },
    { L"AviUtlにBPMを同期", 2 },
    { nullptr }
};
FILTER_ITEM_SELECT midi_sync_mode(L"BPMの同期", 0, list_sync_mode);
FILTER_ITEM_TRACK midi_fixed_bpm(L"BPM(手動)", 120.0, 1.0, 999.0, 0.1);
FILTER_ITEM_TRACK midi_offset(L"オフセット", 0.0, -100.0, 100.0, 0.01);
FILTER_ITEM_SELECT::ITEM list_render_mode[] = {
    { L"内蔵シンセ", 0 },
    { L"SF2", 1 },
    { nullptr }
};
FILTER_ITEM_SELECT render_mode(L"レンダリング", 0, list_render_mode);
FILTER_ITEM_GROUP sf2_group(L"SF2", false);
FILTER_ITEM_FILE sf2_file(L"SF2ファイル", L"", L"SoundFont\0*.sf2\0");
FILTER_ITEM_TRACK sf2_master_volume(L"SF2音量", 1.0, 0.0, 2.0, 0.01);
FILTER_ITEM_TRACK sf2_reverb_level(L"SF2リバーブ", 0.0, 0.0, 1.0, 0.01);
FILTER_ITEM_GROUP built_in_group(L"内蔵", false);
FILTER_ITEM_SELECT midi_type(L"波形", 0, gen_type_list);
FILTER_ITEM_TRACK midi_timbre(L"音色", 0.0, 0.0, 1.0, 0.01);
FILTER_ITEM_TRACK midi_attack(L"アタック", 0.0, 0.0, 2000.0, 1.0);
FILTER_ITEM_TRACK midi_decay(L"ディケイ", 0.0, 0.0, 5000.0, 1.0);
FILTER_ITEM_TRACK midi_sustain(L"サステイン", 0.0, -60.0, 0.0, 0.1);
FILTER_ITEM_TRACK midi_release(L"リリース", 0.0, 0.0, 5000.0, 1.0);
FILTER_ITEM_TRACK midi_cutoff(L"カットオフ", 1.0, 0.0, 1.0, 0.01);
FILTER_ITEM_TRACK midi_reso(L"レゾナンス", 0.0, 0.0, 1.0, 0.01);
FILTER_ITEM_TRACK midi_detune(L"デチューン", 0.2, 0.0, 1.0, 0.01);

void* filter_items_midi_gen[] = {
    &midi_file,
    &midi_sync_mode,
    &midi_fixed_bpm,
    &midi_offset,
    &render_mode,
    &sf2_group,
    &sf2_file,
    &sf2_master_volume,
    &sf2_reverb_level,
    &built_in_group,
    &midi_type,
    &midi_timbre,
    &midi_attack,
    &midi_decay,
    &midi_sustain,
    &midi_release,
    &midi_cutoff,
    &midi_reso,
    &midi_detune,
    nullptr
};

struct RendererInitParams {
    double sample_rate = 44100.0;
};

class ISynthRenderer {
  public:
    virtual ~ISynthRenderer() = default;
    virtual bool Init(const RendererInitParams& params) = 0;
    virtual void NoteOn(int32_t channel, int32_t note, float velocity, double time_sec) = 0;
    virtual void NoteOff(int32_t channel, int32_t note, double time_sec) = 0;
    virtual void PitchBend(int32_t channel, int32_t val) = 0;
    virtual void ControlChange(int32_t channel, int32_t cc, int32_t val) = 0;
    virtual void ProgramChange(int32_t channel, int32_t program) = 0;
    virtual void AllNotesOff() = 0;
    virtual void SoundsOff() { AllNotesOff(); }
    virtual void Reset() = 0;
    virtual void Render(float* bufL, float* bufR, int32_t total_samples,
                        double start_time_sec, double Fs) = 0;
    virtual const char* Name() const = 0;
};

class InternalSynthRenderer : public ISynthRenderer {
  public:
    static constexpr int32_t MAX_VOICES = 64;

    struct ChannelState {
        float volume = 1.0f;
        float pan = 0.5f;
        float modWheel = 0.0f;
        int32_t pitchBend = 8192;
    };

    std::vector<VoiceState> voices_;
    std::array<ChannelState, 16> channels_{};
    SynthParams params_{};
    double Fs_ = 44100.0;
    void SetSynthParams(const SynthParams& p) { params_ = p; }

    bool Init(const RendererInitParams& p) override {
        Fs_ = p.sample_rate;
        voices_.resize(MAX_VOICES);
        for (auto& v : voices_) v.init();
        channels_.fill(ChannelState{});
        return true;
    }

    void NoteOn(int32_t ch, int32_t note, float velocity, double time_sec) override {
        VoiceState* v = AllocVoice_(note, velocity, time_sec, ch);
    }

    void NoteOff(int32_t ch, int32_t note, double time_sec) override {
        VoiceState* target = nullptr;
        double oldest = 1e16;
        for (auto& v : voices_) {
            if (v.active && v.noteNumber == note && v.channel == ch && v.noteOffTime < 0.0) {
                if (v.noteOnTime < oldest) {
                    oldest = v.noteOnTime;
                    target = &v;
                }
            }
        }
        if (target) target->noteOffTime = time_sec;
    }

    void PitchBend(int32_t ch, int32_t val) override {
        channels_[ch].pitchBend = val;
    }

    void ControlChange(int32_t ch, int32_t cc, int32_t val) override {
        switch (cc) {
            case 1:
                channels_[ch].modWheel = val / 127.0f;
                break;
            case 7:
                channels_[ch].volume = val / 127.0f;
                break;
            case 10:
                channels_[ch].pan = val / 127.0f;
                break;
            default:
                break;
        }
    }

    void ProgramChange(int32_t ch, int32_t program) override {
        // 要らない
    }

    void AllNotesOff() override {
        for (auto& v : voices_) {
            v.active = false;
            v.noteNumber = -1;
            v.noteOnTime = 0.0;
            v.noteOffTime = -1.0;
            v.reset_filter();
        }
    }

    void Reset() override {
        AllNotesOff();
        channels_.fill(ChannelState{});
    }

    void Render(float* bufL, float* bufR, int32_t total_samples,
                double start_time_sec, double Fs) override {
        for (int32_t i = 0; i < total_samples; ++i) {
            double t = start_time_sec + static_cast<double>(i) / Fs;
            StereoSample mix = { 0.0f, 0.0f };

            for (auto& v : voices_) {
                if (!v.active) continue;

                const auto& ch = channels_[v.channel];
                v.modWheel = ch.modWheel;
                v.volume = ch.volume;
                v.pan = ch.pan;

                float freq_hz = MidiParser::CalculateFrequency(v.noteNumber, ch.pitchBend);
                SynthParams lp = params_;
                lp.freq = freq_hz;

                double note_elapsed = t - v.noteOnTime;
                double effective_dur = 100000.0;
                if (v.noteOffTime >= 0.0) {
                    effective_dur = v.noteOffTime - v.noteOnTime;
                    if (effective_dur < 0) effective_dur = 0;
                    if (note_elapsed > effective_dur + lp.release) {
                        v.active = false;
                        continue;
                    }
                }
                if (note_elapsed < 0) continue;

                StereoSample s = GenerateSampleStereo(v, lp, note_elapsed, Fs, effective_dur) / 2;
                s = s * v.velocity;
                mix += s;
            }
            bufL[i] = static_cast<float>(mix.l);
            bufR[i] = static_cast<float>(mix.r);
        }
    }

    const char* Name() const override { return "InternalSynth"; }

  private:
    VoiceState* AllocVoice_(int32_t note, float velocity, double time, int32_t ch) {
        VoiceState* target = nullptr;
        for (auto& v : voices_)
            if (!v.active) {
                target = &v;
                break;
            }
        if (!target)
            for (auto& v : voices_)
                if (v.noteOffTime >= 0.0) {
                    target = &v;
                    break;
                }
        if (!target) {
            double oldest = 1e16;
            for (auto& v : voices_)
                if (v.noteOnTime < oldest) {
                    oldest = v.noteOnTime;
                    target = &v;
                }
        }

        if (!target) target = &voices_[0];

        target->init();
        target->active = true;
        target->noteNumber = note;
        target->velocity = velocity;
        target->noteOnTime = time;
        target->noteOffTime = -1.0;
        target->channel = ch;
        return target;
    }
};

class SF2SynthRenderer : public ISynthRenderer {
  public:
    float masterVolume = 1.0f;
    float reverbLevel = 0.0f;

    bool Init(const RendererInitParams& p) override {
        Fs_ = p.sample_rate;
        if (tsf_) {
            tsf_set_output(tsf_, TSF_STEREO_INTERLEAVED, static_cast<int32_t>(Fs_), 0.0f);
        }
        return true;
    }

    bool LoadFile(const std::filesystem::path& path) {
        if (path == loadedPath_ && tsf_) return true;

        tsf* next = tsf_load_filename(path.string().c_str());
        if (!next) return false;

        if (tsf_) tsf_close(tsf_);
        tsf_ = next;
        tsf_set_output(tsf_, TSF_STEREO_INTERLEAVED, static_cast<int32_t>(Fs_), 0.0f);
        tsf_set_volume(tsf_, masterVolume);

        ResetChannels_();
        loadedPath_ = path;
        return true;
    }

    bool IsLoaded() const { return tsf_ != nullptr; }

    ~SF2SynthRenderer() override {
        if (tsf_) tsf_close(tsf_);
    }

    void NoteOn(int32_t ch, int32_t note, float velocity, double time_sec) override {
        if (!tsf_) return;
        tsf_channel_note_on(tsf_, ch, note, velocity);
    }

    void NoteOff(int32_t ch, int32_t note, double time_sec) override {
        if (!tsf_) return;
        tsf_channel_note_off(tsf_, ch, note);
    }

    void PitchBend(int32_t ch, int32_t val) override {
        if (!tsf_) return;
        tsf_channel_set_pitchwheel(tsf_, ch, val);
    }

    void ControlChange(int32_t ch, int32_t cc, int32_t val) override {
        if (!tsf_) return;

        switch (cc) {
            case 100:
                rpn_lsb_[ch] = val;
                return;
            case 101:
                rpn_msb_[ch] = val;
                return;
            case 98:
                nrpn_lsb_[ch] = val;
                return;
            case 99:
                nrpn_msb_[ch] = val;
                return;

            case 6:
                HandleDataEntry_(ch, val, 0);
                return;
            case 38:
                HandleDataEntry_(ch, 0, val);
                return;
            case 96:
            case 97:
                return;
            case 120:
                tsf_channel_sounds_off_all(tsf_, ch);
                return;
            case 121:
                ResetChannel_(ch);
                return;
            case 123:
            case 124:
            case 125:
            case 126:
            case 127:
                tsf_channel_note_off_all(tsf_, ch);
                return;

            default:
                break;
        }

        tsf_channel_midi_control(tsf_, ch, cc, val);
    }

    void ProgramChange(int32_t ch, int32_t program) override {
        if (!tsf_) return;
        int32_t bank = bankMSB_[ch] * 128 + bankLSB_[ch];
        int32_t drum = (ch == 9) ? 1 : 0;
        tsf_channel_set_bank_preset(tsf_, ch, bank, program);
        (void)drum;
    }

    void AllNotesOff() override {
        if (!tsf_) return;
        tsf_note_off_all(tsf_);
    }

    void SoundsOff() override {
        if (!tsf_) return;
        for (int32_t ch = 0; ch < 16; ++ch)
            tsf_channel_sounds_off_all(tsf_, ch);
    }

    void Reset() override {
        if (!tsf_) return;
        SoundsOff();
        ResetChannels_();
    }

    void Render(float* bufL, float* bufR, int32_t total_samples,
                double start_time_sec, double Fs) override {
        if (!tsf_) {
            std::fill(bufL, bufL + total_samples, 0.0f);
            std::fill(bufR, bufR + total_samples, 0.0f);
            return;
        }

        tsf_set_volume(tsf_, masterVolume);
        if (reverbLevel > 0.0f) {
            int32_t rev_val = static_cast<int>(reverbLevel * 127.0f);
            for (int32_t ch = 0; ch < 16; ++ch)
                tsf_channel_midi_control(tsf_, ch, 91, rev_val);
        }

        interleavedBuf_.resize(total_samples * 2);
        tsf_render_float(tsf_, interleavedBuf_.data(), total_samples, 0);

        for (int32_t i = 0; i < total_samples; ++i) {
            bufL[i] = interleavedBuf_[i * 2];
            bufR[i] = interleavedBuf_[i * 2 + 1];
        }
    }

    const char* Name() const override { return "SF2 (TinySoundFont)"; }

  private:
    tsf* tsf_ = nullptr;
    std::filesystem::path loadedPath_;
    double Fs_ = 44100.0;
    std::vector<float> interleavedBuf_;
    std::array<int, 16> bankMSB_{};
    std::array<int, 16> bankLSB_{};
    std::array<int, 16> rpn_msb_{};
    std::array<int, 16> rpn_lsb_{};
    std::array<int, 16> nrpn_msb_{};
    std::array<int, 16> nrpn_lsb_{};

    void ResetChannels_() {
        bankMSB_.fill(0);
        bankLSB_.fill(0);
        rpn_msb_.fill(0x7F);
        rpn_lsb_.fill(0x7F);
        nrpn_msb_.fill(0x7F);
        nrpn_lsb_.fill(0x7F);
        for (int32_t ch = 0; ch < 16; ++ch) ResetChannel_(ch);
    }

    void ResetChannel_(int32_t ch) {
        tsf_channel_set_volume(tsf_, ch, 100 / 127.0f);
        tsf_channel_set_pan(tsf_, ch, 0.5f);
        tsf_channel_set_pitchwheel(tsf_, ch, 8192);
        tsf_channel_set_pitchrange(tsf_, ch, 2.0f);
        tsf_channel_midi_control(tsf_, ch, 1, 0);
        tsf_channel_midi_control(tsf_, ch, 11, 127);
        tsf_channel_midi_control(tsf_, ch, 64, 0);
        tsf_channel_midi_control(tsf_, ch, 67, 0);
        tsf_channel_midi_control(tsf_, ch, 91, 0);
        tsf_channel_midi_control(tsf_, ch, 93, 0);
    }

    void HandleDataEntry_(int32_t ch, int32_t msb, int32_t lsb) {
        int32_t rmsb = rpn_msb_[ch], rlsb = rpn_lsb_[ch];
        if (rmsb == 0x7F && rlsb == 0x7F) return;
        if (rmsb == 0 && rlsb == 0) {
            tsf_channel_set_pitchrange(tsf_, ch, static_cast<float>(msb));
        }
    }
};

class MidiPlayer {
  public:
    MidiParser parser;
    std::filesystem::path currentMidiPath;
    std::unique_ptr<ISynthRenderer> renderer;

    int64_t last_sample_pos = -1;
    size_t next_event_index = 0;
    double current_Fs = 44100.0;

    MidiPlayer() = default;
    bool Load(const std::filesystem::path& path) {
        if (currentMidiPath == path && !parser.GetEvents().empty()) return true;
        if (!parser.Load(path)) return false;
        currentMidiPath = path;
        HardReset();
        return true;
    }

    void HardReset() {
        if (renderer) renderer->Reset();
        last_sample_pos = -1;
        next_event_index = 0;
    }

    void AllNotesOff() {
        if (renderer) renderer->AllNotesOff();
    }

    void PreRoll(double current_time_sec,
                 const std::function<int64_t(double)>& TimeToTick) {
        if (!renderer) return;
        renderer->Reset();

        int64_t current_tick = TimeToTick(current_time_sec);
        if (current_tick <= 0) {
            next_event_index = 0;
            return;
        }

        const auto& events = parser.GetEvents();

        struct NoteKey {
            int32_t ch, note;
        };
        struct NoteInfo {
            int32_t velocity;
            int64_t onTick;
        };
        auto cmp = [](NoteKey a, NoteKey b) {
            return a.ch < b.ch || (a.ch == b.ch && a.note < b.note);
        };
        std::map<NoteKey, NoteInfo, decltype(cmp)> active_notes(cmp);

        std::array<int, 16> prog{};
        prog.fill(0);
        std::array<int, 16> bankMSB{};
        bankMSB.fill(0);
        std::array<int, 16> bankLSB{};
        bankLSB.fill(0);
        std::array<int, 16> pbend{};
        pbend.fill(8192);
        std::array<std::array<int, 128>, 16> cc{};
        for (auto& row : cc) row.fill(-1);

        for (size_t i = 0; i < events.size(); ++i) {
            const auto& ev = events[i];
            if (ev.absoluteTick > current_tick) {
                next_event_index = i;
                break;
            }
            next_event_index = i + 1;
            uint8_t status = ev.status & 0xF0;
            int32_t ch = ev.status & 0x0F;
            if (status == 0x90) {
                if (ev.data2 > 0) {
                    active_notes[{ ch, ev.data1 }] = { ev.data2, ev.absoluteTick };
                } else {
                    active_notes.erase({ ch, ev.data1 });
                }
            } else if (status == 0x80) {
                active_notes.erase({ ch, ev.data1 });
            } else if (status == 0xC0) {
                prog[ch] = ev.data1;
                renderer->ProgramChange(ch, ev.data1);
            } else if (status == 0xE0) {
                int32_t val = MidiParser::CombineBytes14(ev.data1, ev.data2);
                pbend[ch] = val;
                renderer->PitchBend(ch, val);
            } else if (status == 0xB0) {
                cc[ch][ev.data1] = ev.data2;
                renderer->ControlChange(ch, ev.data1, ev.data2);
            }
        }
        for (const auto& kv : active_notes) {
            renderer->NoteOn(kv.first.ch, kv.first.note,
                             kv.second.velocity / 127.0f,
                             current_time_sec - 0.01);
        }
    }

    void DispatchEvent(const RawMidiEvent& ev, double time_sec) {
        if (!renderer) return;

        uint8_t status = ev.status & 0xF0;
        int32_t ch = ev.status & 0x0F;

        switch (status) {
            case 0x90:
                if (ev.data2 > 0)
                    renderer->NoteOn(ch, ev.data1, ev.data2 / 127.0f, time_sec);
                else
                    renderer->NoteOff(ch, ev.data1, time_sec);
                break;
            case 0x80:
                renderer->NoteOff(ch, ev.data1, time_sec);
                break;
            case 0xE0:
                renderer->PitchBend(ch, MidiParser::CombineBytes14(ev.data1, ev.data2));
                break;
            case 0xB0:
                renderer->ControlChange(ch, ev.data1, ev.data2);
                break;
            case 0xC0:
                renderer->ProgramChange(ch, ev.data1);
                break;
        }
    }
};

static std::mutex g_midi_mutex;
static std::map<const void*, MidiPlayer> g_midi_players;

bool func_proc_audio_midi(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int64_t current_obj_sample_index = audio->object->sample_index;
    double Fs = (audio->scene->sample_rate > 0)
                    ? static_cast<double>(audio->scene->sample_rate)
                    : 44100.0;

    if (!midi_file.value || std::filesystem::path(midi_file.value).empty())
        return true;

    std::filesystem::path midi_path(midi_file.value);

    const int32_t mode_render = render_mode.value;

    SynthParams sp;
    sp.type = midi_type.value;
    sp.attack = midi_attack.value / 1000.0;
    sp.decay = midi_decay.value / 1000.0;
    sp.sustain = static_cast<float>(std::pow(10.0, midi_sustain.value / 20.0));
    sp.release = midi_release.value / 1000.0;
    sp.timbre = static_cast<float>(midi_timbre.value);
    sp.filter_cutoff = static_cast<float>(midi_cutoff.value);
    sp.filter_res = static_cast<float>(midi_reso.value);
    sp.detune = static_cast<float>(midi_detune.value);
    sp.freq = 440.0f;

    double offset_sec = midi_offset.value;
    double fixed_bpm = midi_fixed_bpm.value;
    int32_t sync_mode = midi_sync_mode.value;
    double global_bpm = g_shared_bpm.load();

    MidiPlayer* player = nullptr;
    bool seek_detected = false;
    bool renderer_dirty = false;

    {
        std::lock_guard<std::mutex> lock(g_midi_mutex);
        player = &g_midi_players[audio->object];

        if (!player->Load(midi_path)) return true;

        if (player->current_Fs != Fs) {
            player->current_Fs = Fs;
            if (player->renderer)
                player->renderer->Init({ Fs });
            renderer_dirty = true;
        }

        bool need_internal = (mode_render == 0);
        bool need_sf2 = (mode_render == 1);

        auto* cur_internal = dynamic_cast<InternalSynthRenderer*>(player->renderer.get());
        auto* cur_sf2 = dynamic_cast<SF2SynthRenderer*>(player->renderer.get());

        if (need_internal && !cur_internal) {
            auto r = std::make_unique<InternalSynthRenderer>();
            r->Init({ Fs });
            player->renderer = std::move(r);
            renderer_dirty = true;
        } else if (need_sf2 && !cur_sf2) {
            auto r = std::make_unique<SF2SynthRenderer>();
            r->Init({ Fs });
            player->renderer = std::move(r);
            cur_sf2 = static_cast<SF2SynthRenderer*>(player->renderer.get());
            renderer_dirty = true;
        }

        if (need_sf2 && cur_sf2) {
            std::filesystem::path sf2_path =
                (sf2_file.value && *sf2_file.value) ? std::filesystem::path(sf2_file.value)
                                                    : std::filesystem::path{};
            if (!sf2_path.empty()) {
                if (!cur_sf2->LoadFile(sf2_path)) return true;
            } else {
                return true;
            }
            cur_sf2->masterVolume = static_cast<float>(sf2_master_volume.value);
            cur_sf2->reverbLevel = static_cast<float>(sf2_reverb_level.value);
        }

        if (need_internal && cur_internal) {
            cur_internal->SetSynthParams(sp);
        }

        if (!renderer_dirty &&
            player->last_sample_pos != -1 &&
            std::abs(player->last_sample_pos - current_obj_sample_index) > 100) {
            seek_detected = true;
        }
        player->last_sample_pos = current_obj_sample_index + total_samples;
    }
    uint16_t tpqn = player->parser.GetTPQN();
    double tick_factor = 0.0;
    if (sync_mode == 0) tick_factor = (fixed_bpm * tpqn) / 60.0;
    else if (sync_mode == 2) tick_factor = (global_bpm * tpqn) / 60.0;
    auto TimeToTick = [&](double t_sec) -> int64_t {
        double midi_time = t_sec - offset_sec;
        if (midi_time < 0) return -1;
        if (sync_mode == 1) return player->parser.GetTickAtTime(midi_time);
        return static_cast<int64_t>(midi_time * tick_factor);
    };

    if (seek_detected || renderer_dirty || current_obj_sample_index == 0) {
        double t0 = static_cast<double>(current_obj_sample_index) / Fs;
        player->PreRoll(t0, TimeToTick);
    }

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }
    std::fill(bufL.begin(), bufL.begin() + total_samples, 0.0f);
    std::fill(bufR.begin(), bufR.begin() + total_samples, 0.0f);
    const auto& events = player->parser.GetEvents();
    for (int32_t i = 0; i < total_samples; ++i) {
        double t = static_cast<double>(current_obj_sample_index + i) / Fs;
        int64_t tick = TimeToTick(t);
        if (tick >= 0) {
            while (player->next_event_index < events.size()) {
                const auto& ev = events[player->next_event_index];
                if (ev.absoluteTick > tick) break;
                player->DispatchEvent(ev, t);
                player->next_event_index++;
            }
        }
    }
    double start_time = static_cast<double>(current_obj_sample_index) / Fs;
    player->renderer->Render(bufL.data(), bufR.data(), total_samples, start_time, Fs);
    if (audio->object->channel_num >= 1) audio->set_sample_data(bufL.data(), 0);
    if (audio->object->channel_num >= 2) audio->set_sample_data(bufR.data(), 1);
    return true;
}

void CleanupMidiGeneratorResources() {
    std::lock_guard<std::mutex> lock(g_midi_mutex);
    g_midi_players.clear();
}

FILTER_PLUGIN_TABLE filter_plugin_table_midi_gen = {
    TYPE_AUDIO_MEDIA,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_midi_gen,
    nullptr,
    func_proc_audio_midi
};
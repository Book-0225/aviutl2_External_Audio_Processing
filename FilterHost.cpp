#include "Eap2Common.h"
#include <map>
#include <set>
#include <filesystem>
#include <string>

#include "IAudioPluginHost.h"
#include "PluginType.h"
#include "AudioPluginFactory.h"
#include "PluginManager.h"
#include "StringUtils.h"
#include "MidiParser.h"
#include "Avx2Utils.h"

#define FILTER_NAME L"Host"
#define FILTER_NAME_MEDIA L"Host (Media)"

const int MAX_BLOCK_SIZE = 2048;

static std::mutex g_cleanup_mutex;
static std::string g_legacy_id_to_clear;
static std::string g_plugin_path_for_clear;
struct ParamCache {
    double prev_val[4] = { -1.0, -1.0, -1.0, -1.0 };
};
static std::map<std::string, ParamCache> g_param_cache;

struct MidiState {
    std::string prev_path;
    MidiParser parser;
};
static std::map<std::string, MidiState> g_midi_state;

struct ResetGUIContext {
    std::string target_instance_id;
    LPCWSTR param_name;
};

TCHAR filter_ext[] =
L"Audio Plugins (*.vst3;*.clap)\0*.vst3;*.clap\0"
L"VST3 Plugins (*.vst3)\0*.vst3\0"
L"CLAP Plugins (*.clap)\0*.clap\0"
L"All Files (*.*)\0*.*\0\0";

FILTER_ITEM_FILE plugin_path_param(L"プラグイン", L"", filter_ext);
FILTER_ITEM_TRACK track_wet(L"Wet", 100.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK track_volume(L"Gain", 100.0, 0.0, 500.0, 0.1);
FILTER_ITEM_TRACK track_bpm(L"BPM", 120.0, 1.0, 999.0, 0.01);
FILTER_ITEM_TRACK track_ts_num(L"分子", 4.0, 1.0, 32.0, 1.0);
FILTER_ITEM_TRACK track_ts_denom(L"分母", 4.0, 1.0, 32.0, 1.0);
FILTER_ITEM_CHECK check_apply_l(L"Apply to L", true);
FILTER_ITEM_CHECK check_apply_r(L"Apply to R", true);
FILTER_ITEM_CHECK toggle_gui_check(L"プラグインGUIを表示", false);
FILTER_ITEM_CHECK check_param_learn(L"Learn Param", false);
FILTER_ITEM_CHECK check_map_reset(L"Reset Mapping", false);
FILTER_ITEM_TRACK track_param1(L"Param 1", 0.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK track_param2(L"Param 2", 0.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK track_param3(L"Param 3", 0.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK track_param4(L"Param 4", 0.0, 0.0, 100.0, 0.1);
FILTER_ITEM_FILE midi_path_param(L"MIDI File", L"", L"MIDI Files (*.mid;*.midi)\0*.mid;*.midi\0All Files (*.*)\0*.*\0\0");
FILTER_ITEM_CHECK check_bpm_sync_midi(L"MIDIにBPMを同期", false);
FILTER_ITEM_FILE instance_id_param(L"__INSTANCE_ID__", L"", L"");
struct InstanceData {
    char uuid[40] = { 0 };
};
FILTER_ITEM_DATA<InstanceData> instance_data_param(L"INSTANCE_ID");

void* filter_items_host[] = {
    &plugin_path_param,
    &track_wet,
    &track_volume,
    &track_bpm,
    &track_ts_num,
    &track_ts_denom,
    &check_apply_l,
    &check_apply_r,
    &toggle_gui_check,
    &check_param_learn,
    &check_map_reset,
    &track_param1,
    &track_param2,
    &track_param3,
    &track_param4,
    &midi_path_param,
    &check_bpm_sync_midi,
    &instance_id_param,
    &instance_data_param,
    nullptr
};

void* filter_items_host_media[] = {
    &plugin_path_param,
    &track_bpm,
    &track_ts_num,
    &track_ts_denom,
    &toggle_gui_check,
    &check_param_learn,
    &check_map_reset,
    &track_param1,
    &track_param2,
    &track_param3,
    &track_param4,
    &midi_path_param,
    &check_bpm_sync_midi,
    &instance_data_param,
    nullptr
};

void CleanupMainFilterResources() {
    PluginManager::GetInstance().CleanupResources();
}

static std::set<std::string>* g_active_ids_collector = nullptr;

void collect_active_ids_proc(EDIT_SECTION* edit) {
    if (!g_active_ids_collector) return;

    int max_layer = edit->info->layer_max;
    for (int layer = 0; layer <= max_layer; ++layer) {
        OBJECT_HANDLE obj = edit->find_object(layer, 0);
        while (obj != nullptr) {
            int effect_count = edit->count_object_effect(obj, filter_name);
            for (int i = 0; i < effect_count; ++i) {
                std::wstring indexed_filter_name = std::wstring(filter_name);
                if (i > 0) {
                    indexed_filter_name += L":" + std::to_wstring(i);
                }

                LPCSTR hex_encoded_id_str = edit->get_object_item_value(obj, indexed_filter_name.c_str(), instance_data_param.name);
                if (hex_encoded_id_str && hex_encoded_id_str[0] != '\0') {
                    std::string decoded_id_with_padding = StringUtils::HexToString(hex_encoded_id_str);
                    g_active_ids_collector->insert(std::string(decoded_id_with_padding.c_str()));
                }
                else {
                    LPCWSTR legacy_id_w = reinterpret_cast<LPCWSTR>(edit->get_object_item_value(obj, indexed_filter_name.c_str(), instance_id_param.name));
                    if (legacy_id_w && legacy_id_w[0] != L'\0') {
                        g_active_ids_collector->insert(StringUtils::WideToUtf8(legacy_id_w));
                    }
                }
            }
            int end_frame = edit->get_object_layer_frame(obj).end;
            obj = edit->find_object(layer, end_frame + 1);
        }
    }
}

void find_and_clear_legacy_id_proc(EDIT_SECTION* edit) {
    if (g_legacy_id_to_clear.empty()) return;

    int max_layer = edit->info->layer_max;

    for (int layer = 0; layer <= max_layer; ++layer) {
        OBJECT_HANDLE obj = edit->find_object(layer, 0);
        while (obj != nullptr) {
            int effect_count = edit->count_object_effect(obj, filter_name);
            for (int i = 0; i < effect_count; ++i) {
                std::wstring indexed_filter_name_w = std::wstring(filter_name);
                if (i > 0) {
                    indexed_filter_name_w += L":" + std::to_wstring(i);
                }

                LPCSTR val_ptr = edit->get_object_item_value(obj, indexed_filter_name_w.c_str(), instance_id_param.name);

                if (val_ptr != nullptr) {
                    std::string legacy_id_str = std::string(val_ptr);
                    if (legacy_id_str == g_legacy_id_to_clear) {
                        edit->set_object_item_value(obj, indexed_filter_name_w.c_str(), instance_id_param.name, "");

                        DbgPrint("Legacy ID cleared: %hs", legacy_id_str.c_str());
                        return;
                    }
                }
            }
            int current_end_frame = edit->get_object_layer_frame(obj).end;
            obj = edit->find_object(layer, current_end_frame + 1);
        }
    }
}

void func_project_save_impl(PROJECT_FILE* pf) {
    std::set<std::string> active_ids_in_project;
    g_active_ids_collector = &active_ids_in_project;
    if (g_edit_handle) {
        try {
            g_edit_handle->call_edit_section(collect_active_ids_proc);
        }
        catch (...) {
            DbgPrint("[EAP2 Error] Exception in collect_active_ids_proc");
        }
    }
    g_active_ids_collector = nullptr;

    std::string all_data_str = PluginManager::GetInstance().PrepareProjectState(active_ids_in_project);

    if (!all_data_str.empty()) {
        if (all_data_str.size() > 32 * 1024 * 1024) {
            DbgPrint("[EAP2 Error] State data too large. Skipping.");
            pf->set_param_string("AudioHostStateDB", "");
        }
        else {
            pf->set_param_string("AudioHostStateDB", all_data_str.c_str());
            DbgPrint("Saved project state, size: %zu", all_data_str.size());
        }
    }
    else {
        pf->set_param_string("AudioHostStateDB", nullptr);
    }
}

int ProjectSaveExceptionFilter(uint32_t code, struct _EXCEPTION_POINTERS* ep) {
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        DbgPrint("[EAP2 Critical] Access Violation at %p", ep->ExceptionRecord->ExceptionAddress);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void func_project_save(PROJECT_FILE* pf) {
    if (!pf) return;

    __try {
        func_project_save_impl(pf);
    }
    __except (ProjectSaveExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
        DbgPrint("[EAP2 Critical] Save aborted. Clearing project data.");
        pf->set_param_string("AudioHostStateDB", nullptr);
    }
}

void func_project_load(PROJECT_FILE* pf) {
    CleanupMainFilterResources();

    if (!pf) return;
    try {
        LPCSTR all_data_str = pf->get_param_string("AudioHostStateDB");
        if (all_data_str) {
            PluginManager::GetInstance().LoadProjectState(all_data_str);
            DbgPrint("Loaded project state, size: %zu", strlen(all_data_str));
        }
    }
    catch (...) {
        DbgPrint("[EAP2 Error] Exception in func_project_load");
    }
}

void reset_checkbox_proc(void* param, EDIT_SECTION* edit) {
    auto* ctx = static_cast<ResetGUIContext*>(param);
    if (!ctx) return;

    int max_layer = edit->info->layer_max;

    for (int layer = 0; layer <= max_layer; ++layer) {
        int current_frame = 0;
        while (true) {
            OBJECT_HANDLE obj = edit->find_object(layer, current_frame);
            if (obj == nullptr) break;

            int effect_count = edit->count_object_effect(obj, filter_name);
            for (int i = 0; i < effect_count; ++i) {
                std::wstring indexed_filter_name = std::wstring(filter_name);
                if (i > 0) indexed_filter_name += L":" + std::to_wstring(i);

                LPCSTR hex_id = edit->get_object_item_value(obj, indexed_filter_name.c_str(), instance_data_param.name);

                if (hex_id) {
                    std::string raw_data = StringUtils::HexToString(hex_id);
                    if (raw_data.size() >= 36) {
                        std::string obj_uuid = std::string(raw_data.c_str());

                        if (obj_uuid == ctx->target_instance_id) {
                            bool result = edit->set_object_item_value(obj, indexed_filter_name.c_str(), ctx->param_name, "0");
                            return;
                        }
                    }
                }
            }
            OBJECT_LAYER_FRAME frame_info = edit->get_object_layer_frame(obj);
            current_frame = frame_info.end + 1;
        }
    }
}

bool func_proc_audio_host_common(FILTER_PROC_AUDIO* audio, bool is_object) {
    std::string instance_id;
    bool is_migrated = false;

    if (instance_data_param.value->uuid[0] != '\0') {
        instance_id = instance_data_param.value->uuid;
    }
    else {
        LPCWSTR legacy_id_w = nullptr;
        if (!is_object) legacy_id_w = instance_id_param.value;
        if (legacy_id_w && legacy_id_w[0] != L'\0') {
            instance_id = StringUtils::WideToUtf8(legacy_id_w);
            strcpy_s(instance_data_param.value->uuid, sizeof(instance_data_param.value->uuid), instance_id.c_str());
            is_migrated = true;
        }
        else {
            instance_id = StringUtils::GenerateUUID();
            strcpy_s(instance_data_param.value->uuid, sizeof(instance_data_param.value->uuid), instance_id.c_str());
        }
    }

    if (instance_id.empty()) return true;

    int64_t effect_id = audio->object->effect_id;
    bool is_copy = false;
    PluginManager::GetInstance().RegisterOrUpdateInstance(instance_id, effect_id, is_copy);

    if (is_copy) {
        DbgPrint("Copy detected! New instance_id: %hs", instance_id.c_str());
        strcpy_s(instance_data_param.value->uuid, sizeof(instance_data_param.value->uuid), instance_id.c_str());
    }

    if (is_migrated) {
        std::string plugin_path = StringUtils::WideToUtf8(plugin_path_param.value);

        std::lock_guard<std::mutex> task_lock(g_task_queue_mutex);
        g_main_thread_tasks.push_back([id_to_clear = instance_id, path_to_clear = plugin_path]() {
            std::lock_guard<std::mutex> lock(g_cleanup_mutex);
            g_legacy_id_to_clear = id_to_clear;
            g_plugin_path_for_clear = path_to_clear;

            if (g_edit_handle) {
                g_edit_handle->call_edit_section(find_and_clear_legacy_id_proc);
            }
            });

        return true;
    }

    std::wstring plugin_path_w = plugin_path_param.value;
	float wet_val = 100.0f;
    float vol_val = 100.0f;
    bool apply_l = true;
    bool apply_r = true;
    if (!is_object) {
        wet_val = static_cast<float>(track_wet.value);
        vol_val = static_cast<float>(track_volume.value);
        apply_l = check_apply_l.value;
        apply_r = check_apply_r.value;
    }

    bool effective_bypass = (!apply_l && !apply_r) || (wet_val == 0.0f);
    if (effective_bypass && vol_val == 100.0f) {
        return true;
    }

    bool needs_reinitialization = false;
    bool path_changed = false;
    std::string current_plugin_path;

    if (PluginManager::GetInstance().IsPendingReinitialization(effect_id)) {
        return true;
    }

    std::shared_ptr<IAudioPluginHost> host = PluginManager::GetInstance().GetHost(effect_id);
    if (plugin_path_w.empty()) {
        if (host) needs_reinitialization = true;
    }
    else {
        if (!host) {
            needs_reinitialization = true;
        }
        else {
            current_plugin_path = host->GetPluginPath();
            if (current_plugin_path != StringUtils::WideToUtf8(plugin_path_w.c_str())) {
                needs_reinitialization = true;
                path_changed = true;
            }
        }
    }

    if (needs_reinitialization) {
        PluginManager::GetInstance().SetPendingReinitialization(effect_id, true);

        double sampleRate = audio->scene->sample_rate;
        std::string new_path_utf8 = StringUtils::WideToUtf8(plugin_path_w.c_str());

        if (path_changed) {
            PluginManager::GetInstance().ClearMapping(instance_id);
            DbgPrint("Plugin path changed. Mappings cleared for %hs", instance_id.c_str());
        }

        std::lock_guard<std::mutex> task_lock(g_task_queue_mutex);
        g_main_thread_tasks.push_back([effect_id, instance_id, new_path_utf8, sampleRate, path_changed]() {
            std::shared_ptr<IAudioPluginHost> new_host = nullptr;

            if (!new_path_utf8.empty()) {
                auto plugin_type = GetPluginTypeFromPath(std::filesystem::u8path(new_path_utf8).wstring());
                if (plugin_type != PluginType::Unknown) {
                    new_host = AudioPluginFactory::Create(plugin_type, g_hinstance);
                    if (new_host) {
                        if (sampleRate > 0) {
                            if (new_host->LoadPlugin(new_path_utf8, sampleRate, MAX_BLOCK_SIZE)) {
                                std::string state_to_restore;
                                if (!path_changed) {
                                    state_to_restore = PluginManager::GetInstance().GetSavedState(instance_id);
                                }
                                if (!state_to_restore.empty()) {
                                    new_host->SetState(state_to_restore);
                                }
                            }
                            else {
                                new_host = nullptr;
                            }
                        }
                    }
                }
            }

            PluginManager::GetInstance().SetHost(effect_id, new_host);
            PluginManager::GetInstance().SetPendingReinitialization(effect_id, false);
            });

        return true;
    }

    if (host) {
        if (check_map_reset.value) {
            PluginManager::GetInstance().ClearMapping(instance_id);
            {
                std::lock_guard<std::mutex> task_lock(g_task_queue_mutex);
                g_main_thread_tasks.push_back([tgt_id = instance_id]() {
                    if (g_edit_handle) {

                        ResetGUIContext ctx;
                        ctx.target_instance_id = tgt_id;
                        ctx.param_name = check_map_reset.name;
                        g_edit_handle->call_edit_section_param(&ctx, reset_checkbox_proc);
                    }
                    });
            }
            DbgPrint("Mappings Cleared and GUI reset requested for %hs", instance_id.c_str());
        }
        float slider_vals[4] = {
            (float)track_param1.value,
            (float)track_param2.value,
            (float)track_param3.value,
            (float)track_param4.value
        };

        bool is_learning = check_param_learn.value;
        int32_t lastTouched = host->GetLastTouchedParamID();

        ParamCache& cache = g_param_cache[instance_id];

        if (is_learning && lastTouched != -1) {
            for (int i = 0; i < 4; ++i) {
                if (cache.prev_val[i] != -1.0 && std::abs(cache.prev_val[i] - slider_vals[i]) > 0.01) {
                    PluginManager::GetInstance().UpdateMapping(instance_id, i, lastTouched);
                    DbgPrint("Mapped Slider %d to ParamID %d", i + 1, lastTouched);
                    break;
                }
            }
        }

        for (int i = 0; i < 4; ++i) {
            int32_t mapID = PluginManager::GetInstance().GetMappedParamID(instance_id, i);
            if (mapID != -1) {

                float normalized = slider_vals[i] / 100.0f;
                if (normalized < 0.0f) normalized = 0.0f;
                if (normalized > 1.0f) normalized = 1.0f;

                host->SetParameter(mapID, normalized);
            }
            cache.prev_val[i] = slider_vals[i];
        }
    }

    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    thread_local std::vector<float> inL, inR, outL, outR;
    if (inL.size() < total_samples) {
        inL.resize(total_samples); outL.resize(total_samples);
        inR.resize(total_samples); outR.resize(total_samples);
    }

    if (!is_object) {
        if (channels >= 1) audio->get_sample_data(inL.data(), 0);
        if (channels >= 2) audio->get_sample_data(inR.data(), 1);
        else if (channels == 1) Avx2Utils::CopyBufferAVX2(inR.data(), inL.data(), total_samples);
    }

    std::shared_ptr<IAudioPluginHost> host_for_audio = host;

    if (host_for_audio) {
        bool gui_should_show = toggle_gui_check.value;
        if (host_for_audio->IsGuiVisible() != gui_should_show) {
            std::lock_guard<std::mutex> task_lock(g_task_queue_mutex);
            g_main_thread_tasks.push_back([effect_id, instance_id, gui_should_show]() {
                auto host = PluginManager::GetInstance().GetHost(effect_id);
                if (host) {
                    if (gui_should_show) {
                        host->ShowGui();
                    }
                    else {
                        host->HideGui();
                        std::string state = host->GetState();
                        if (!state.empty()) {
                            PluginManager::GetInstance().SaveState(instance_id, state);
                        }
                    }
                }
                });
        }
    }

    if (effective_bypass) {
        float vol_ratio = vol_val / 100.0f;
        if (vol_ratio == 0.0f) {
            Avx2Utils::FillBufferAVX2(outL.data(), total_samples, 0.0f);
            if (channels >= 2) Avx2Utils::FillBufferAVX2(outR.data(), total_samples, 0.0f);
        }
        else {
            Avx2Utils::ScaleBufferAVX2(outL.data(), inL.data(), total_samples, vol_ratio);
            if (channels >= 2) Avx2Utils::ScaleBufferAVX2(outR.data(), inR.data(), total_samples, vol_ratio);
        }
        if (channels >= 1) audio->set_sample_data(outL.data(), 0);
        if (channels >= 2) audio->set_sample_data(outR.data(), 1);
        return true;
    }

    int64_t current_pos = (int64_t)(audio->object->sample_index + 0.5);
    double bpm;
    bool sync_bpm = check_bpm_sync_midi.value;
    int32_t ts_num = (int32_t)track_ts_num.value;
    int32_t ts_denom = (int32_t)track_ts_denom.value;
    bool processed_by_host = false;

    if (host_for_audio) {
        if (PluginManager::GetInstance().ShouldReset(effect_id, current_pos, audio->object->sample_num)) {
            host_for_audio->Reset();
        }
        PluginManager::GetInstance().UpdateLastAudioState(effect_id, current_pos, audio->object->sample_num);

        std::string midi_path_u8 = StringUtils::WideToUtf8(midi_path_param.value);
        MidiState& ms = g_midi_state[instance_id];
        if (ms.prev_path != midi_path_u8) {
            ms.parser.Load(midi_path_u8);
            ms.prev_path = midi_path_u8;
        }

        if (sync_bpm) {
            double current_time_sec = (double)current_pos / audio->scene->sample_rate;
            bpm = ms.parser.GetBpmAtTime(current_time_sec);
        }
        else {
            bpm = track_bpm.value;
        }

        int processed = 0;
        while (processed < total_samples) {
            int block_size = (std::min)(MAX_BLOCK_SIZE, total_samples - processed);
            int64_t current_block_pos = current_pos + processed;

            double time_start = (double)current_block_pos / audio->scene->sample_rate;
            double time_end = (double)(current_block_pos + block_size) / audio->scene->sample_rate;

            std::vector<IAudioPluginHost::MidiEvent> midi_events_for_block;
            int64_t start_tick = 0;
            int64_t end_tick = 0;

            if (ms.parser.GetTPQN() > 0) {
                if (sync_bpm) {
                    start_tick = ms.parser.GetTickAtTime(time_start);
                    end_tick = ms.parser.GetTickAtTime(time_end);
                }
                else {
                    double samplesPerTick = (60.0 * audio->scene->sample_rate) / (bpm * ms.parser.GetTPQN());
                    start_tick = (int64_t)(current_block_pos / samplesPerTick);
                    end_tick = (int64_t)((current_block_pos + block_size) / samplesPerTick);
                }

                const auto& all_events = ms.parser.GetEvents();
                auto it = std::lower_bound(all_events.begin(), all_events.end(), (uint32_t)start_tick,
                    [](const RawMidiEvent& e, uint32_t tick) { return e.absoluteTick < tick; });

                for (; it != all_events.end(); ++it) {
                    if (it->absoluteTick >= end_tick) break;

                    int32_t delta_samples = 0;

                    if (sync_bpm) {
                        int64_t tick_diff = it->absoluteTick - start_tick;
                        int64_t total_tick_diff = end_tick - start_tick;
                        if (total_tick_diff > 0) {
                            delta_samples = (int32_t)((double)tick_diff / total_tick_diff * block_size);
                        }
                    }
                    else {
                        double samplesPerTick = (60.0 * audio->scene->sample_rate) / (bpm * ms.parser.GetTPQN());
                        delta_samples = (int32_t)((it->absoluteTick * samplesPerTick) - current_block_pos);
                    }

                    if (delta_samples < 0) delta_samples = 0;
                    if (delta_samples >= block_size) delta_samples = block_size - 1;

                    midi_events_for_block.push_back({ delta_samples, it->status, it->data1, it->data2 });
                }
            }

            host_for_audio->ProcessAudio(
                inL.data() + processed,
                inR.data() + processed,
                outL.data() + processed,
                outR.data() + processed,
                block_size,
                channels,
                current_block_pos,
                bpm,
                ts_num,
                ts_denom,
                midi_events_for_block
            );

            processed += block_size;
        }
        processed_by_host = true;
    }

    float wet_ratio = wet_val / 100.0f;
    float dry_ratio = 1.0f - wet_ratio;
    float vol_ratio = vol_val / 100.0f;

    if (processed_by_host) {
        if (apply_l) {
            Avx2Utils::MixAudioAVX2(outL.data(), inL.data(), total_samples, wet_ratio, dry_ratio, vol_ratio);
        }
        else {
            Avx2Utils::ScaleBufferAVX2(outL.data(), inL.data(), total_samples, vol_ratio);
        }

        if (channels >= 2) {
            if (apply_r) {
                Avx2Utils::MixAudioAVX2(outR.data(), inR.data(), total_samples, wet_ratio, dry_ratio, vol_ratio);
            }
            else {
                Avx2Utils::ScaleBufferAVX2(outR.data(), inR.data(), total_samples, vol_ratio);
            }
        }
    }
    else {

        float combined_scale = (apply_l ? (wet_ratio + dry_ratio) : 1.0f) * vol_ratio;
        Avx2Utils::ScaleBufferAVX2(outL.data(), inL.data(), total_samples, combined_scale);

        if (channels >= 2) {
            combined_scale = (apply_r ? (wet_ratio + dry_ratio) : 1.0f) * vol_ratio;
            Avx2Utils::ScaleBufferAVX2(outR.data(), inR.data(), total_samples, combined_scale);
        }
    }

    if (channels >= 1) audio->set_sample_data(outL.data(), 0);
    if (channels >= 2) audio->set_sample_data(outR.data(), 1);

    return true;
}

bool func_proc_audio_host(FILTER_PROC_AUDIO* audio) {
    return func_proc_audio_host_common(audio, false);
}

bool func_proc_audio_host_media(FILTER_PROC_AUDIO* audio) {
    return func_proc_audio_host_common(audio, true);
}

FILTER_PLUGIN_TABLE filter_plugin_table_host = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    filter_name,
    label,
	GEN_FILTER_INFO(FILTER_NAME),
    filter_items_host,
    nullptr,
    func_proc_audio_host
};


FILTER_PLUGIN_TABLE filter_plugin_table_host_media = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO | FILTER_PLUGIN_TABLE::FLAG_INPUT,
    filter_name_media,
    label,
	GEN_FILTER_INFO(FILTER_NAME_MEDIA),
    filter_items_host_media,
    nullptr,
    func_proc_audio_host_media
};
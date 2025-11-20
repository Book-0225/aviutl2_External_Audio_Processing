#define _CRT_SECURE_NO_WARNINGS
#include "Eap2Common.h"
#include <map>
#include <set>
#include <filesystem>
#include <random>
#include <string>
#include <regex>

#include "IAudioPluginHost.h"
#include "PluginType.h"
#include "AudioPluginFactory.h"
#include "PluginManager.h"
#include "StringUtils.h"

#define FILTER_NAME L"Host"

const int MAX_BLOCK_SIZE = 2048;

static std::mutex g_cleanup_mutex;
static std::string g_legacy_id_to_clear;
static std::string g_plugin_path_for_clear;

TCHAR filter_ext[] =
L"Audio Plugins (*.vst3;*.clap)\0*.vst3;*.clap\0"
L"VST3 Plugins (*.vst3)\0*.vst3\0"
L"CLAP Plugins (*.clap)\0*.clap\0"
L"All Files (*.*)\0*.*\0\0";

FILTER_ITEM_FILE plugin_path_param(L"プラグイン", L"", filter_ext);
FILTER_ITEM_TRACK track_wet(L"Wet", 100.0, 0.0, 100.0, 0.1);
FILTER_ITEM_TRACK track_volume(L"Gain", 100.0, 0.0, 500.0, 0.1);
FILTER_ITEM_CHECK check_apply_l(L"Apply to L", true);
FILTER_ITEM_CHECK check_apply_r(L"Apply to R", true);
FILTER_ITEM_CHECK toggle_gui_check(L"プラグインGUIを表示", false);
FILTER_ITEM_FILE instance_id_param(L"__INSTANCE_ID__", L"", L"");
struct InstanceData {
    char uuid[40] = { 0 };
};
FILTER_ITEM_DATA<InstanceData> instance_data_param(L"INSTANCE_ID");

void* filter_items_host[] = {
    &plugin_path_param,
    &track_wet,
    &track_volume,
    &check_apply_l,
    &check_apply_r,
    &toggle_gui_check,
    &instance_id_param,
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

void func_project_save(PROJECT_FILE* pf) {
    std::set<std::string> active_ids_in_project;
    g_active_ids_collector = &active_ids_in_project;
    if (g_edit_handle) {
        g_edit_handle->call_edit_section(collect_active_ids_proc);
    }
    g_active_ids_collector = nullptr;

    std::string all_data_str = PluginManager::GetInstance().PrepareProjectState(active_ids_in_project);

    if (!all_data_str.empty()) {
        pf->set_param_string("AudioHostStateDB", all_data_str.c_str());
        DbgPrint("Saved project state, size: %zu", all_data_str.size());
    }
    else {
        pf->set_param_string("AudioHostStateDB", nullptr);
    }
}

void func_project_load(PROJECT_FILE* pf) {
    CleanupMainFilterResources();

    LPCSTR all_data_str = pf->get_param_string("AudioHostStateDB");
    if (!all_data_str) {
        return;
    }
    PluginManager::GetInstance().LoadProjectState(all_data_str);
    DbgPrint("Loaded project state, size: %zu", strlen(all_data_str));
}

bool func_proc_audio_host(FILTER_PROC_AUDIO* audio) {
    std::string instance_id;
    bool is_migrated = false;

    if (instance_data_param.value->uuid[0] != '\0') {
        instance_id = instance_data_param.value->uuid;
    }
    else {
        LPCWSTR legacy_id_w = instance_id_param.value;
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
    float wet_val = static_cast<float>(track_wet.value);
    float vol_val = static_cast<float>(track_volume.value);
    bool apply_l = check_apply_l.value;
    bool apply_r = check_apply_r.value;

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

    auto host = PluginManager::GetInstance().GetHost(effect_id);
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

    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    thread_local std::vector<float> inL, inR, outL, outR;
    if (inL.size() < total_samples) {
        inL.resize(total_samples); outL.resize(total_samples);
        inR.resize(total_samples); outR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(inL.data(), 0);
    if (channels >= 2) audio->get_sample_data(inR.data(), 1);
    else if (channels == 1) std::copy(inL.begin(), inL.begin() + total_samples, inR.begin());

    std::shared_ptr<IAudioPluginHost> host_for_audio = PluginManager::GetInstance().GetHost(effect_id);

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
            std::fill(outL.begin(), outL.begin() + total_samples, 0.0f);
            if (channels >= 2) std::fill(outR.begin(), outR.begin() + total_samples, 0.0f);
        }
        else {
            for (int i = 0; i < total_samples; ++i) {
                outL[i] = inL[i] * vol_ratio;
                if (channels >= 2) outR[i] = inR[i] * vol_ratio;
            }
        }
        if (channels >= 1) audio->set_sample_data(outL.data(), 0);
        if (channels >= 2) audio->set_sample_data(outR.data(), 1);
        return true;
    }

    if (host_for_audio) {
        if (PluginManager::GetInstance().ShouldReset(effect_id, audio->object->sample_index, audio->object->sample_num)) {
            host_for_audio->Reset();
        }
        PluginManager::GetInstance().UpdateLastAudioState(effect_id, audio->object->sample_index, audio->object->sample_num);

        host_for_audio->ProcessAudio(inL.data(), inR.data(), outL.data(), outR.data(), total_samples, channels);
    }
    else {
        std::copy(inL.begin(), inL.begin() + total_samples, outL.begin());
        if (channels >= 2) std::copy(inR.begin(), inR.begin() + total_samples, outR.begin());
    }

    float wet_ratio = wet_val / 100.0f;
    float dry_ratio = 1.0f - wet_ratio;
    float vol_ratio = vol_val / 100.0f;

    for (int i = 0; i < total_samples; ++i) {
        if (apply_l) {
            outL[i] = (outL[i] * wet_ratio + inL[i] * dry_ratio) * vol_ratio;
        }
        else {
            outL[i] = inL[i] * vol_ratio;
        }

        if (channels >= 2) {
            if (apply_r) {
                outR[i] = (outR[i] * wet_ratio + inR[i] * dry_ratio) * vol_ratio;
            }
            else {
                outR[i] = inR[i] * vol_ratio;
            }
        }
    }

    if (channels >= 1) audio->set_sample_data(outL.data(), 0);
    if (channels >= 2) audio->set_sample_data(outR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_host = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    filter_name,
    L"音声効果",
    []() {
        static std::wstring s = std::regex_replace(filter_info, std::wregex(regex_info_name), FILTER_NAME);
        return s.c_str();
    }(),
    filter_items_host,
    nullptr,
    func_proc_audio_host
};
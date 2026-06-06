#pragma once
#define _USE_MATH_DEFINES
#include "Eap2Info.h"
#include "cache2.h"
#include "config2.h"
#include "filter2.h"
#include "logger2.h"
#include "module2.h"
#include "plugin2.h"

#include <array>
#include <functional>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <tchar.h>
#include <variant>
#include <vector>
#include <windows.h>

#define GENERATE_STR_REPLACE(SRC_STR, REGEX_PATTERN, REPLACEMENT) []() {                          \
    static std::wstring s = std::regex_replace(SRC_STR, std::wregex(REGEX_PATTERN), REPLACEMENT); \
    return s.c_str();                                                                             \
}()

#define GEN_TOOL_NAME(NAME) GENERATE_STR_REPLACE(tool_name, regex_tool_name, NAME)
#define GEN_FILTER_INFO(NAME) GENERATE_STR_REPLACE(filter_info, regex_info_name, NAME)

extern HINSTANCE g_hinstance;
extern LOG_HANDLE* g_log_handle;
extern EDIT_HANDLE* g_edit_handle;
extern CONFIG_HANDLE* g_config_handle;
extern CACHE_HANDLE* g_cache_handle;

enum LOG_TYPE {
    LOG_NONE,
    LOG_VERBOSE,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

inline void DbgPrint(const std::wstring& message, std::optional<LOG_TYPE> log_type = std::nullopt) {
#ifdef _DEBUG
    std::wstring debug_string_type = L"";
    switch (log_type.value_or(LOG_INFO)) {
        case LOG_NONE:
            debug_string_type = L"[Log] ";
            break;
        case LOG_VERBOSE:
            debug_string_type = L"[Verbose] ";
            break;
        case LOG_INFO:
            debug_string_type = L"[Info] ";
            break;
        case LOG_WARN:
            debug_string_type = L"[Warn] ";
            break;
        case LOG_ERROR:
            debug_string_type = L"[Error] ";
            break;
        default:
            debug_string_type = L"[Info] ";
            break;
    }
    OutputDebugString((L"[External Audio Processing 2] " + debug_string_type + message).c_str());
#endif
    switch (log_type.value_or(LOG_INFO)) {
        case LOG_NONE:
            g_log_handle->log(g_log_handle, message.c_str());
            break;
        case LOG_VERBOSE:
            g_log_handle->verbose(g_log_handle, message.c_str());
            break;
        case LOG_INFO:
            g_log_handle->info(g_log_handle, message.c_str());
            break;
        case LOG_WARN:
            g_log_handle->warn(g_log_handle, message.c_str());
            break;
        case LOG_ERROR:
            g_log_handle->error(g_log_handle, message.c_str());
            break;
        default:
            g_log_handle->info(g_log_handle, message.c_str());
            break;
    }
}

inline LPCWSTR TrText(LPCWSTR text) {
    if (!text || !g_config_handle || !g_config_handle->translate) {
        return text;
    }
    LPCWSTR translated = g_config_handle->translate(g_config_handle, text);
    return translated ? translated : text;
}

inline Version parseVersion(std::wstring_view v) {
    Version res;
    try {
        size_t txt_v = v.find(L'v');
        if (txt_v != std::wstring_view::npos) v.remove_prefix(txt_v + 1);
        size_t pos = 0;
        std::wstring ws(v);
        res.major = static_cast<uint16_t>(std::stoi(ws, &pos));
        ws = ws.substr(pos + 1);
        res.minor = static_cast<uint16_t>(std::stoi(ws, &pos));
        ws = ws.substr(pos + 1);
        res.patch = static_cast<uint16_t>(std::stoi(ws, &pos));
        if (pos < ws.size()) res.letter = static_cast<uint16_t>(ws[pos]);
        else res.letter = 0;
    } catch (...) {
        DbgPrint(L"Failed parse version", LOG_ERROR);
    }
    return res;
}

extern std::atomic<double> g_shared_bpm;
extern std::atomic<int32_t> g_shared_ts_num;
extern std::atomic<int32_t> g_shared_ts_denom;

#define TYPE_AUDIO_FILTER_OBJECT FILTER_PLUGIN_TABLE::FLAG_AUDIO | FILTER_PLUGIN_TABLE::FLAG_FILTER
#define TYPE_VIDEO_MEDIA FILTER_PLUGIN_TABLE::FLAG_VIDEO | FILTER_PLUGIN_TABLE::FLAG_INPUT
#define TYPE_AUDIO_MEDIA FILTER_PLUGIN_TABLE::FLAG_AUDIO | FILTER_PLUGIN_TABLE::FLAG_INPUT

extern std::mutex g_task_queue_mutex;
extern std::vector<std::function<void()>> g_main_thread_tasks;

extern FILTER_PLUGIN_TABLE filter_plugin_table_host;
extern FILTER_PLUGIN_TABLE filter_plugin_table_host_media;
extern FILTER_PLUGIN_TABLE filter_plugin_table_utility;
extern FILTER_PLUGIN_TABLE filter_plugin_table_eq;
extern FILTER_PLUGIN_TABLE filter_plugin_table_stereo;
extern FILTER_PLUGIN_TABLE filter_plugin_table_dynamics;
extern FILTER_PLUGIN_TABLE filter_plugin_table_spatial;
extern FILTER_PLUGIN_TABLE filter_plugin_table_modulation;
extern FILTER_PLUGIN_TABLE filter_plugin_table_distortion;
extern FILTER_PLUGIN_TABLE filter_plugin_table_maximizer;
extern FILTER_PLUGIN_TABLE filter_plugin_table_chain_send;
extern FILTER_PLUGIN_TABLE filter_plugin_table_chain_comp;
extern FILTER_PLUGIN_TABLE filter_plugin_table_chain_gate;
extern FILTER_PLUGIN_TABLE filter_plugin_table_chain_dyn_eq;
extern FILTER_PLUGIN_TABLE filter_plugin_table_chain_filter;
extern FILTER_PLUGIN_TABLE filter_plugin_table_reverb;
extern FILTER_PLUGIN_TABLE filter_plugin_table_phaser;
extern FILTER_PLUGIN_TABLE filter_plugin_table_generator;
extern FILTER_PLUGIN_TABLE filter_plugin_table_generator2;
extern FILTER_PLUGIN_TABLE filter_plugin_table_pitch_shift;
extern FILTER_PLUGIN_TABLE filter_plugin_table_autowah;
extern FILTER_PLUGIN_TABLE filter_plugin_table_deesser;
extern FILTER_PLUGIN_TABLE filter_plugin_table_spectral_gate;
extern FILTER_PLUGIN_TABLE filter_plugin_table_midi_visualizer;
extern FILTER_PLUGIN_TABLE filter_plugin_table_notes_send_media;
extern FILTER_PLUGIN_TABLE filter_plugin_table_midi_gen;
extern FILTER_PLUGIN_TABLE filter_plugin_table_reverb2;
extern SCRIPT_MODULE_FUNCTION module_funcs[];

void LoadConfig();
void ReloadConfig();
void SaveConfig();
void ResetConfig();
void OpenConfig();

void CleanupSpectralGateResources();
void CleanupSpatialResources();
void CleanupReverbResources();
void CleanupReverbResources2();
void CleanupPitchShiftResources();
void CleanupPhaserResources();
void CleanupModulationResources();
void CleanupGeneratorResources();
void CleanupMaximizerResources();
void CleanupEQResources();
void CleanupDistortionResources();
void CleanupDeEsserResources();
void CleanupDynamicsResources();
void CleanupChainGateResources();
void CleanupChainFilterResources();
void CleanupChainDynEQResources();
void CleanupChainCompResources();
void CleanupAutoWahResources();
void CleanupMidiVisualizerResources();
void CleanupGeneratorResources2();
void CleanupMidiGeneratorResources();

void ToolCleanupResources();
void CleanupMainFilterResources();
void func_project_save(PROJECT_FILE* pf);
void func_project_load(PROJECT_FILE* pf);
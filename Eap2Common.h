#pragma once
#include <windows.h>
#include <mutex>
#include <vector>
#include <functional>
#include <tchar.h>
#include <regex>
#include "filter2.h"
#include "plugin2.h"
#include "logger2.h"

#if _MSC_VER >= 1950
#define VS_VERSION 2026
#elif _MSC_VER >= 1930
#define VS_VERSION 2022
#elif _MSC_VER >= 1920
#define VS_VERSION 2019
#elif _MSC_VER >= 1910
#define VS_VERSION 2017
#else
#define VS_VERSION -1
#endif

#define GENERATE_STR_REPLACE(SRC_STR, REGEX_PATTERN, REPLACEMENT) []() { \
    static std::wstring s = std::regex_replace(SRC_STR, std::wregex(REGEX_PATTERN), REPLACEMENT); \
    return s.c_str(); \
}()

#define GEN_TOOL_NAME(NAME) GENERATE_STR_REPLACE(tool_name, regex_tool_name, NAME)
#define GEN_FILTER_INFO(NAME) GENERATE_STR_REPLACE(filter_info, regex_info_name, NAME)

extern LOG_HANDLE* g_logger;

#ifdef _DEBUG
#define DbgPrint(format, ...) do { \
    TCHAR b[1024]; \
    _stprintf_s(b, 1024, _T("[External Audio Processing 2] ") _T(format) _T("\n"), ##__VA_ARGS__); \
    OutputDebugString(b); \
    if (g_logger && g_logger->verbose) { \
        g_logger->verbose(g_logger, b); \
    } \
} while (0)
#else
#define DbgPrint(format, ...) do { \
    if (g_logger && g_logger->verbose) { \
        TCHAR b[1024]; \
        _stprintf_s(b, 1024, _T("[External Audio Processing 2] ") _T(format) _T("\n"), ##__VA_ARGS__); \
        g_logger->verbose(g_logger, b); \
    } \
} while (0)
#endif

extern HINSTANCE g_hinstance;
extern EDIT_HANDLE* g_edit_handle;

extern const wchar_t regex_info_name[];
extern const wchar_t regex_tool_name[];
extern const wchar_t filter_name[];
extern const wchar_t filter_info[];
extern const wchar_t filter_name_media[];
extern const wchar_t tool_name[];
extern const wchar_t label[];

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
extern FILTER_PLUGIN_TABLE filter_plugin_table_pitch_shift;
extern FILTER_PLUGIN_TABLE filter_plugin_table_autowah;
extern FILTER_PLUGIN_TABLE filter_plugin_table_deesser;
extern FILTER_PLUGIN_TABLE filter_plugin_table_spectral_gate;
extern FILTER_PLUGIN_TABLE filter_plugin_table_midi_visualizer;
extern FILTER_PLUGIN_TABLE filter_plugin_table_notes_send_media;

void CleanupSpectralGateResources();
void CleanupSpatialResources();
void CleanupReverbResources();
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

void ToolCleanupResources();
void CleanupMainFilterResources();
void func_project_save(PROJECT_FILE* pf);
void func_project_load(PROJECT_FILE* pf);
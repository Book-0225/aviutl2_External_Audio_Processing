#include "Eap2Common.h"
#include "AudioPluginFactory.h"

#define STR2(x) L#x

#define VST_ATTRIBUTION L"VST is a registered trademark of Steinberg Media Technologies GmbH."
#define PLUGIN_VERSION L"v2-0.0.20"
#ifdef _DEBUG
#define DEBUG_PREFIX L"-dev"
#else
#define DEBUG_PREFIX L""
#endif
#define PLUGIN_AUTHOR L"BOOK25"
#define FILTER_NAME L"External Audio Processing 2"
#define FILTER_NAME_SHORT L"EAP2"
#define REGEX_FILTER_NAME L"filter_name"
#define REGEX_TOOL_NAME L"tool_name"
#define MINIMUM_VERSION 2002400
#define RECOMMENDED_VS_VERSION 2026

#define FILTER_NAME_MEDIA_FMT(name) (name L" (Media)")
#define TOOL_NAME_FMT(name, regex) (name L" " regex)
#if VS_VSERSION == -1
#define FILTER_INFO_FMT(name, regex, ver, debug, vsver, author) (name L" " regex L" " ver debug L"-VSUnknown by " author)
#elif VS_VERSION != RECOMMENDED_VS_VERSION
#define FILTER_INFO_FMT(name, regex, ver, debug, vsver, author) (name L" " regex L" " ver debug L"-VS" STR2(vsver) L" by " author)
#else
#define FILTER_INFO_FMT(name, regex, ver, debug, author) (name L" " regex L" " ver debug L" by " author)
#endif
#define PLUGIN_INFO_FMT(name, attr) (name L" Info: " attr)

constexpr wchar_t filter_name[] = FILTER_NAME;
constexpr wchar_t filter_name_media[] = FILTER_NAME_MEDIA_FMT(FILTER_NAME);
constexpr wchar_t tool_name[] = TOOL_NAME_FMT(FILTER_NAME, REGEX_TOOL_NAME);
#if VS_VERSION == -1
constexpr wchar_t filter_info[] = FILTER_INFO_FMT(FILTER_NAME, REGEX_FILTER_NAME, PLUGIN_VERSION, DEBUG_PREFIX, PLUGIN_AUTHOR);
#elif VS_VERSION != RECOMMENDED_VS_VERSION
constexpr wchar_t filter_info[] = FILTER_INFO_FMT(FILTER_NAME, REGEX_FILTER_NAME, PLUGIN_VERSION, DEBUG_PREFIX, VS_VERSION, PLUGIN_AUTHOR);
#else
constexpr wchar_t filter_info[] = FILTER_INFO_FMT(FILTER_NAME, REGEX_FILTER_NAME, PLUGIN_VERSION, DEBUG_PREFIX, PLUGIN_AUTHOR);
#endif
constexpr wchar_t plugin_info[] = PLUGIN_INFO_FMT(FILTER_NAME_SHORT, VST_ATTRIBUTION);

constexpr wchar_t regex_info_name[] = REGEX_FILTER_NAME;
constexpr wchar_t regex_tool_name[] = REGEX_TOOL_NAME;
constexpr wchar_t label[] = FILTER_NAME_SHORT;

HINSTANCE g_hinstance = NULL;
EDIT_HANDLE* g_edit_handle = nullptr;
LOG_HANDLE* g_logger = nullptr;

std::mutex g_task_queue_mutex;
std::vector<std::function<void()>> g_main_thread_tasks;
std::vector<std::function<void()>> g_execution_queue;
std::atomic<double> g_shared_bpm{ 120.0 };
std::atomic<int32_t> g_shared_ts_num{ 4 };
std::atomic<int32_t> g_shared_ts_denom{ 4 };

UINT_PTR g_timer_id = 87655;
HWND g_hMessageWindow = NULL;
const uint32_t WM_APP_EXECUTE_TASKS = WM_APP + 100;

LRESULT CALLBACK MessageWndProc(HWND hWnd, uint32_t msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_APP_EXECUTE_TASKS) {
        std::vector<std::function<void()>> tasks_to_run;
        {
            std::lock_guard<std::mutex> lock(g_task_queue_mutex);
            if (!g_execution_queue.empty()) tasks_to_run.swap(g_execution_queue);
        }
        for (const auto& task : tasks_to_run) task();
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (g_edit_handle) {
        EDIT_INFO info = { 0 };
        g_edit_handle->get_edit_info(&info, sizeof(info));
        g_shared_bpm.store(info.grid_bpm_tempo > 0 ? info.grid_bpm_tempo : 120.0);
        if (info.grid_bpm_beat > 0) {
            g_shared_ts_num.store(info.grid_bpm_beat);
            g_shared_ts_denom.store(4);
        }
    }
    std::lock_guard<std::mutex> lock(g_task_queue_mutex);
    if (g_main_thread_tasks.empty()) return;

    g_execution_queue.insert(
        g_execution_queue.end(),
        std::make_move_iterator(g_main_thread_tasks.begin()),
        std::make_move_iterator(g_main_thread_tasks.end())
    );
    g_main_thread_tasks.clear();

    if (g_hMessageWindow) PostMessage(g_hMessageWindow, WM_APP_EXECUTE_TASKS, 0, 0);
}

BOOL APIENTRY DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hinstance = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
    if (version < MINIMUM_VERSION) { 
        MessageBox(NULL, L"AviUtl2のバージョンが古すぎます。", L"EAP2 Error", MB_OK | MB_ICONERROR);
        return false;
    }

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
        MessageBox(NULL, L"COM 初期化に失敗しました。", L"EAP2 Error", MB_OK | MB_ICONERROR);
        return false;
    }

    if (!AudioPluginFactory::Initialize(g_hinstance)) {
        CoUninitialize();
        MessageBox(NULL, L"Audio Plugin Factory の初期化に失敗しました。", L"EAP2 Error", MB_OK | MB_ICONERROR);
        return false;
    }

    WNDCLASS wc = {};
    wc.lpfnWndProc = MessageWndProc;
    wc.hInstance = g_hinstance;
    wc.lpszClassName = _T("EAP2_MessageWindowClass");
    if (!RegisterClass(&wc)) {
        AudioPluginFactory::Uninitialize();
        CoUninitialize();
        return false;
    }

    g_hMessageWindow = CreateWindow(wc.lpszClassName, _T("EAP2 Message Window"), 
                                    0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_hinstance, NULL);
    if (!g_hMessageWindow) {
        UnregisterClass(_T("EAP2_MessageWindowClass"), g_hinstance);
        AudioPluginFactory::Uninitialize();
        CoUninitialize();
        MessageBox(NULL, L"メッセージウィンドウの作成に失敗しました。", L"EAP2 Error", MB_OK | MB_ICONERROR);
        return false;
    }

    SetTimer(NULL, g_timer_id, 50, TimerProc);
    
    DbgPrint("EAP2 Initialized Successfully.");
    return true;
}

void ToolCleanupResources() {
    CleanupSpectralGateResources();
    CleanupSpatialResources();
    CleanupReverbResources();
    CleanupPitchShiftResources();
    CleanupPhaserResources();
    CleanupModulationResources();
    CleanupGeneratorResources();
    CleanupMaximizerResources();
    CleanupEQResources();
    CleanupDistortionResources();
    CleanupDeEsserResources();
    CleanupDynamicsResources();
    CleanupChainGateResources();
    CleanupChainFilterResources();
    CleanupChainDynEQResources();
    CleanupChainCompResources();
    CleanupAutoWahResources();
    CleanupMidiVisualizerResources();
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
    KillTimer(NULL, g_timer_id);

    if (g_hMessageWindow) {
        DestroyWindow(g_hMessageWindow);
        g_hMessageWindow = NULL;
    }
    UnregisterClass(_T("EAP2_MessageWindowClass"), g_hinstance);
    CleanupMainFilterResources();
    AudioPluginFactory::Uninitialize();
    CoUninitialize();
    
    DbgPrint("EAP2 Uninitialized.");
}

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) {
    g_logger = logger;
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    host->set_plugin_information(plugin_info);
    host->register_filter_plugin(&filter_plugin_table_host);
    host->register_filter_plugin(&filter_plugin_table_host_media);
    host->register_filter_plugin(&filter_plugin_table_utility);
    host->register_filter_plugin(&filter_plugin_table_eq);
    host->register_filter_plugin(&filter_plugin_table_stereo);
    host->register_filter_plugin(&filter_plugin_table_dynamics);
    host->register_filter_plugin(&filter_plugin_table_spatial);
    host->register_filter_plugin(&filter_plugin_table_modulation);
    host->register_filter_plugin(&filter_plugin_table_distortion);
    host->register_filter_plugin(&filter_plugin_table_maximizer);
    host->register_filter_plugin(&filter_plugin_table_chain_send);
    host->register_filter_plugin(&filter_plugin_table_chain_comp);
    host->register_filter_plugin(&filter_plugin_table_chain_gate);
    host->register_filter_plugin(&filter_plugin_table_chain_dyn_eq);
    host->register_filter_plugin(&filter_plugin_table_chain_filter);
    host->register_filter_plugin(&filter_plugin_table_reverb);
    host->register_filter_plugin(&filter_plugin_table_phaser);
    host->register_filter_plugin(&filter_plugin_table_generator);
    host->register_filter_plugin(&filter_plugin_table_pitch_shift);
    host->register_filter_plugin(&filter_plugin_table_autowah);
    host->register_filter_plugin(&filter_plugin_table_deesser);
    host->register_filter_plugin(&filter_plugin_table_spectral_gate);
    host->register_filter_plugin(&filter_plugin_table_midi_visualizer);
    host->register_filter_plugin(&filter_plugin_table_notes_send_media);
    host->register_project_save_handler(func_project_save);
    host->register_project_load_handler(func_project_load);
    g_edit_handle = host->create_edit_handle();
}
#include "Eap2Common.h"
#include "Eap2Config.h"
#include "AudioPluginFactory.h"
#include <unordered_set>

#define STR2(x) L#x

#define VST_ATTRIBUTION L"VST is a registered trademark of Steinberg Media Technologies GmbH."
#define PLUGIN_VERSION L"v2-0.0.26"
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
#define MINIMUM_VERSION 2003300
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
constexpr wchar_t plugin_version[] = PLUGIN_VERSION;

static constexpr std::array all_plugins{
    &filter_plugin_table_host,
    &filter_plugin_table_host_media,
    &filter_plugin_table_utility,
    &filter_plugin_table_eq,
    &filter_plugin_table_stereo,
    &filter_plugin_table_dynamics,
    &filter_plugin_table_spatial,
    &filter_plugin_table_modulation,
    &filter_plugin_table_distortion,
    &filter_plugin_table_maximizer,
    &filter_plugin_table_chain_send,
    &filter_plugin_table_chain_comp,
    &filter_plugin_table_chain_gate,
    &filter_plugin_table_chain_dyn_eq,
    &filter_plugin_table_chain_filter,
    &filter_plugin_table_reverb,
    &filter_plugin_table_phaser,
    &filter_plugin_table_generator,
    &filter_plugin_table_pitch_shift,
    &filter_plugin_table_autowah,
    &filter_plugin_table_deesser,
    &filter_plugin_table_spectral_gate,
    &filter_plugin_table_midi_visualizer,
    &filter_plugin_table_notes_send_media
};

static constexpr std::array tool_plugins{
    &filter_plugin_table_utility,
    & filter_plugin_table_eq,
    & filter_plugin_table_stereo,
    & filter_plugin_table_dynamics,
    & filter_plugin_table_spatial,
    & filter_plugin_table_modulation,
    & filter_plugin_table_distortion,
    & filter_plugin_table_maximizer,
    & filter_plugin_table_chain_send,
    & filter_plugin_table_chain_comp,
    & filter_plugin_table_chain_gate,
    & filter_plugin_table_chain_dyn_eq,
    & filter_plugin_table_chain_filter,
    & filter_plugin_table_reverb,
    & filter_plugin_table_phaser,
    & filter_plugin_table_generator,
    & filter_plugin_table_pitch_shift,
    & filter_plugin_table_autowah,
    & filter_plugin_table_deesser,
    & filter_plugin_table_spectral_gate,
    & filter_plugin_table_midi_visualizer,
    & filter_plugin_table_notes_send_media
};

static constexpr std::array host_plugins{
    &filter_plugin_table_host,
    &filter_plugin_table_host_media
};

static constexpr std::array chain_plugins{
    &filter_plugin_table_chain_send,
    &filter_plugin_table_chain_comp,
    &filter_plugin_table_chain_gate,
    &filter_plugin_table_chain_dyn_eq,
    &filter_plugin_table_chain_filter
};

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

template <typename T, std::size_t N>
std::vector<T> GetModule(const std::array<T, N>& plugins, AppSettings setting) {
    std::unordered_set<T> disable_set;
    auto add_if = [&](bool condition, auto target) {
        if (condition) {
            if constexpr (std::is_pointer_v<decltype(target)>) disable_set.insert(target);
            else disable_set.insert(std::begin(target), std::end(target));
        }
        };
    add_if(setting.module.all_tool_disable, tool_plugins);
    add_if(setting.module.host_disable, host_plugins);
    add_if(setting.module.chain_tool_disable, chain_plugins);
    add_if(setting.module.host_filter_disable, &filter_plugin_table_host);
    add_if(setting.module.host_media_disable, &filter_plugin_table_host_media);
    add_if(setting.module.auto_wah_disable, &filter_plugin_table_autowah);
    add_if(setting.module.chain_comp_disable, &filter_plugin_table_chain_comp);
    add_if(setting.module.chain_dynamic_eq_disable, &filter_plugin_table_chain_dyn_eq);
    add_if(setting.module.chain_filter_disable, &filter_plugin_table_chain_filter);
    add_if(setting.module.chain_gate_disable, &filter_plugin_table_chain_gate);
    add_if(setting.module.chain_send_disable, &filter_plugin_table_chain_send);
    add_if(setting.module.deesser_disable, &filter_plugin_table_deesser);
    add_if(setting.module.distortion_disable, &filter_plugin_table_distortion);
    add_if(setting.module.dynamics_disable, &filter_plugin_table_dynamics);
    add_if(setting.module.eq_disable, &filter_plugin_table_eq);
    add_if(setting.module.generator_disable, &filter_plugin_table_generator);
    add_if(setting.module.maximizer_disable, &filter_plugin_table_maximizer);
    add_if(setting.module.modulation_disable, &filter_plugin_table_modulation);
    add_if(setting.module.notes_send_disable, &filter_plugin_table_notes_send_media);
    add_if(setting.module.phaser_disable, &filter_plugin_table_phaser);
    add_if(setting.module.pitch_shift_disable, &filter_plugin_table_pitch_shift);
    add_if(setting.module.reverb_disable, &filter_plugin_table_reverb);
    add_if(setting.module.spatial_disable, &filter_plugin_table_spatial);
    add_if(setting.module.spectral_gate_disable, &filter_plugin_table_spectral_gate);
    add_if(setting.module.stereo_disable, &filter_plugin_table_stereo);
    add_if(setting.module.utility_disable, &filter_plugin_table_utility);
    std::vector<T> registry;
    registry.reserve(plugins.size());
    for (const auto& p : plugins) if (disable_set.find(p) == disable_set.end()) registry.push_back(p);
    if (setting.general.enable_experimental) {
        if (setting.exp.use_experimental_generator) std::replace(registry.begin(), registry.end(), &filter_plugin_table_generator, &filter_plugin_table_generator2);
        if (!setting.module.all_tool_disable && setting.exp.enable_experimental_midi_generator) registry.push_back(&filter_plugin_table_midi_gen);
        if (setting.exp.use_experimental_reverb) std::replace(registry.begin(), registry.end(), &filter_plugin_table_reverb, &filter_plugin_table_reverb2);
    }
    return registry;
}

BOOL APIENTRY DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hinstance = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return true;
}

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
    // RequiredVersion()実装前のバージョン用
    if (version < 2003300) {
        MessageBox(NULL, L"AviUtl2のバージョンが古すぎます。", L"EAP2 Error", MB_OK | MB_ICONERROR);
        return false;
    }

    LoadConfig();
    
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
    CleanupReverbResources2();
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
    CleanupGeneratorResources2();
    CleanupMidiGeneratorResources();
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

    // SaveConfig(); 将来的にAviUtl内で設定を変更出来るようにした時用
    
    DbgPrint("EAP2 Uninitialized.");
}

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) {
    g_logger = logger;
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    host->set_plugin_information(plugin_info);
    host->register_config_menu(L"EAP2の設定を再読込", [](HWND hwnd, HINSTANCE dllhinst) { if (MessageBox(NULL, L"EAP2の設定を再読込しますか？(一部は再起動後に反映)", L"EAP2 設定再読込", MB_OKCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON2) == IDOK) ReloadConfig(); });
    host->register_config_menu(L"EAP2の設定をリセット", [](HWND hwnd, HINSTANCE dllhinst) { if (MessageBox(NULL, L"EAP2の設定をリセットしますか？(再起動後に反映)", L"EAP2 設定リセット", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK) ResetConfig(); });
    host->register_config_menu(L"EAP2の設定を開く", [](HWND hwnd, HINSTANCE dllhinst) { OpenConfig(); });
    for (auto& plugin : GetModule(all_plugins, settings)) host->register_filter_plugin(plugin);
    host->register_project_save_handler(func_project_save);
    host->register_project_load_handler(func_project_load);
    host->register_clear_cache_handler([](EDIT_SECTION* edit) { CleanupMainFilterResources(); });
    g_edit_handle = host->create_edit_handle();
}

EXTERN_C __declspec(dllexport) DWORD RequiredVersion() {
    return MINIMUM_VERSION;
}
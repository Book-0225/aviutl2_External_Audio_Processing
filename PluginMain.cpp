#include "AudioPluginFactory.h"
#include "Eap2Common.h"
#include "Eap2Config.h"

#include <unordered_set>

#define STR2(x) L#x

#define VST_ATTRIBUTION L"VST is a registered trademark of Steinberg Media Technologies GmbH."
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
#define MINIMUM_VERSION 2010301
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

constexpr wchar_t EAP2_MW_CLASS[] = L"EAP2_MessageWindowClass";

COMMON_PLUGIN_TABLE common_plugin_table = {
    filter_name,
    plugin_info,
};

SCRIPT_MODULE_TABLE script_module_table = {
    GEN_FILTER_INFO(L"Module"),
    module_funcs
};

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
    &filter_plugin_table_notes_send_media,
    &filter_plugin_table_midi_gen,
    &filter_plugin_table_convolution_reverb
};

static constexpr std::array tool_plugins{
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
    &filter_plugin_table_notes_send_media,
    &filter_plugin_table_midi_gen,
    &filter_plugin_table_convolution_reverb
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

HINSTANCE g_hinstance = nullptr;
EDIT_HANDLE* g_edit_handle = nullptr;
LOG_HANDLE* g_log_handle = nullptr;
CONFIG_HANDLE* g_config_handle = nullptr;
CACHE_HANDLE* g_cache_handle = nullptr;
HWND g_host_hwnd = nullptr;

std::mutex g_task_queue_mutex;
std::vector<std::function<void()>> g_main_thread_tasks;
std::vector<std::function<void()>> g_execution_queue;

UINT_PTR g_timer_id = 87655;
HWND g_hMessageWindow = nullptr;
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
    std::lock_guard<std::mutex> lock(g_task_queue_mutex);
    if (g_main_thread_tasks.empty()) return;

    g_execution_queue.insert(
        g_execution_queue.end(),
        std::make_move_iterator(g_main_thread_tasks.begin()),
        std::make_move_iterator(g_main_thread_tasks.end()));
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
    add_if(setting.module.midi_gen_disable, &filter_plugin_table_midi_gen);
    std::vector<T> registry;
    registry.reserve(plugins.size());
    for (const auto& p : plugins)
        if (disable_set.find(p) == disable_set.end()) registry.push_back(p);
    if (setting.compat.use_new_generator) std::replace(registry.begin(), registry.end(), &filter_plugin_table_generator, &filter_plugin_table_generator2);
    if (setting.compat.use_new_reverb) std::replace(registry.begin(), registry.end(), &filter_plugin_table_reverb, &filter_plugin_table_reverb2);
    if (setting.general.enable_experimental) {
        // expのやつをここで置き換え反映する
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
    CleanupConvolutionReverbResources();
}

void func_proc_file_drop_plugin(EDIT_SECTION* edit, LPCWSTR file) {
    std::filesystem::path path = file;
    std::string utf8_path = path.u8string();
    EDIT_INFO* info = edit->info;
    int32_t layer = info->layer;
    int32_t frame = info->frame;
    int32_t layer_max = info->layer_max;
    std::wstring target_name = filter_plugin_table_host.name;
    int32_t mode = 1;
    edit->get_mouse_layer_frame(&layer, &frame);
    for (; layer <= layer_max; layer++)
        if (!edit->find_object(layer, frame))
            break;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        mode = 0;
    else if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        mode = 2;
    if (mode == 1)
        target_name = filter_plugin_table_host_media.name;
    OBJECT_HANDLE obj = edit->create_object(target_name.c_str(), layer, frame, 30);
    if (!obj)
        return;
    if (mode == 0) {
        std::string alias = edit->get_object_alias(obj);
        edit->delete_object(obj);
        std::string target_name_str = StringUtils::WideToUtf8(target_name);
        std::string target = "[Object.0]\r\neffect.name=" + target_name_str;
        std::string replacement =
            "[Object.0]\r\neffect.name=" + StringUtils::WideToUtf8(L"フィルタオブジェクト") + "\r\n"
                                                                                              "[Object.1]\r\neffect.name=" +
            target_name_str;
        size_t pos = alias.find(target);
        if (pos != std::string::npos)
            alias.replace(pos, target.length(), replacement);
        obj = edit->create_object_from_alias(alias.c_str(), layer, frame, 30);
        if (!obj)
            return;
    }
    edit->set_object_item_value(obj, target_name.c_str(), L"プラグイン", utf8_path.c_str());
}

void func_proc_file_drop_midi(EDIT_SECTION* edit, LPCWSTR file) {
    std::filesystem::path path = file;
    std::string utf8_path = path.u8string();
    EDIT_INFO* info = edit->info;
    int32_t layer = info->layer;
    int32_t frame = info->frame;
    int32_t layer_max = info->layer_max;
    std::wstring target_name = filter_plugin_table_midi_gen.name;
    int32_t mode = 0;
    edit->get_mouse_layer_frame(&layer, &frame);
    for (; layer <= layer_max; layer++)
        if (!edit->find_object(layer, frame))
            break;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000 || GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        target_name = filter_plugin_table_midi_visualizer.name;
        mode = 1;
    }
    OBJECT_HANDLE obj = edit->create_object(target_name.c_str(), layer, frame, 30);
    if (!obj)
        return;
    edit->set_object_item_value(obj, target_name.c_str(), mode ? L"MIDI File" : L"MIDIファイル", utf8_path.c_str());
}

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
    // RequiredVersion()実装前のバージョン用
    if (version < 2003300) {
        std::wstring message = std::wstring(L"AviUtl2のバージョンが古すぎます。\n最低バージョン: ") + std::to_wstring(MINIMUM_VERSION);
        MessageBox(NULL, message.c_str(), L"EAP2 Error", MB_OK | MB_ICONERROR);
        return false;
    }

    LoadConfig();

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        DbgMessage(TrText(L"COM 初期化に失敗しました。"), LOG_ERROR);
        return false;
    }

    if (!AudioPluginFactory::Initialize(g_hinstance)) {
        CoUninitialize();
        DbgMessage(TrText(L"Audio Plugin Factory の初期化に失敗しました。"), LOG_ERROR);
        return false;
    }

    WNDCLASS wc = {};
    wc.lpfnWndProc = MessageWndProc;
    wc.hInstance = g_hinstance;
    wc.lpszClassName = EAP2_MW_CLASS;
    if (!RegisterClass(&wc)) {
        AudioPluginFactory::Uninitialize();
        CoUninitialize();
        return false;
    }

    g_hMessageWindow = CreateWindow(wc.lpszClassName, L"EAP2 Message Window", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hinstance, nullptr);
    if (!g_hMessageWindow) {
        UnregisterClass(EAP2_MW_CLASS, g_hinstance);
        AudioPluginFactory::Uninitialize();
        CoUninitialize();
        DbgMessage(TrText(L"メッセージウィンドウの作成に失敗しました。"), LOG_ERROR);
        return false;
    }

    SetTimer(nullptr, g_timer_id, 50, TimerProc);
    DbgPrint(TrText(L"EAP2の初期化に成功しました。"), LOG_INFO);
    return true;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
    KillTimer(nullptr, g_timer_id);

    if (g_hMessageWindow) {
        DestroyWindow(g_hMessageWindow);
        g_hMessageWindow = nullptr;
    }
    UnregisterClass(EAP2_MW_CLASS, g_hinstance);
    CleanupMainFilterResources();
    AudioPluginFactory::Uninitialize();
    CoUninitialize();

    // SaveConfig(); 将来的にAviUtl内で設定を変更出来るようにした時用

    DbgPrint(TrText(L"EAP2 の終了処理が完了しました。"), LOG_INFO);
}

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) {
    g_log_handle = logger;
}

EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) {
    g_config_handle = handle;
}

EXTERN_C __declspec(dllexport) void InitializeCache(CACHE_HANDLE* cache) {
    g_cache_handle = cache;
}

EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) {
    return &common_plugin_table;
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    if (!settings.general.disable_dropin) {
        host->register_file_drop_handler(TrText(L"EAP2でVST3/CLAPファイルをフィルタオブジェクトとして追加(Ctrlでフィルタ効果, Shiftでメディアオブジェクト)"), L"Audio Plugins (*.vst3;*.clap)\0*.vst3;*.clap\0", &func_proc_file_drop_plugin);
        host->register_file_drop_handler(TrText(L"EAP2でMIDIファイルを再生用オブジェクトとして追加(CtrlまたはShiftで表示用オブジェクト)"), L"MIDI File (*.mid)\0*.mid;*.midi\0", &func_proc_file_drop_midi);
    }
    host->register_config_menu(TrText(L"EAP2の設定を再読込"), [](HWND hwnd, HINSTANCE dllhinst) {
        if (MessageBox(hwnd, TrText(L"EAP2の設定を再読込しますか？(一部は再起動後に反映)"), TrText(L"EAP2 設定再読込"), MB_OKCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON2) == IDOK) ReloadConfig();
    });
    host->register_config_menu(TrText(L"EAP2の設定をリセット"), [](HWND hwnd, HINSTANCE dllhinst) {
        if (MessageBox(hwnd, TrText(L"EAP2の設定をリセットしますか？(再起動後に反映)"), TrText(L"EAP2 設定リセット"), MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK) ResetConfig();
    });
    host->register_config_menu(TrText(L"EAP2の設定を開く"), [](HWND hwnd, HINSTANCE dllhinst) { OpenConfig(); });
    for (auto& plugin : GetModule(all_plugins, settings)) host->register_filter_plugin(plugin);
    if (settings.exp.use_experimental_script_module) host->register_script_module_name(&script_module_table, L"EAP2_module");
    host->register_project_save_handler(func_project_save);
    host->register_project_load_handler(func_project_load);
    host->register_clear_cache_handler([](EDIT_SECTION* edit) { CleanupMainFilterResources(); });
    g_edit_handle = host->create_edit_handle();
    g_host_hwnd = g_edit_handle->get_host_app_window();
    if (!settings.module.all_tool_disable && !settings.module.analyzer_disable)
        Register_Analyzer(host);
}

EXTERN_C __declspec(dllexport) DWORD RequiredVersion() {
    return MINIMUM_VERSION;
}
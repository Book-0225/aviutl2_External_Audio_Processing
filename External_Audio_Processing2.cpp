#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <atomic>
#include <mutex>
#include <functional>
#include <algorithm>
#include <commdlg.h>
#include <tchar.h>
#include <shlobj.h>
#include <objbase.h>
#include <rpcdce.h>
#include <iterator>

#include "filter2.h"
#include "plugin2.h"
#include "IAudioPluginHost.h"
#include "PluginType.h"
#include "AudioPluginFactory.h"

#ifdef _DEBUG
#define DbgPrint(format, ...) do { TCHAR b[512]; _stprintf_s(b, 512, _T("[External Audio Processing 2] ") _T(format) _T("\n"), ##__VA_ARGS__); OutputDebugString(b); } while (0)
#else
#define DbgPrint(format, ...)
#endif

std::string WideToUtf8(LPCWSTR w) {
    if (!w || !w[0]) return "";
    int s = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    if (s == 0) return "";
    std::string r(s, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &r[0], s, 0, 0);
    r.pop_back();
    return r;
}

std::string GenerateUUID() {
    UUID u; UuidCreate(&u); RPC_CSTR s; UuidToStringA(&u, &s);
    std::string r((char*)s); RpcStringFreeA(&s);
    return r;
}

const int MAX_BLOCK_SIZE = 2048;
EDIT_HANDLE* g_edit_handle = nullptr;
UINT_PTR g_timer_id = 87655;
HINSTANCE g_hinstance = NULL;

HWND g_hMessageWindow = NULL;
const UINT WM_APP_EXECUTE_TASKS = WM_APP + 100;
std::mutex g_task_queue_mutex;
std::vector<std::function<void()>> g_main_thread_tasks;
std::vector<std::function<void()>> g_execution_queue;

std::mutex g_states_mutex;
std::map<int64_t, std::shared_ptr<IAudioPluginHost>> g_hosts;
std::map<std::string, std::string> g_plugin_state_database;

struct LastAudioState {
    int64_t sample_index;
    int sample_num;
};
std::mutex g_last_audio_state_mutex;
std::map<int64_t, LastAudioState> g_last_audio_states;

static std::vector<std::string> g_temp_active_ids;
static int64_t g_target_object_id_for_update = -1;
static std::string g_new_instance_id_for_update;
static std::string g_plugin_path_for_update;

struct InstanceData {
    char uuid[40] = { 0 };
};

#define VST_ATTRIBUTION L"VST is a registered trademark of Steinberg Media Technologies GmbH."
#define PLUGIN_VERSION L"v2-0.0.5-dev"
#define PLUGIN_AUTHOR L"BOOK25"
#define FILTER_NAME L"External Audio Processing 2"
#define FILTER_NAME_SHORT L"EAP2"
#define MIN_VER 2001900
#define FILTER_INFO_FMT(name, ver, author) (name L" filter " ver L" by " author)
#define PLUGIN_INFO_FMT(name, attr) (name L" Info: " attr)

TCHAR filter[] =
L"Audio Plugins (*.vst3;*.clap)\0*.vst3;*.clap\0"
L"VST3 Plugins (*.vst3)\0*.vst3\0"
L"CLAP Plugins (*.clap)\0*.clap\0"
L"All Files (*.*)\0*.*\0\0";

constexpr wchar_t filter_name[] = FILTER_NAME;
constexpr wchar_t filter_info[] = FILTER_INFO_FMT(FILTER_NAME, PLUGIN_VERSION, PLUGIN_AUTHOR);
constexpr wchar_t plugin_info[] = PLUGIN_INFO_FMT(FILTER_NAME_SHORT, VST_ATTRIBUTION);

FILTER_ITEM_FILE plugin_path_param(L"プラグイン", L"", filter);
FILTER_ITEM_CHECK toggle_gui_check(L"プラグインGUIを表示", false);
FILTER_ITEM_FILE instance_id_param(L"__INSTANCE_ID__", L"", L"");
FILTER_ITEM_DATA<InstanceData> instance_data_param(L"INSTANCE_ID");

void* filter_items[] = {
    &plugin_path_param,
    &toggle_gui_check,
    &instance_id_param,
    &instance_data_param,
    nullptr
};

bool func_proc_audio(FILTER_PROC_AUDIO* audio);
void func_project_save(PROJECT_FILE* pf);
void func_project_load(PROJECT_FILE* pf);

FILTER_PLUGIN_TABLE filter_pluginable = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    filter_name,
    L"音声効果",
    filter_info,
    filter_items,
    nullptr,
    func_proc_audio
};

LRESULT CALLBACK MessageWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_APP_EXECUTE_TASKS) {
        std::vector<std::function<void()>> tasks_to_run;
        {
            std::lock_guard<std::mutex> lock(g_task_queue_mutex);
            if (!g_execution_queue.empty()) {
                tasks_to_run.swap(g_execution_queue);
            }
        }
        for (const auto& task : tasks_to_run) {
            task();
        }
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
        std::make_move_iterator(g_main_thread_tasks.end())
    );
    g_main_thread_tasks.clear();

    if (g_hMessageWindow) {
        PostMessage(g_hMessageWindow, WM_APP_EXECUTE_TASKS, 0, 0);
    }
}

void update_instance_id_proc(EDIT_SECTION* edit) {
    if (g_target_object_id_for_update == -1) return;

    OBJECT_HANDLE target_object = edit->get_focus_object();

    if (target_object) {
        for (int i = 0; i < 100; ++i) {
            std::wstring indexed_filter_name = std::wstring(filter_name) + L":" + std::to_wstring(i);

            LPCSTR path_str_c = edit->get_object_item_value(target_object, indexed_filter_name.c_str(), plugin_path_param.name);
            if (!path_str_c) break;
            if (g_plugin_path_for_update != std::string(path_str_c)) continue;

            LPCSTR old_id = edit->get_object_item_value(target_object, indexed_filter_name.c_str(), instance_id_param.name);
            if (old_id && old_id[0] != '\0') {
                edit->set_object_item_value(target_object, indexed_filter_name.c_str(), instance_id_param.name, "");
            }
            break;
        }
    }
    g_target_object_id_for_update = -1;
    g_new_instance_id_for_update.clear();
    g_plugin_path_for_update.clear();
}

bool func_proc_audio(FILTER_PROC_AUDIO* audio) {
    std::wstring plugin_path_w = plugin_path_param.value;
    int64_t effect_id = audio->object->effect_id;

    std::string instance_id;
    bool is_migrated = false;
    if (instance_data_param.value != &instance_data_param.default_value &&
        instance_data_param.value->uuid[0] != '\0')
    {
        instance_id = instance_data_param.value->uuid;
    }
    else
    {
        LPCWSTR legacy_id_w = instance_id_param.value;
        if (legacy_id_w && legacy_id_w[0] != L'\0') {
            instance_id = WideToUtf8(legacy_id_w);

            strcpy_s(instance_data_param.value->uuid, sizeof(instance_data_param.value->uuid), instance_id.c_str());

            is_migrated = true;
        }
        else {
            instance_id = GenerateUUID();
            strcpy_s(instance_data_param.value->uuid, sizeof(instance_data_param.value->uuid), instance_id.c_str());
        }
    }

    if (instance_id.empty()) {
        return true;
    }

    if (is_migrated || (instance_data_param.value == &instance_data_param.default_value)) {
        g_target_object_id_for_update = audio->object->id;
        g_new_instance_id_for_update = instance_id;
        g_plugin_path_for_update = WideToUtf8(plugin_path_w.c_str());

        g_main_thread_tasks.push_back([] {
            g_edit_handle->call_edit_section(update_instance_id_proc);
            });
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(g_states_mutex);
        auto it = g_hosts.find(effect_id);
        bool needs_reinitialization = false;
        bool path_changed = false;

        if (plugin_path_w.empty()) {
            if (it != g_hosts.end()) needs_reinitialization = true;
        }
        else {
            if (it == g_hosts.end()) {
                needs_reinitialization = true;
            }
            else if (it->second->GetPluginPath() != WideToUtf8(plugin_path_w.c_str())) {
                needs_reinitialization = true;
                path_changed = true;
            }
        }

        if (needs_reinitialization) {
            double sampleRate = audio->scene->sample_rate;

            bool isFirstLoad = (it == g_hosts.end());
            if (!isFirstLoad && plugin_path_w.empty()) {
                std::lock_guard<std::mutex> task_lock(g_task_queue_mutex);

                g_main_thread_tasks.push_back([instance_id] {
                    if (instance_id.empty()) return;

                    static std::string target_instance_id_for_callback;
                    target_instance_id_for_callback = instance_id;

                    auto find_and_update_object_proc = [](EDIT_SECTION* edit) {
                        if (target_instance_id_for_callback.empty()) return;

                        OBJECT_HANDLE target_object = nullptr;
                        int max_layer = edit->info->layer_max;

                        for (int layer = 0; layer <= max_layer && !target_object; ++layer) {
                            OBJECT_HANDLE obj = edit->find_object(layer, 0);
                            while (obj != nullptr) {
                                for (int i = 0; i < 100; ++i) {
                                    std::wstring indexed_filter_name = std::wstring(filter_name) + L":" + std::to_wstring(i);
                                    LPCSTR stored_id_str = edit->get_object_item_value(obj, indexed_filter_name.c_str(), instance_id_param.name);
                                    if (!stored_id_str) break;
                                    if (stored_id_str[0] != '\0' && target_instance_id_for_callback == stored_id_str) {
                                        target_object = obj;
                                        break;
                                    }
                                }
                                if (target_object) break;
                                int current_end_frame = edit->get_object_layer_frame(obj).end;
                                obj = edit->find_object(layer, current_end_frame + 1);
                            }
                        }

                        if (target_object) {
                            for (int i = 0; i < 100; ++i) {
                                std::wstring indexed_filter_name = std::wstring(filter_name) + L":" + std::to_wstring(i);
                                LPCSTR stored_id_str = edit->get_object_item_value(target_object, indexed_filter_name.c_str(), instance_id_param.name);
                                if (!stored_id_str) break;
                                if (target_instance_id_for_callback == stored_id_str) {
                                    edit->set_object_item_value(target_object, indexed_filter_name.c_str(), toggle_gui_check.name, "0");
                                    break;
                                }
                            }
                        }
                        };

                    g_edit_handle->call_edit_section(find_and_update_object_proc);
                    });
            }
            {
                std::lock_guard<std::mutex> task_lock(g_task_queue_mutex);
                g_main_thread_tasks.push_back([effect_id, instance_id, plugin_path_w, sampleRate, path_changed]() {
                    std::lock_guard<std::mutex> host_lock(g_states_mutex);

                    g_hosts.erase(effect_id);

                    if (!plugin_path_w.empty()) {
                        auto plugin_type = GetPluginTypeFromPath(plugin_path_w);
                        if (plugin_type != PluginType::Unknown) {
                            auto new_host = AudioPluginFactory::Create(plugin_type, g_hinstance);
                            if (new_host) {
                                if (sampleRate > 0) {
                                    if (new_host->LoadPlugin(WideToUtf8(plugin_path_w.c_str()), sampleRate, MAX_BLOCK_SIZE)) {
                                        if (!path_changed && g_plugin_state_database.count(instance_id)) {
                                            new_host->SetState(g_plugin_state_database[instance_id]);
                                        }
                                        g_hosts[effect_id] = std::move(new_host);
                                    }
                                }
                            }
                        }
                    }
                    });
            }
            return true;
        }
    }

    std::shared_ptr<IAudioPluginHost> host_for_audio;
    {
        std::lock_guard<std::mutex> lock(g_states_mutex);
        auto it = g_hosts.find(effect_id);
        if (it == g_hosts.end()) return true;

        host_for_audio = it->second;
    }

    if (!host_for_audio) return true;

    {
        std::lock_guard<std::mutex> lock(g_last_audio_state_mutex);
        auto it = g_last_audio_states.find(effect_id);
        bool needs_reset = false;
        if (it != g_last_audio_states.end()) {
            const auto& last_state = it->second;
            if (audio->object->sample_index != last_state.sample_index + last_state.sample_num) {
                needs_reset = true;
                DbgPrint("Seek detected for effect_id %lld. Resetting plugin.", effect_id);
            }
        }
        else {
            needs_reset = true;
            DbgPrint("First process for effect_id %lld. Resetting plugin.", effect_id);
        }
        if (needs_reset) {
            host_for_audio->Reset();
        }
        g_last_audio_states[effect_id] = { audio->object->sample_index, audio->object->sample_num };
    }

    bool gui_should_show = toggle_gui_check.value;
    if (host_for_audio->IsGuiVisible() != gui_should_show) {
        std::lock_guard<std::mutex> task_lock(g_task_queue_mutex);
        g_main_thread_tasks.push_back([=, host = host_for_audio]() {
            if (gui_should_show) {
                host->ShowGui();
            }
            else {
                host->HideGui();
                std::string state = host->GetState();
                if (!state.empty()) {
                    std::lock_guard<std::mutex> db_lock(g_states_mutex);
                    g_plugin_state_database[instance_id] = state;
                }
            }
            });
    }

    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    std::vector<float> inL(total_samples), inR(total_samples), outL(total_samples), outR(total_samples);
    if (channels >= 1) audio->get_sample_data(inL.data(), 0);
    if (channels >= 2) audio->get_sample_data(inR.data(), 1);
    else if (channels == 1) inR = inL;

    host_for_audio->ProcessAudio(inL.data(), inR.data(), outL.data(), outR.data(), total_samples, channels);

    if (channels >= 1) audio->set_sample_data(outL.data(), 0);
    if (channels >= 2) audio->set_sample_data(outR.data(), 1);

    return true;
}

void CollectActiveInstances(EDIT_SECTION* edit) {
    g_temp_active_ids.clear();

    int max_layer = edit->info->layer_max;

    for (int layer = 0; layer <= max_layer; ++layer) {
        OBJECT_HANDLE object = edit->find_object(layer, 0);
        while (object != nullptr) {
            for (int i = 0; i < 100; ++i) {
                std::wstring indexed_filter_name = std::wstring(filter_name) + L":" + std::to_wstring(i);
                LPCSTR instance_id_str = edit->get_object_item_value(object, indexed_filter_name.c_str(), instance_id_param.name);
                if (!instance_id_str || instance_id_str[0] == '\0') {
                    break;
                }
                g_temp_active_ids.push_back(std::string(instance_id_str));
            }
            int current_end_frame = edit->get_object_layer_frame(object).end;
            object = edit->find_object(layer, current_end_frame + 1);
        }
    }
}

void func_project_save(PROJECT_FILE* pf) {
    std::string all_data_str;

    g_edit_handle->call_edit_section(CollectActiveInstances);

    std::lock_guard<std::mutex> lock(g_states_mutex);
    for (const auto& active_id : g_temp_active_ids) {
        if (g_plugin_state_database.count(active_id)) {
            all_data_str += active_id + ":" + g_plugin_state_database[active_id] + ";";
        }
    }

    if (!all_data_str.empty()) {
        pf->set_param_string("AudioHostStateDB", all_data_str.c_str());
    }
    else {
        pf->set_param_string("AudioHostStateDB", nullptr);
    }

    g_temp_active_ids.clear();
}

void func_project_load(PROJECT_FILE* pf) {
    std::lock_guard<std::mutex> lock(g_states_mutex);
    g_plugin_state_database.clear();
    LPCSTR all_data_str = pf->get_param_string("AudioHostStateDB");
    if (!all_data_str) return;
    std::string str(all_data_str);
    size_t start = 0;
    while (start < str.length()) {
        size_t end = str.find(';', start);
        if (end == std::string::npos) break;
        std::string pair_str = str.substr(start, end - start);
        size_t colon_pos = pair_str.find(':');
        if (colon_pos != std::string::npos) {
            g_plugin_state_database[pair_str.substr(0, colon_pos)] = pair_str.substr(colon_pos + 1);
        }
        start = end + 1;
    }
}

BOOL APIENTRY DllMain(HINSTANCE hinst, DWORD, LPVOID) {
    if (hinst) g_hinstance = hinst;
    return TRUE;
}

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
    if (version < MIN_VER) {
        MessageBox(NULL, L"AviUtl2のバージョンが古すぎます。", filter_name, MB_OK | MB_ICONERROR);
        return false;
    }
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
        MessageBox(NULL, L"COM 初期化に失敗しました。", filter_name, MB_OK | MB_ICONERROR);
        return false;
    }

    if (!AudioPluginFactory::Initialize(g_hinstance)) {
        CoUninitialize();
        MessageBox(NULL, L"Audio Plugin Factory の初期化に失敗しました。", filter_name, MB_OK | MB_ICONERROR);
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

    g_hMessageWindow = CreateWindow(wc.lpszClassName, _T("EAP2 Message Window"), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_hinstance, NULL);
    if (!g_hMessageWindow) {
        UnregisterClass(_T("EAP2_MessageWindowClass"), g_hinstance);
        AudioPluginFactory::Uninitialize();
        CoUninitialize();
        MessageBox(NULL, L"メッセージウィンドウの作成に失敗しました。", filter_name, MB_OK | MB_ICONERROR);
        return false;
    }

    SetTimer(NULL, g_timer_id, 50, TimerProc);
    DbgPrint("External Audio Processing 2 Initialized Successfully.");
    return true;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
    KillTimer(NULL, g_timer_id);

    if (g_hMessageWindow) {
        DestroyWindow(g_hMessageWindow);
        g_hMessageWindow = NULL;
    }

    UnregisterClass(_T("EAP2_MessageWindowClass"), g_hinstance);

    {
        std::lock_guard<std::mutex> lock(g_states_mutex);
        g_hosts.clear();
    }

    AudioPluginFactory::Uninitialize();
    CoUninitialize();
    DbgPrint("External Audio Processing 2 Uninitialized.");
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    host->set_plugin_information(plugin_info);
    host->register_filter_plugin(&filter_pluginable);
    host->register_project_save_handler(func_project_save);
    host->register_project_load_handler(func_project_load);
    g_edit_handle = host->create_edit_handle();
}
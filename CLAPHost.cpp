#include "ClapHost.h"
#include "clap/all.h"
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm>
#include "StringUtils.h"

struct ClapHost::Impl {
    Impl(HINSTANCE hInst);
    ~Impl();

    bool LoadPlugin(const std::filesystem::path& path, double sampleRate, int32_t blockSize);
    void ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, const std::vector<MidiEvent>& midiEvents);
    void Reset(int64_t currentSampleIndex, double bpm, int32_t timeSigNum, int32_t timeSigDenom);
    void ShowGui();
    void HideGui();
    std::string GetState();
    bool SetState(const std::string& state_b64);
    void ReleasePlugin();
    void Cleanup();
    bool GuiResize(uint32_t width, uint32_t height);
    int32_t GetParameterCount();
    bool GetParameterInfo(int32_t index, IAudioPluginHost::ParameterInfo& info);
    uint32_t GetParameterID(int32_t index);
    int32_t GetLatencySamples();
    int32_t GetLastTouchedParamID();
    void SetParameter(uint32_t paramId, float value);
    std::filesystem::path m_pluginPath;
    bool m_isGuiVisible = false;

    clap_host host = {};
    HINSTANCE hInstance;
    HMODULE hModule = nullptr;
    const clap_plugin_entry* entry = nullptr;
    const clap_plugin* plugin = nullptr;
    const clap_plugin_state* extState = nullptr;
    const clap_plugin_gui* extGui = nullptr;
    const clap_plugin_params* extParams = nullptr;
    const clap_plugin_latency* extLatency = nullptr;
    bool isReady = false;
    std::atomic<int32_t> lastTouchedParamID{-1};
    HWND guiWindow = nullptr;
    double currentSampleRate = 44100.0;
    int32_t currentBlockSize = 1024;

    void SetSampleRate(double newRate) {
        if (std::abs(currentSampleRate - newRate) < 0.1) return;
        currentSampleRate = newRate;
        if (!isReady || !plugin) return;

        if (plugin->stop_processing) plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        if (plugin->activate(plugin, currentSampleRate, currentBlockSize, currentBlockSize)) {
            if (plugin->start_processing) plugin->start_processing(plugin);
        }
    }
};


static void clap_log_callback(const clap_host_t* host, clap_log_severity severity, const char* msg) {
    DbgPrint("[CLAP] %hs \n", msg);
}

static bool clap_gui_resize(const clap_host_t* host, uint32_t width, uint32_t height) {
    auto self = static_cast<ClapHost::Impl*>(host->host_data);
    return self->GuiResize(width, height);
}
static const clap_host_log s_clap_log = {
    [](const clap_host_t*, clap_log_severity, const char* msg) {
        DbgPrint("[CLAP] %hs \n", msg);
    }
};

static const clap_host_gui s_clap_gui = {
    nullptr,
    [](const clap_host_t* h, uint32_t width, uint32_t height) -> bool {
        auto self = static_cast<ClapHost::Impl*>(h->host_data);
        if (self->guiWindow && IsWindow(self->guiWindow)) {
            RECT rc = {0, 0, (LONG)width, (LONG)height};
            AdjustWindowRect(&rc, GetWindowLong(self->guiWindow, GWL_STYLE), FALSE);
            SetWindowPos(self->guiWindow, NULL, 0, 0,
                rc.right - rc.left, rc.bottom - rc.top,
                SWP_NOMOVE | SWP_NOZORDER);
            return true;
        }
        return false;
    }
};

static const void* ClapHost_GetExtension(const clap_host_t* host, const char* id) {
    if (strcmp(id, CLAP_EXT_LOG) == 0) {
        return &s_clap_log;
    }
    if (strcmp(id, CLAP_EXT_GUI) == 0) {
        return &s_clap_gui;
    }
    return nullptr;
}

ClapHost::Impl::Impl(HINSTANCE hInst)
    : hInstance(hInst), isReady(false) {

    host.clap_version = CLAP_VERSION;
    host.host_data = this;
    host.name = "AviUtl2 CLAP Host";
    host.vendor = "BOOK25";
    host.url = "https://example.com";
    host.version = "1.0.0";
    host.get_extension = ClapHost_GetExtension;
    host.request_restart = [](const clap_host_t*) {};
    host.request_process = nullptr;
    host.request_callback = [](const clap_host_t*) {};
}

static uint32_t dummy_events_size(const clap_input_events_t*) { return 0; }
static const clap_event_header_t* dummy_events_get(const clap_input_events_t*, uint32_t) { return nullptr; }
static bool dummy_events_push(const clap_output_events_t*, const clap_event_header_t*) { return false; }
static const clap_input_events_t g_dummy_in = { nullptr, dummy_events_size, dummy_events_get };
static const clap_output_events_t g_dummy_out = { nullptr, dummy_events_push };

bool ClapHost::Impl::LoadPlugin(const std::filesystem::path& path, double sampleRate, int32_t blockSize) {
    ReleasePlugin();
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;

    std::wstring wpath = path.wstring();

    hModule = LoadLibraryW(wpath.c_str());
    if (!hModule) return false;

    auto entry_proc = (clap_plugin_entry_t*)GetProcAddress(hModule, "clap_plugin_entry");
    if (!entry_proc) entry_proc = (clap_plugin_entry_t*)GetProcAddress(hModule, "clap_entry");
    if (!entry_proc || !entry_proc->init(path.string().c_str())) {
        ReleasePlugin();
        return false;
    }

    entry = entry_proc;
    auto factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory || factory->get_plugin_count(factory) == 0) {
        ReleasePlugin();
        return false;
    }

    auto desc = factory->get_plugin_descriptor(factory, 0);
    plugin = factory->create_plugin(factory, &host, desc->id);
    if (!plugin || !plugin->init(plugin)) {
        ReleasePlugin();
        return false;
    }

    extState = (const clap_plugin_state*)plugin->get_extension(plugin, CLAP_EXT_STATE);
    extGui = (const clap_plugin_gui*)plugin->get_extension(plugin, CLAP_EXT_GUI);
    extParams = (const clap_plugin_params*)plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    extLatency = (const clap_plugin_latency*)plugin->get_extension(plugin, CLAP_EXT_LATENCY);

    if (extParams) DbgPrint("[CLAP] params extension available\n");
    if (extLatency) DbgPrint("[CLAP] latency extension available\n");

    if (!plugin->activate(plugin, sampleRate, blockSize, blockSize)) {
        ReleasePlugin();
        return false;
    }
    if (plugin->start_processing) plugin->start_processing(plugin);
    isReady = true;
    m_pluginPath = path;
    return true;
}

void ClapHost::Impl::ReleasePlugin() {
    if (!plugin) return;
    if (plugin->stop_processing) plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    plugin = nullptr;
    extState = nullptr;
    extGui = nullptr;
    extParams = nullptr;
    extLatency = nullptr;
    if (entry) entry->deinit();
    entry = nullptr;
    if (hModule) FreeLibrary(hModule);
    hModule = nullptr;
    isReady = false;
    m_pluginPath.clear();
    m_isGuiVisible = false;
    lastTouchedParamID = -1;
}

void ClapHost::Impl::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, const std::vector<MidiEvent>& midiEvents) {
    if (!isReady || !plugin) {
        memcpy(outL, inL, numSamples * sizeof(float));
        if (numChannels > 1) memcpy(outR, inR, numSamples * sizeof(float));
        return;
    }

    clap_process process = {};
    process.steady_time = 0;
    process.frames_count = numSamples;
    process.in_events = &g_dummy_in;
    process.out_events = &g_dummy_out;

    const float* inputs[2] = { inL, inR };
    float* outputs[2] = { outL, outR };

    clap_audio_buffer in_buf = {};
    in_buf.data32 = const_cast<float**>(inputs);
    in_buf.channel_count = numChannels;

    clap_audio_buffer out_buf = {};
    out_buf.data32 = outputs;
    out_buf.channel_count = numChannels;

    process.audio_inputs_count = (numChannels > 0) ? 1 : 0;
    process.audio_outputs_count = (numChannels > 0) ? 1 : 0;
    process.audio_inputs = &in_buf;
    process.audio_outputs = &out_buf;

    plugin->process(plugin, &process);
}

void ClapHost::Impl::Reset(int64_t currentSampleIndex, double bpm, int32_t timeSigNum, int32_t timeSigDenom) {
    if (isReady && plugin && plugin->reset) {
        plugin->reset(plugin);
    }
}

LRESULT CALLBACK ClapHostGuiProc(HWND hWnd, uint32_t msg, WPARAM wp, LPARAM lp) {
    auto self = (ClapHost::Impl*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (msg == WM_CREATE) {
        self = (ClapHost::Impl*)((CREATESTRUCT*)lp)->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    switch (msg) {
    case WM_CLOSE:
        return 0;

    case WM_DESTROY:
        if (self) {
            if (self->plugin && self->extGui) {
                self->extGui->destroy(self->plugin);
            }
            self->guiWindow = nullptr;
            self->m_isGuiVisible = false;
        }
        break;
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}

void ClapHost::Impl::ShowGui() {
    if (guiWindow && IsWindow(guiWindow)) {
        ShowWindow(guiWindow, SW_SHOW);
        SetForegroundWindow(guiWindow);
        m_isGuiVisible = true;
        return;
    }
    if (!plugin || !extGui || !extGui->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, false)) return;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = ClapHostGuiProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ClapHostGuiWindowClass";
    RegisterClassW(&wc);

    guiWindow = CreateWindowEx(0, wc.lpszClassName, L"CLAP Plugin", WS_OVERLAPPED | WS_CAPTION,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, hInstance, this);
    if (!guiWindow) return;

    if (!extGui->create(plugin, CLAP_WINDOW_API_WIN32, false)) {
        DestroyWindow(guiWindow);
        guiWindow = nullptr;
        return;
    }

    uint32_t w = 800, h = 600;
    extGui->get_size(plugin, &w, &h);
    RECT rc = { 0, 0, (LONG)w, (LONG)h };
    AdjustWindowRect(&rc, GetWindowLong(guiWindow, GWL_STYLE), FALSE);
    SetWindowPos(guiWindow, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

    clap_window win = {};
    win.api = CLAP_WINDOW_API_WIN32;
    win.win32 = guiWindow;
    extGui->set_parent(plugin, &win);
    extGui->show(plugin);
    ShowWindow(guiWindow, SW_SHOW);
    m_isGuiVisible = true;
}

void ClapHost::Impl::HideGui() {
    if (guiWindow) DestroyWindow(guiWindow);
    m_isGuiVisible = false;
}

bool ClapHost::Impl::GuiResize(uint32_t width, uint32_t height) {
    if (!guiWindow || !IsWindow(guiWindow)) return false;
    RECT rc = { 0, 0, (LONG)width, (LONG)height };
    AdjustWindowRect(&rc, GetWindowLong(guiWindow, GWL_STYLE), FALSE);
    SetWindowPos(guiWindow, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
    return true;
}

std::string ClapHost::Impl::GetState() {
    if (!plugin || !extState) return "";
    struct Ctx { std::vector<BYTE> data; };
    Ctx ctx;
    clap_ostream stream = { &ctx, [](const clap_ostream* s, const void* buf, uint64_t size) -> int64_t {
        auto c = (Ctx*)s->ctx;
        c->data.insert(c->data.end(), (const BYTE*)buf, (const BYTE*)buf + size);
        return (int64_t)size;
    } };
    if (extState->save(plugin, &stream)) {
        return "CLAP:" + StringUtils::Base64Encode(ctx.data.data(), (DWORD)ctx.data.size());
    }
    return "";
}

bool ClapHost::Impl::SetState(const std::string& state_b64) {
    if (state_b64.rfind("CLAP:", 0) != 0 || !plugin || !extState) return false;
    auto data = StringUtils::Base64Decode(state_b64.substr(5));
    if (data.empty() && !state_b64.empty()) return false;
    struct Ctx { const std::vector<BYTE>* d; size_t pos = 0; };
    Ctx ctx{ &data };
    clap_istream stream = { &ctx, [](const clap_istream* s, void* buf, uint64_t size) -> int64_t {
        auto c = (Ctx*)s->ctx;
        size_t to_read = (std::min)(size, c->d->size() - c->pos);
        if (to_read == 0) return 0;
        memcpy(buf, c->d->data() + c->pos, to_read);
        c->pos += to_read;
        return (int64_t)to_read;
    } };
    if (!extState->load(plugin, &stream)) return false;
    return true;
}

void ClapHost::Impl::Cleanup() { ReleasePlugin(); }

int32_t ClapHost::Impl::GetParameterCount() {
    if (!extParams) {
        DbgPrint("[CLAP] params extension not available\n");
        return 0;
    }
    return static_cast<int32_t>(extParams->count(plugin));
}

bool ClapHost::Impl::GetParameterInfo(int32_t index, IAudioPluginHost::ParameterInfo& info) {
    if (!extParams) {
        DbgPrint("[CLAP] params extension not available\n");
        return false;
    }
    
    clap_param_info_t clapInfo = {};
    if (!extParams->get_info(plugin, static_cast<uint32_t>(index), &clapInfo)) {
        DbgPrint("[CLAP] failed to get parameter info for index %d\n", index);
        return false;
    }
    strncpy_s(info.name, clapInfo.name, sizeof(info.name) - 1);
    info.name[sizeof(info.name) - 1] = '\0';
    strncpy_s(info.unit, clapInfo.module, sizeof(info.unit) - 1);
    info.unit[sizeof(info.unit) - 1] = '\0';
    if (clapInfo.flags & CLAP_PARAM_IS_STEPPED) info.step = static_cast<uint32_t>(clapInfo.max_value - clapInfo.min_value);
    else info.step = 0;
    
    return true;
}

uint32_t ClapHost::Impl::GetParameterID(int32_t index) {
    if (!extParams) {
        DbgPrint("[CLAP] params extension not available\n");
        return 0;
    }
    
    clap_param_info_t clapInfo = {};
    if (!extParams->get_info(plugin, static_cast<uint32_t>(index), &clapInfo)) {
        DbgPrint("[CLAP] failed to get parameter info for index %d\n", index);
        return 0;
    }
    
    return static_cast<uint32_t>(clapInfo.id);
}

int32_t ClapHost::Impl::GetLatencySamples() {
    if (!extLatency) {
        DbgPrint("[CLAP] latency extension not available\n");
        return 0;
    }
    
    if (!isReady || !plugin) {
        DbgPrint("[CLAP] plugin not ready\n");
        return 0;
    }
    
    return static_cast<int32_t>(extLatency->get(plugin));
}

int32_t ClapHost::Impl::GetLastTouchedParamID() {
    return lastTouchedParamID.exchange(-1);
}

void ClapHost::Impl::SetParameter(uint32_t paramId, float value) {
    if (!extParams) {
        DbgPrint("[CLAP] params extension not available\n");
        return;
    }
    
    DbgPrint("[CLAP] SetParameter id=%u, value=%f (not fully implemented)\n", paramId, value);
}

ClapHost::Impl::~Impl() {
    Cleanup();
}


ClapHost::ClapHost(HINSTANCE hInstance) : m_impl(std::make_unique<Impl>(hInstance)) {}
ClapHost::~ClapHost() = default;
bool ClapHost::LoadPlugin(const std::filesystem::path& path, double sampleRate, int32_t blockSize) { return m_impl->LoadPlugin(path, sampleRate, blockSize); }
void ClapHost::SetSampleRate(double sampleRate) { m_impl->SetSampleRate(sampleRate); }
double ClapHost::GetSampleRate() const { return m_impl->currentSampleRate; }
void ClapHost::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, int64_t currentSampleIndex, double bpm, int32_t tsNum, int32_t tsDenom, const std::vector<MidiEvent>& midiEvents) { m_impl->ProcessAudio(inL, inR, outL, outR, numSamples, numChannels, midiEvents); }
void ClapHost::Reset(int64_t currentSampleIndex, double bpm, int32_t timeSigNum, int32_t timeSigDenom) { m_impl->Reset(currentSampleIndex, bpm, timeSigNum, timeSigDenom); }
void ClapHost::ShowGui() { m_impl->ShowGui(); }
void ClapHost::HideGui() { m_impl->HideGui(); }
std::string ClapHost::GetState() { return m_impl->GetState(); }
bool ClapHost::SetState(const std::string& state_b64) { return m_impl->SetState(state_b64); }
void ClapHost::Cleanup() { m_impl->Cleanup(); }
bool ClapHost::IsGuiVisible() const {
    return m_impl->m_isGuiVisible;
}

std::filesystem::path ClapHost::GetPluginPath() const {
    return m_impl->m_pluginPath;
}

void ClapHost::SetParameter(uint32_t paramId, float value) {
    m_impl->SetParameter(paramId, value);
}

int32_t ClapHost::GetLastTouchedParamID() {
    return m_impl->GetLastTouchedParamID();
}

int32_t ClapHost::GetLatencySamples() {
    return m_impl->GetLatencySamples();
}

int32_t ClapHost::GetParameterCount() {
    return m_impl->GetParameterCount();
}

bool ClapHost::GetParameterInfo(int32_t index, ParameterInfo& info) {
    return m_impl->GetParameterInfo(index, info);
}

uint32_t ClapHost::GetParameterID(int32_t index) {
    return m_impl->GetParameterID(index);
}
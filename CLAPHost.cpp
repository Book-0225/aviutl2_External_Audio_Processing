#include "CLAPHost.h"
#include "clap/all.h"
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <wincrypt.h>
#include <filesystem>
#include <algorithm>

static std::string Base64Encode(const BYTE* data, DWORD len) {
    if (!data || len == 0) return "";
    DWORD b64_len = 0;
    DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    if (!CryptBinaryToStringA(data, len, flags, nullptr, &b64_len)) return "";
    std::string s(b64_len, '\0');
    if (!CryptBinaryToStringA(data, len, flags, &s[0], &b64_len)) return "";
    size_t nullPos = s.find('\0');
    if (nullPos != std::string::npos) {
        s.resize(nullPos);
    } else {
        s.resize(b64_len); 
    }
    while (!s.empty() && (s.back() == '\0' || s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    return s;
}

static std::vector<BYTE> Base64Decode(const std::string& b64) {
    if (b64.empty()) return {};
    std::string safe_b64 = b64;
    while (safe_b64.size() % 4 != 0) {
        safe_b64 += '=';
    }
    DWORD bin_len = 0;
    DWORD flags = CRYPT_STRING_BASE64_ANY;
    if (!CryptStringToBinaryA(safe_b64.c_str(), (DWORD)safe_b64.size(), flags, nullptr, &bin_len, nullptr, nullptr)) {
        return {};
    } 
    std::vector<BYTE> v(bin_len);
    if (!CryptStringToBinaryA(safe_b64.c_str(), (DWORD)safe_b64.size(), flags, v.data(), &bin_len, nullptr, nullptr)) {
        return {};
    }
    return v;
}

struct ClapHost::Impl {
    Impl(HINSTANCE hInst);
    ~Impl();

    bool LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize);
    void ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels);
    void Reset();
    void ShowGui();
    void HideGui();
    std::string GetState();
    bool SetState(const std::string& state_b64);
    void ReleasePlugin();
    void Cleanup();
    bool GuiResize(uint32_t width, uint32_t height);
    std::string m_pluginPath;
    bool m_isGuiVisible = false;

    clap_host host = {};
    HINSTANCE hInstance;
    HMODULE hModule = nullptr;
    const clap_plugin_entry* entry = nullptr;
    const clap_plugin* plugin = nullptr;
    const clap_plugin_state* extState = nullptr;
    const clap_plugin_gui* extGui = nullptr;
    bool isReady = false;
    HWND guiWindow = nullptr;
    double currentSampleRate = 44100.0;
    int32_t currentBlockSize = 1024;
};


static void clap_log_callback(const clap_host_t* host, clap_log_severity severity, const char* msg) {
    OutputDebugStringA("[CLAP] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

static bool clap_gui_resize(const clap_host_t* host, uint32_t width, uint32_t height) {
    auto self = static_cast<ClapHost::Impl*>(host->host_data);
    return self->GuiResize(width, height);
}
static const clap_host_log s_clap_log = {
    [](const clap_host_t*, clap_log_severity, const char* msg) {
        OutputDebugStringA("[CLAP] ");
        OutputDebugStringA(msg);
        OutputDebugStringA("\n");
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

bool ClapHost::Impl::LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize) {
    ReleasePlugin();
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;

    std::filesystem::path p(path);
    std::wstring wpath = p.wstring();

    hModule = LoadLibraryW(wpath.c_str());
    if (!hModule) return false;

    auto entry_proc = (clap_plugin_entry_t*)GetProcAddress(hModule, "clap_plugin_entry");
    if (!entry_proc) entry_proc = (clap_plugin_entry_t*)GetProcAddress(hModule, "clap_entry");
    if (!entry_proc || !entry_proc->init(path.c_str())) {
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
    if (entry) entry->deinit();
    entry = nullptr;
    if (hModule) FreeLibrary(hModule);
    hModule = nullptr;
    isReady = false;
    m_pluginPath.clear();
    m_isGuiVisible = false;
}

void ClapHost::Impl::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels) {
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

void ClapHost::Impl::Reset() {
    if (isReady && plugin && plugin->reset) {
        plugin->reset(plugin);
    }
}

LRESULT CALLBACK ClapHostGuiProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
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

    guiWindow = CreateWindowExW(0, wc.lpszClassName, L"CLAP Plugin", WS_OVERLAPPEDWINDOW,
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
        return "CLAP:" + Base64Encode(ctx.data.data(), (DWORD)ctx.data.size());
    }
    return "";
}

bool ClapHost::Impl::SetState(const std::string& state_b64) {
    if (state_b64.rfind("CLAP:", 0) != 0 || !plugin || !extState) return false;
    auto data = Base64Decode(state_b64.substr(5));
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
ClapHost::Impl::~Impl() {
    Cleanup();
}

ClapHost::ClapHost(HINSTANCE hInstance) : m_impl(std::make_unique<Impl>(hInstance)) {}
ClapHost::~ClapHost() = default;
bool ClapHost::LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize) { return m_impl->LoadPlugin(path, sampleRate, blockSize); }
void ClapHost::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels) { m_impl->ProcessAudio(inL, inR, outL, outR, numSamples, numChannels); }
void ClapHost::Reset() { m_impl->Reset(); }
void ClapHost::ShowGui() { m_impl->ShowGui(); }
void ClapHost::HideGui() { m_impl->HideGui(); }
std::string ClapHost::GetState() { return m_impl->GetState(); }
bool ClapHost::SetState(const std::string& state_b64) { return m_impl->SetState(state_b64); }
void ClapHost::Cleanup() { m_impl->Cleanup(); }
bool ClapHost::IsGuiVisible() const {
    return m_impl->m_isGuiVisible;
}

std::string ClapHost::GetPluginPath() const {
    return m_impl->m_pluginPath;
}
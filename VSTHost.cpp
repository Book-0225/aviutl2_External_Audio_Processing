#define _CRT_SECURE_NO_WARNINGS
#include "VstHost.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include <windows.h>
#include <vector>
#include <string>
#include <wincrypt.h>
#include <objbase.h>
#include <mutex>

#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "ole32.lib")

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;

static std::string Base64Encode(const BYTE* data, DWORD len) {
    if (!data || len == 0) return "";
    DWORD b64_len = 0;
    if (!CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64_len)) return "";
    std::string s(b64_len, '\0');
    if (!CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &s[0], &b64_len)) return "";
    s.resize(b64_len - 1);
    return s;
}

static std::vector<BYTE> Base64Decode(const std::string& b64) {
    if (b64.empty()) return {};
    DWORD bin_len = 0;
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &bin_len, nullptr, nullptr)) return {};
    std::vector<BYTE> v(bin_len);
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, v.data(), &bin_len, nullptr, nullptr)) return {};
    return v;
}

class WindowController : public IPlugFrame {
public:
    WindowController(IPlugView* view, HWND parent) : plugView(view), parentWindow(parent), m_refCount(1) {}
    ~WindowController() {}
    void connect() { if (plugView) plugView->setFrame(this); }
    void disconnect() { if (plugView) plugView->setFrame(nullptr); }

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(_iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(_iid, FUnknown::iid)) {
            *obj = this; addRef(); return kResultTrue;
        }
        *obj = nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    uint32 PLUGIN_API release() override {
        if (--m_refCount == 0) { delete this; return 0; }
        return m_refCount;
    }
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (view == plugView && newSize && parentWindow) {
            RECT rc = { 0, 0, (LONG)(newSize->right - newSize->left), (LONG)(newSize->bottom - newSize->top) };
            DWORD style = GetWindowLong(parentWindow, GWL_STYLE);
            DWORD exStyle = GetWindowLong(parentWindow, GWL_EXSTYLE);
            AdjustWindowRectEx(&rc, style, FALSE, exStyle);
            SetWindowPos(parentWindow, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
            if (plugView) plugView->onSize(newSize);
        }
        return kResultTrue;
    }
private:
    IPlugView* plugView = nullptr;
    HWND parentWindow = nullptr;
    std::atomic<uint32> m_refCount{ 1 };
};

struct VstHost::Impl {
    Impl(HINSTANCE hInst) : hInstance(hInst), isReady(false) {}
    ~Impl() { ReleasePlugin(); }

    bool LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize);
    void ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels);
    void ShowGui();
    void HideGui();
    std::string GetState();
    bool SetState(const std::string& state_b64);
    void ReleasePlugin();
    bool IsGuiVisible() const { return guiWindow && IsWindow(guiWindow); }
    std::string GetPluginPath() const { return currentPluginPath; }

    HINSTANCE hInstance = nullptr;
    Module::Ptr module;
    PlugProvider* provider = nullptr;
    IComponent* component = nullptr;
    IEditController* controller = nullptr;
    IAudioProcessor* processor = nullptr;
    bool isReady = false;
    HWND guiWindow = nullptr;
    FUnknownPtr<IPlugView> plugView;
    WindowController* windowController = nullptr;
    std::string currentPluginPath;
    double currentSampleRate = 44100.0;
    int32_t currentBlockSize = 1024;

    std::mutex paramMutex;
    std::mutex processorUpdateMutex;
    std::vector<std::pair<ParamID, ParamValue>> pendingParamChanges;
    std::vector<std::pair<ParamID, ParamValue>> processorParamUpdates;
};

bool VstHost::Impl::LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize) {
    ReleasePlugin();
    currentPluginPath = path;
    std::string error;
    module = Module::create(path, error);
    if (!module) return false;

    auto factory = module->getFactory();
    ClassInfo target;
    bool found = false;
    for (const auto& ci : factory.classInfos()) {
        if (ci.category() == "Audio Module Class" ||
            ci.category() == "Instrument Module Class" ||
            ci.category() == "MIDI Module Class") {
            target = ci; found = true; break;
        }
    }
    if (!found) { module.reset(); return false; }

    provider = new PlugProvider(factory, target, true);
    if (!provider) { module.reset(); return false; }

    component = provider->getComponent();
    controller = provider->getController();
    if (!component || !controller) { ReleasePlugin(); return false; }

    if (component->queryInterface(IAudioProcessor::iid, (void**)&processor) != kResultOk) {
        ReleasePlugin(); return false;
    }

    component->setActive(true);
    ProcessSetup setup{ kRealtime, kSample32, blockSize, sampleRate };
    if (processor->setupProcessing(setup) != kResultOk) {
        ReleasePlugin(); return false;
    }

    int32_t numIn = component->getBusCount(kAudio, kInput);
    int32_t numOut = component->getBusCount(kAudio, kOutput);
    for (int32_t i = 0; i < numIn; ++i) component->activateBus(kAudio, kInput, i, true);
    for (int32_t i = 0; i < numOut; ++i) component->activateBus(kAudio, kOutput, i, true);

    processor->setProcessing(true);
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    isReady = true;
    return true;
}

void VstHost::Impl::ReleasePlugin() {
    HideGui();

    isReady = false;
    if (processor) {
        processor->setProcessing(false);
        processor->release();
        processor = nullptr;
    }

    if (component) {
        component->setActive(false);
        component->release();
        component = nullptr;
    }

    if (controller) {
        controller->release();
        controller = nullptr;
    }

    if (provider) {
        delete provider;
        provider = nullptr;
    }

    module.reset();
    currentPluginPath.clear();
}

void VstHost::Impl::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels) {
    if (!isReady || !processor || numSamples <= 0 || numChannels <= 0) {
        memcpy(outL, inL, numSamples * sizeof(float));
        if (numChannels > 1) memcpy(outR, inR, numSamples * sizeof(float));
        return;
    }

    ParameterChanges inParamChanges;
    {
        std::lock_guard<std::mutex> lock(paramMutex);
        if (!pendingParamChanges.empty()) {
            for (const auto& change : pendingParamChanges) {
                int32 numPoints = 1;
                IParamValueQueue* queue = inParamChanges.addParameterData(change.first, numPoints);
                if (queue) {
                    int32 pointIndex;
                    queue->addPoint(0, change.second, pointIndex);
                }
            }
            pendingParamChanges.clear();
        }
    }

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;

    std::vector<AudioBusBuffers> inBufs;
    std::vector<std::vector<float*>> inPtrs;
    int32_t numInputs = component->getBusCount(kAudio, kInput);
    if (numInputs > 0) {
        inBufs.resize(numInputs);
        inPtrs.resize(numInputs);
        for (int32_t i = 0; i < numInputs; ++i) {
            BusInfo info; component->getBusInfo(kAudio, kInput, i, info);
            inBufs[i].numChannels = info.channelCount;
            inPtrs[i].resize(info.channelCount);
            if (i == 0) {
                inPtrs[i][0] = const_cast<float*>(inL);
                if (info.channelCount > 1 && numChannels > 1) inPtrs[i][1] = const_cast<float*>(inR);
                else if (info.channelCount > 1) inPtrs[i][1] = const_cast<float*>(inL);
            }
            inBufs[i].channelBuffers32 = inPtrs[i].data();
        }
        data.numInputs = numInputs;
        data.inputs = inBufs.data();
    }

    std::vector<AudioBusBuffers> outBufs;
    std::vector<std::vector<float*>> outPtrs;
    int32_t numOutputs = component->getBusCount(kAudio, kOutput);
    if (numOutputs > 0) {
        outBufs.resize(numOutputs);
        outPtrs.resize(numOutputs);
        for (int32_t i = 0; i < numOutputs; ++i) {
            BusInfo info; component->getBusInfo(kAudio, kOutput, i, info);
            outBufs[i].numChannels = info.channelCount;
            outPtrs[i].resize(info.channelCount);
            if (i == 0) {
                outPtrs[i][0] = outL;
                if (info.channelCount > 1 && numChannels > 1) outPtrs[i][1] = outR;
                else if (info.channelCount > 1) outPtrs[i][1] = outL;
            }
            outBufs[i].channelBuffers32 = outPtrs[i].data();
        }
        data.numOutputs = numOutputs;
        data.outputs = outBufs.data();
    }

    ProcessContext ctx{};
    ctx.state = ProcessContext::kPlaying;
    ctx.sampleRate = currentSampleRate;
    data.processContext = &ctx;

    if (processor->process(data) != kResultOk) {
        memcpy(outL, inL, numSamples * sizeof(float));
        if (numChannels > 1) memcpy(outR, inR, numSamples * sizeof(float));
    }
}

void VstHost::Impl::ShowGui() {
    if (guiWindow && IsWindow(guiWindow)) {
        ShowWindow(guiWindow, SW_SHOW); SetForegroundWindow(guiWindow); return;
    }
    if (!controller) return;

    plugView = owned(controller->createView("editor"));
    if (!plugView) return;

    ViewRect vr; plugView->getSize(&vr);
    RECT rc = { 0, 0, vr.right - vr.left, vr.bottom - vr.top };
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);

    WNDCLASS wc{};
    wc.lpfnWndProc = [](HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        VstHost::Impl* self = nullptr;
        if (msg == WM_CREATE) {
            self = (VstHost::Impl*)((CREATESTRUCT*)lp)->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
        }
        else {
            self = (VstHost::Impl*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        }
        
        if (msg == WM_CLOSE) return 0;

        if (msg == WM_DESTROY) {
            if (self) {
                if (self->plugView) self->plugView->removed();
                self->plugView.reset();
                if (self->windowController) {
                    self->windowController->disconnect();
                    self->windowController->release();
                    self->windowController = nullptr;
                }
                self->guiWindow = nullptr;
            }
        }
        return DefWindowProc(hWnd, msg, wp, lp);
        };
    wc.hInstance = hInstance;
    wc.lpszClassName = L"VstHostGuiWindowClass";
    RegisterClass(&wc);

    guiWindow = CreateWindowEx(0, wc.lpszClassName, L"VST3 Plugin", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, this);
    if (!guiWindow) { plugView.reset(); return; }

    windowController = new WindowController(plugView, guiWindow);
    windowController->connect();
    if (plugView->attached(guiWindow, kPlatformTypeHWND) != kResultOk) {
        DestroyWindow(guiWindow);
        return;
    }
    ShowWindow(guiWindow, SW_SHOW);
}

void VstHost::Impl::HideGui() {
    if (guiWindow) {
        DestroyWindow(guiWindow);
    }
}

std::string VstHost::Impl::GetState() {
    if (!provider || !component || !controller) return "";
    MemoryStream cStream, tStream;
    component->getState(&cStream);
    controller->getState(&tStream);
    MemoryStream full;
    int32 b;
    int64 cs = cStream.getSize(), ts = tStream.getSize();
    full.write(&cs, sizeof(cs), &b);
    if (cs > 0) full.write(cStream.getData(), (int32_t)cs, &b);
    full.write(&ts, sizeof(ts), &b);
    if (ts > 0) full.write(tStream.getData(), (int32_t)ts, &b);
    return "VST3_DUAL:" + Base64Encode((const BYTE*)full.getData(), (DWORD)full.getSize());
}

bool VstHost::Impl::SetState(const std::string& state_b64) {
    if (state_b64.rfind("VST3_DUAL:", 0) != 0) return false;
    auto data = Base64Decode(state_b64.substr(10));
    if (data.empty() && !state_b64.empty()) return false;
    MemoryStream stream(data.data(), data.size());
    int32 br;
    int64 cs = 0, ts = 0;
    stream.read(&cs, sizeof(cs), &br);
    if (cs > 0 && component) {
        std::vector<BYTE> d(cs); stream.read(d.data(), (int32)cs, &br);
        MemoryStream s(d.data(), cs); component->setState(&s);
    }
    stream.read(&ts, sizeof(ts), &br);
    if (ts > 0 && controller) {
        std::vector<BYTE> d(ts); stream.read(d.data(), (int32)ts, &br);
        MemoryStream s(d.data(), ts); controller->setState(&s);
    }
    return true;
}

VstHost::VstHost(HINSTANCE hInstance) : m_impl(std::make_unique<Impl>(hInstance)) {}
VstHost::~VstHost() = default;

bool VstHost::LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize) {
    return m_impl->LoadPlugin(path, sampleRate, blockSize);
}

void VstHost::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels) {
    m_impl->ProcessAudio(inL, inR, outL, outR, numSamples, numChannels);
}

void VstHost::ShowGui() { m_impl->ShowGui(); }
void VstHost::HideGui() { m_impl->HideGui(); }
std::string VstHost::GetState() { return m_impl->GetState(); }
bool VstHost::SetState(const std::string& state_b64) { return m_impl->SetState(state_b64); }
void VstHost::Cleanup() { m_impl->ReleasePlugin(); }
bool VstHost::IsGuiVisible() const { return m_impl->IsGuiVisible(); }
std::string VstHost::GetPluginPath() const { return m_impl->GetPluginPath(); }
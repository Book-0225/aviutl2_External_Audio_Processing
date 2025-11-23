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
#include <filesystem>
#include "StringUtils.h"
#include "Eap2Common.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;


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

class HostComponentHandler : public IComponentHandler
{
public:
    std::atomic<int32_t> lastTouchedParamID{ -1 };
    tresult PLUGIN_API beginEdit(ParamID tag) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID tag, ParamValue valueNormalized) override {
        lastTouchedParamID.store(static_cast<int32_t>(tag));
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID tag) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override { return kResultOk; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
            QUERY_INTERFACE(iid, obj, FUnknown::iid, FUnknown)
            * obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refCount; }
    uint32 PLUGIN_API release() override {
        if (--refCount == 0) { delete this; return 0; }
        return refCount;
    }
private:
    std::atomic<uint32> refCount{ 1 };
};

static tresult SafeProcessCall(IAudioProcessor* processor, ProcessData& data) {
    if (!processor) return kResultFalse;

    __try {
        return processor->process(data);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint("[EAP2 Error] Access Violation inside VST3::process()");
        return kResultFalse;
    }
}


struct VstHost::Impl {
    std::vector<float> dummyBuffer;
    Impl(HINSTANCE hInst) : hInstance(hInst), isReady(false) {
        dummyBuffer.assign(4096, 0.0f);
    }
    ~Impl() { ReleasePlugin(); }

    bool LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize);
    void ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, int64_t currentSampleIndex, double bpm, int32_t tsNum, int32_t tsDenom);
    void Reset();
    void ShowGui();
    void HideGui();
    std::string GetState();
    bool SetState(const std::string& state_b64);
    void ReleasePlugin();
    bool IsGuiVisible() const { return guiWindow && IsWindow(guiWindow); }
    std::string GetPluginPath() const { return currentPluginPath; }

    struct PendingParamChange {
        ParamID id;
        ParamValue value;
    };
    std::vector<PendingParamChange> paramQueue;
    std::mutex paramQueueMutex;

    int32_t GetLastTouchedParamID() {
        if (componentHandler) {
            return componentHandler->lastTouchedParamID.exchange(-1);
        }
        return -1;
    }

    void SetParameter(uint32_t paramId, float value) {
        if (controller) {
            controller->setParamNormalized(paramId, (ParamValue)value);
        }

        std::lock_guard<std::mutex> lock(paramQueueMutex);
        paramQueue.push_back({ paramId, (ParamValue)value });
    }

    float* GetDummyBuffer(int32_t requiredSize) {
        if (dummyBuffer.size() < (size_t)requiredSize) {
            dummyBuffer.assign(requiredSize + 1024, 0.0f);
        }
        return dummyBuffer.data();
    }

    HINSTANCE hInstance = nullptr;
    Module::Ptr module;
    PlugProvider* provider = nullptr;
    IComponent* component = nullptr;
    IEditController* controller = nullptr;
    IAudioProcessor* processor = nullptr;
    FUnknownPtr<HostComponentHandler> componentHandler;
    bool isReady = false;
    HWND guiWindow = nullptr;
    FUnknownPtr<IPlugView> plugView;
    WindowController* windowController = nullptr;
    std::string currentPluginPath;
    double currentSampleRate = 44140.0;
    int32_t currentBlockSize = 1024;

    std::mutex paramMutex;
    std::mutex processorUpdateMutex;
    std::vector<std::pair<ParamID, ParamValue>> pendingParamChanges;
};

bool VstHost::Impl::LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize) {
    ReleasePlugin();
    componentHandler = owned(new HostComponentHandler());
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

    if (controller) {
        controller->setComponentHandler(componentHandler);
    }
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
        controller->setComponentHandler(nullptr);
        controller->release();
        controller = nullptr;
    }

    if (provider) {
        delete provider;
        provider = nullptr;
    }

    module.reset();
    currentPluginPath.clear();
    componentHandler.reset();
}

void VstHost::Impl::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, int64_t currentSampleIndex, double bpm, int32_t tsNum, int32_t tsDenom) {
    if (!isReady || !processor || !component || numSamples <= 0 || numChannels <= 0) {
        if (outL != inL) memcpy(outL, inL, numSamples * sizeof(float));
        if (numChannels > 1 && outR != inR) memcpy(outR, inR, numSamples * sizeof(float));
        return;
    }

    ParameterChanges inParamChanges;
    {
        std::lock_guard<std::mutex> lock(paramQueueMutex);
        if (!paramQueue.empty()) {
            for (const auto& change : paramQueue) {
                int32 index = 0;
                IParamValueQueue* queue = inParamChanges.addParameterData(change.id, index);
                if (queue) {
                    int32 pointIndex = 0;
                    queue->addPoint(0, change.value, pointIndex);
                }
            }
            paramQueue.clear();
        }
    }

    float* silence = GetDummyBuffer(numSamples);

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;
    data.inputParameterChanges = &inParamChanges;
    std::vector<AudioBusBuffers> inBufs;
    std::vector<std::vector<float*>> inPtrs;
    int32_t numInputs = component->getBusCount(kAudio, kInput);
    if (numInputs > 0) {
        inBufs.resize(numInputs);
        inPtrs.resize(numInputs);
        for (int32_t i = 0; i < numInputs; ++i) {
            BusInfo info;
            component->getBusInfo(kAudio, kInput, i, info);

            inBufs[i].numChannels = info.channelCount;
            inBufs[i].silenceFlags = 0;

            if (info.channelCount > 0) {
                inPtrs[i].resize(info.channelCount);
                if (i == 0) {
                    inPtrs[i][0] = const_cast<float*>(inL);
                    if (info.channelCount > 1) {
                        inPtrs[i][1] = const_cast<float*>(numChannels > 1 ? inR : inL);
                    }
                    for (int ch = 2; ch < info.channelCount; ++ch) {
                        inPtrs[i][ch] = silence;
                    }
                }
                else {
                    for (int ch = 0; ch < info.channelCount; ++ch) {
                        inPtrs[i][ch] = silence;
                    }
                    inBufs[i].silenceFlags = (1 << info.channelCount) - 1;
                }
                inBufs[i].channelBuffers32 = inPtrs[i].data();
            }
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
            BusInfo info;
            component->getBusInfo(kAudio, kOutput, i, info);

            outBufs[i].numChannels = info.channelCount;
            outBufs[i].silenceFlags = 0;

            if (info.channelCount > 0) {
                outPtrs[i].resize(info.channelCount);
                if (i == 0) {
                    outPtrs[i][0] = outL;
                    if (info.channelCount > 1) {
                        outPtrs[i][1] = (numChannels > 1) ? outR : silence;
                    }
                    for (int ch = 2; ch < info.channelCount; ++ch) {
                        outPtrs[i][ch] = silence;
                    }
                }
                else {
                    for (int ch = 0; ch < info.channelCount; ++ch) {
                        outPtrs[i][ch] = silence;
                    }
                }
                outBufs[i].channelBuffers32 = outPtrs[i].data();
            }
        }
        data.numOutputs = numOutputs;
        data.outputs = outBufs.data();
    }

    ProcessContext ctx{};
    ctx.state = ProcessContext::kPlaying;
    ctx.sampleRate = currentSampleRate;
	ctx.projectTimeSamples = currentSampleIndex;
	ctx.tempo = bpm;
	ctx.timeSigNumerator = tsNum;
	ctx.timeSigDenominator = tsDenom;
    data.processContext = &ctx;
    tresult result = kResultFalse;
    result = SafeProcessCall(processor, data);

    if (result != kResultOk) {
        if (outL != inL) memcpy(outL, inL, numSamples * sizeof(float));
        if (numChannels > 1 && outR != inR) memcpy(outR, inR, numSamples * sizeof(float));
    }
}

void VstHost::Impl::Reset() {
    if (!isReady || !component) {
        return;
    }
    component->setActive(false);
    component->setActive(true);
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
    try {
        MemoryStream cStream, tStream;
        if (component->getState(&cStream) != kResultOk) return "";

        if (controller->getState(&tStream) != kResultOk) {
            DbgPrint("[EAP2 Error] Exception inside VST3 GetState");
        }

        int64_t cs = cStream.getSize();
        int64_t ts = tStream.getSize();
        if (cs < 0 || ts < 0 || cs > 200 * 1024 * 1024) return ""; // 200MB制限

        MemoryStream full;
        int32 b;
        full.write(&cs, sizeof(cs), &b);
        if (cs > 0) full.write(cStream.getData(), (int32_t)cs, &b);
        full.write(&ts, sizeof(ts), &b);
        if (ts > 0) full.write(tStream.getData(), (int32_t)ts, &b);
        return "VST3_DUAL:" + StringUtils::Base64Encode((const BYTE*)full.getData(), (DWORD)full.getSize());
    }
    catch (...) {
        DbgPrint("[EAP2 Error] Exception inside VST3 GetState");
        return "";
    }
}

bool VstHost::Impl::SetState(const std::string& state_b64) {
    if (state_b64.rfind("VST3_DUAL:", 0) != 0) return false;
    auto data = StringUtils::Base64Decode(state_b64.substr(10));
    if (data.empty() && !state_b64.empty()) return false;
    MemoryStream stream(data.data(), data.size());
    int32 br;
    int64 cs = 0, ts = 0;
    stream.read(&cs, sizeof(cs), &br);
    if (cs > 0 && component) {
        std::vector<BYTE> d(cs); stream.read(d.data(), (int32)cs, &br);
        MemoryStream s(d.data(), cs); 
        if (component->setState(&s) != kResultOk) return false;
    }
    stream.read(&ts, sizeof(ts), &br);
    if (ts > 0 && controller) {
        std::vector<BYTE> d(ts); stream.read(d.data(), (int32)ts, &br);
        MemoryStream s(d.data(), ts); 
        if (controller->setState(&s) != kResultOk) return false;
    }
    return true;
}

VstHost::VstHost(HINSTANCE hInstance) : m_impl(std::make_unique<Impl>(hInstance)) {}
VstHost::~VstHost() = default;

bool VstHost::LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize) {
    return m_impl->LoadPlugin(path, sampleRate, blockSize);
}

void VstHost::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, int64_t currentSampleIndex, double bpm, int32_t tsNum, int32_t tsDenom) {
    m_impl->ProcessAudio(inL, inR, outL, outR, numSamples, numChannels, currentSampleIndex, bpm, tsNum, tsDenom);
}

void VstHost::Reset() { m_impl->Reset(); }
void VstHost::ShowGui() { m_impl->ShowGui(); }
void VstHost::HideGui() { m_impl->HideGui(); }
std::string VstHost::GetState() { return m_impl->GetState(); }
bool VstHost::SetState(const std::string& state_b64) { return m_impl->SetState(state_b64); }
void VstHost::Cleanup() { m_impl->ReleasePlugin(); }
bool VstHost::IsGuiVisible() const { return m_impl->IsGuiVisible(); }
std::string VstHost::GetPluginPath() const { return m_impl->GetPluginPath(); }
void VstHost::SetParameter(uint32_t paramId, float value) {
    if (m_impl) {
        m_impl->SetParameter(paramId, value);
    }
}

int32_t VstHost::GetLastTouchedParamID() {
    if (m_impl) {
        return m_impl->GetLastTouchedParamID();
    }
    return -1;
}
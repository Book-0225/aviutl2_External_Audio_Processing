#include "VstHost.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/common/memorystream.h"
#include <windows.h>
#include <chrono>
#include <vector>
#include <string>
#include <mutex>
#include <set>
#include "StringUtils.h"
#include "Eap2Common.h"
#include "Avx2Utils.h"

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
    uint32_t PLUGIN_API addRef() override { return ++m_refCount; }
    uint32_t PLUGIN_API release() override {
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
    std::atomic<uint32_t> m_refCount{ 1 };
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
    tresult PLUGIN_API restartComponent(int32_t flags) override { return kResultOk; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
            QUERY_INTERFACE(iid, obj, FUnknown::iid, FUnknown)
            * obj = nullptr;
        return kNoInterface;
    }
    uint32_t PLUGIN_API addRef() override { return ++refCount; }
    uint32_t PLUGIN_API release() override {
        if (--refCount == 0) { delete this; return 0; }
        return refCount;
    }
private:
    std::atomic<uint32_t> refCount{ 1 };
};

static tresult SafeProcessCall(IAudioProcessor* processor, ProcessData& data) {
    if (!processor) return kResultFalse;

    _mm256_zeroupper();

    __try {
        return processor->process(data);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint("[EAP2 Error] Access Violation inside VST3::process()");
        return kResultFalse;
    }
}

class EventList : public IEventList {
public:
    EventList() {}
    virtual ~EventList() {}

    int32_t PLUGIN_API getEventCount() override { return (int32_t)events.size(); }
    tresult PLUGIN_API getEvent(int32_t index, Event& e) override {
        if (index < 0 || index >= (int32_t)events.size()) return kResultFalse;
        e = events[index];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override {
        events.push_back(e);
        return kResultOk;
    }

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(_iid, IEventList::iid) || FUnknownPrivate::iidEqual(_iid, FUnknown::iid)) {
            *obj = this; addRef(); return kResultTrue;
        }
        *obj = nullptr; return kNoInterface;
    }
    uint32_t PLUGIN_API addRef() override { return 1; }
    uint32_t PLUGIN_API release() override { return 1; }

    void clear() { events.clear(); }

private:
    std::vector<Event> events;
};

struct VstHost::Impl {
    std::vector<float> dummyBuffer;
    Impl(HINSTANCE hInst) : hInstance(hInst), isReady(false) {
        dummyBuffer.assign(4096, 0.0f);
    }
    ~Impl() { ReleasePlugin(); }

    bool LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize);
    void ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, int64_t currentSampleIndex, double bpm, int32_t tsNum, int32_t tsDenom, const std::vector<MidiEvent>& midiEvents);
    void Reset(int64_t currentSampleIndex, double bpm, int32_t timeSigNum, int32_t timeSigDenom);
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

    int32_t GetLatencySamples() {
        if (processor) return processor->getLatencySamples();
        return 0;
    }

    int32_t GetParameterCount() {
        if (controller) return controller->getParameterCount();
        return 0;
    }

    bool GetParameterInfo(int32_t index, IAudioPluginHost::ParameterInfo& info) {
        if (!controller) return false;
        Steinberg::Vst::ParameterInfo vstInfo = {};
        if (controller->getParameterInfo(index, vstInfo) == kResultOk) {
            info.step = vstInfo.stepCount;
            std::string nameUtf8 = StringUtils::WideToUtf8(reinterpret_cast<LPCWSTR>(vstInfo.title));
            strncpy_s(info.name, nameUtf8.c_str(), sizeof(info.name) - 1);
            std::string unitUtf8 = StringUtils::WideToUtf8(reinterpret_cast<LPCWSTR>(vstInfo.units));
            strncpy_s(info.unit, unitUtf8.c_str(), sizeof(info.unit) - 1);
            return true;
        }
        return false;
    }

    uint32_t GetParameterID(int32_t index) {
        if (!controller) return 0;
        Steinberg::Vst::ParameterInfo vstInfo = {};
        if (controller->getParameterInfo(index, vstInfo) == kResultOk) {
            return vstInfo.id;
        }
        return 0;
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

    std::mutex outputParamMutex;
    std::vector<std::pair<ParamID, ParamValue>> outputParamQueue;

    void ProcessGuiUpdates() {
        if (!controller) return;
        std::vector<std::pair<ParamID, ParamValue>> updates;
        {
            std::lock_guard<std::mutex> lock(outputParamMutex);
            if (outputParamQueue.empty()) return;
            updates.swap(outputParamQueue);
        }
        for (const auto& update : updates) {
            controller->setParamNormalized(update.first, update.second);
        }
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
    double currentSampleRate = 44100.0;
    double currentBpm = 120.0;
    int32_t currentTsNum = 4;
    int32_t currentTsDenom = 4;
    int32_t currentBlockSize = 1024;
    bool initialMute = false;

    std::mutex paramMutex;
    std::mutex processorUpdateMutex;
    std::vector<std::pair<ParamID, ParamValue>> pendingParamChanges;
    std::set<int32_t> activeNotes;
    std::mutex activeNotesMutex;
    EventList eventList;
    std::atomic<bool> pendingStopNotes{ false };
    void RequestStopAllNotes() {
        pendingStopNotes = true;
    }

    void SetSampleRate(double newRate) {
        if (std::abs(currentSampleRate - newRate) < 0.1) return;
        if (!isReady || !processor || !component) {
            currentSampleRate = newRate;
            return;
        }

        processor->setProcessing(false);
        ProcessSetup setup{ kRealtime, kSample32, currentBlockSize, newRate };
        processor->setupProcessing(setup);
        processor->setProcessing(true);

        currentSampleRate = newRate;
        initialMute = true;
    }
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

    int32_t numEventIn = component->getBusCount(kEvent, kInput);
    for (int32_t i = 0; i < numEventIn; ++i) component->activateBus(kEvent, kInput, i, true);

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

void VstHost::Impl::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, int64_t currentSampleIndex, double bpm, int32_t tsNum, int32_t tsDenom, const std::vector<MidiEvent>& midiEvents) {
    if (!isReady || !processor || !component || numSamples <= 0 || numChannels <= 0) {
        if (outL != inL) memcpy(outL, inL, numSamples * sizeof(float));
        if (numChannels > 1 && outR != inR) memcpy(outR, inR, numSamples * sizeof(float));
        return;
    }

    ParameterChanges inParamChanges;
    ParameterChanges outParamChanges;
    {
        std::lock_guard<std::mutex> lock(paramQueueMutex);
        if (!paramQueue.empty()) {
            for (const auto& change : paramQueue) {
                int32_t index = 0;
                IParamValueQueue* queue = inParamChanges.addParameterData(change.id, index);
                if (queue) {
                    int32_t pointIndex = 0;
                    queue->addPoint(0, change.value, pointIndex);
                }
            }
            paramQueue.clear();
        }
    }

    eventList.clear();

    if (pendingStopNotes.exchange(false)) {
        std::lock_guard<std::mutex> lock(activeNotesMutex);
        for (int32_t noteKey : activeNotes) {
            int32_t channel = (noteKey >> 8) & 0xFF;
            int32_t pitch = noteKey & 0xFF;

            Event e = {};
            e.type = Event::kNoteOffEvent;
            e.noteOff.channel = (int16)channel;
            e.noteOff.pitch = (int16)pitch;
            e.noteOff.velocity = 0.0f;
            e.noteOff.noteId = -1;
            e.sampleOffset = 0;
            eventList.addEvent(e);
        }
        activeNotes.clear();
    }

    {
        std::lock_guard<std::mutex> lock(activeNotesMutex);
        for (const auto& me : midiEvents) {
            Event e = {};
            e.busIndex = 0;
            e.sampleOffset = me.deltaFrames;
            e.ppqPosition = 0;
            e.flags = 0;

            if ((me.status & 0xF0) == 0x90 && me.data2 > 0) {
                e.type = Event::kNoteOnEvent;
                e.noteOn.channel = me.status & 0x0F;
                e.noteOn.pitch = me.data1;
                e.noteOn.velocity = me.data2 / 127.0f;
                e.noteOn.length = 0;
                e.noteOn.noteId = -1;
                eventList.addEvent(e);

                activeNotes.insert((e.noteOn.channel << 8) | e.noteOn.pitch);
            }
            else if ((me.status & 0xF0) == 0x80 || ((me.status & 0xF0) == 0x90 && me.data2 == 0)) {
                e.type = Event::kNoteOffEvent;
                e.noteOff.channel = me.status & 0x0F;
                e.noteOff.pitch = me.data1;
                e.noteOff.velocity = me.data2 / 127.0f;
                e.noteOff.noteId = -1;
                eventList.addEvent(e);

                activeNotes.erase((e.noteOff.channel << 8) | e.noteOff.pitch);
            }
        }
    }

    float* silence = GetDummyBuffer(numSamples);
    Avx2Utils::FillBufferAVX2(silence, numSamples, 0.0f);

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;
    data.inputParameterChanges = &inParamChanges;
    data.inputEvents = &eventList;
    data.outputParameterChanges = &outParamChanges;

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
                    for (int32_t ch = 2; ch < info.channelCount; ++ch) {
                        inPtrs[i][ch] = silence;
                    }
                }
                else {
                    for (int32_t ch = 0; ch < info.channelCount; ++ch) {
                        inPtrs[i][ch] = silence;
                    }
                    inBufs[i].silenceFlags = (1ULL << info.channelCount) - 1;
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
                    for (int32_t ch = 2; ch < info.channelCount; ++ch) {
                        outPtrs[i][ch] = silence;
                    }
                }
                else {
                    for (int32_t ch = 0; ch < info.channelCount; ++ch) {
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
    ctx.state = ProcessContext::kPlaying |
        ProcessContext::kProjectTimeMusicValid |
        ProcessContext::kTempoValid |
        ProcessContext::kTimeSigValid |
        ProcessContext::kBarPositionValid |
        ProcessContext::kCycleValid |
        ProcessContext::kSystemTimeValid;
    ctx.sampleRate = currentSampleRate;
    ctx.projectTimeSamples = currentSampleIndex;
    ctx.tempo = bpm;
    currentBpm = bpm;
    ctx.timeSigNumerator = tsNum;
    currentTsNum = tsNum;
    ctx.timeSigDenominator = tsDenom;
    currentTsDenom = tsDenom;
    if (bpm > 0.0) {
        double samplesPerBeat = (currentSampleRate * 60.0) / bpm;
        double ppq = (double)currentSampleIndex / samplesPerBeat;
        ctx.projectTimeMusic = ppq;
        if (tsNum > 0 && tsDenom > 0) {
            double quarterNotesPerBar = (double)tsNum * 4.0 / (double)tsDenom;
            int64_t currentBarIndex = (int64_t)(ppq / quarterNotesPerBar);
            ctx.barPositionMusic = (double)currentBarIndex * quarterNotesPerBar;
            ctx.cycleStartMusic = ctx.barPositionMusic;
            ctx.cycleEndMusic = ctx.barPositionMusic + quarterNotesPerBar;
        }
    }
    ctx.systemTime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    data.processContext = &ctx;
    tresult result = kResultFalse;
    result = SafeProcessCall(processor, data);

    if (result == kResultOk) {
        if (initialMute) {
            Avx2Utils::FillBufferAVX2(outL, numSamples, 0.0f);
            if (numChannels > 1)  Avx2Utils::FillBufferAVX2(outR, numSamples, 0.0f);
            initialMute = false;
        }
        else {
            int32_t numOutParams = outParamChanges.getParameterCount();
            if (numOutParams > 0) {
                std::lock_guard<std::mutex> lock(outputParamMutex);
                for (int32_t i = 0; i < numOutParams; ++i) {
                    IParamValueQueue* queue = outParamChanges.getParameterData(i);
                    if (queue && queue->getPointCount() > 0) {
                        ParamID id = queue->getParameterId();
                        ParamValue value;
                        int32_t sampleOffset;
                        if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) == kResultTrue) {
                            outputParamQueue.push_back({ id, value });
                        }
                    }
                }
            }
        }
    }
    else {
        if (outL != inL) Avx2Utils::CopyBufferAVX2(outL, inL, numSamples);
        if (numChannels > 1 && outR != inR) Avx2Utils::CopyBufferAVX2(outR, inR, numSamples);
    }
}

void VstHost::Impl::Reset(int64_t currentSampleIndex, double bpm, int32_t timeSigNum, int32_t timeSigDenom) {
    if (!isReady || !component || !processor) return;
    currentBpm = bpm;
    currentTsNum = timeSigNum;
    currentTsDenom = timeSigDenom;
    pendingStopNotes = true;

    {
        std::lock_guard<std::mutex> lock(paramQueueMutex);
        paramQueue.clear();
    }

    {
        std::lock_guard<std::mutex> lock(activeNotesMutex);
        activeNotes.clear();
    }

    {
        std::lock_guard<std::mutex> lock(outputParamMutex);
        outputParamQueue.clear();
    }

    processor->setProcessing(false);
    component->setActive(false);
    component->setActive(true);
    processor->setProcessing(true);
    initialMute = true;

    EventList resetEventList;
    
    for (int32_t channel = 0; channel < 16; ++channel) {
        for (int32_t pitch = 0; pitch < 128; ++pitch) {
            Event e = {};
            e.type = Event::kNoteOffEvent;
            e.noteOff.channel = (int16)channel;
            e.noteOff.pitch = (int16)pitch;
            e.noteOff.velocity = 0.0f;
            e.noteOff.noteId = -1;
            e.sampleOffset = 0;
            resetEventList.addEvent(e);
        }
    }
    
    int32_t flushBlocks = 20;
    int32_t blockSize = currentBlockSize;
    float* silence = GetDummyBuffer(blockSize);
    Avx2Utils::FillBufferAVX2(silence, blockSize, 0.0f);

    for (int32_t iterations = 0; iterations < flushBlocks; ++iterations) {
        ParameterChanges emptyParamChanges;
        ParameterChanges emptyOutParamChanges;
        ProcessData data{};
        data.numSamples = blockSize;
        data.symbolicSampleSize = kSample32;
        data.inputParameterChanges = &emptyParamChanges;
        data.inputEvents = &resetEventList;
        data.outputParameterChanges = &emptyOutParamChanges;
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
                inBufs[i].silenceFlags = (1ULL << info.channelCount) - 1;
                if (info.channelCount > 0) {
                    inPtrs[i].resize(info.channelCount);
                    for (int32_t ch = 0; ch < info.channelCount; ++ch) inPtrs[i][ch] = silence;
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
                    for (int32_t ch = 0; ch < info.channelCount; ++ch) outPtrs[i][ch] = silence;
                    outBufs[i].channelBuffers32 = outPtrs[i].data();
                }
            }
            data.numOutputs = numOutputs;
            data.outputs = outBufs.data();
        }

        ProcessContext ctx{};
        ctx.state = ProcessContext::kPlaying |
            ProcessContext::kProjectTimeMusicValid |
            ProcessContext::kTempoValid |
            ProcessContext::kTimeSigValid |
            ProcessContext::kBarPositionValid |
            ProcessContext::kCycleValid |
            ProcessContext::kSystemTimeValid;
        ctx.sampleRate = currentSampleRate;
        ctx.projectTimeSamples = currentSampleIndex;
        ctx.tempo = currentBpm;
        ctx.timeSigNumerator = currentTsNum;
        ctx.timeSigDenominator = currentTsDenom;
        if (currentBpm > 0.0) {
            double samplesPerBeat = (currentSampleRate * 60.0) / currentBpm;
            double ppq = (double)currentSampleIndex / samplesPerBeat;
            ctx.projectTimeMusic = ppq;
            if (currentTsNum > 0 && currentTsDenom > 0) {
                double quarterNotesPerBar = (double)currentTsNum * 4.0 / (double)currentTsDenom;
                int64_t currentBarIndex = (int64_t)(ppq / quarterNotesPerBar);
                ctx.barPositionMusic = (double)currentBarIndex * quarterNotesPerBar;
                ctx.cycleStartMusic = ctx.barPositionMusic;
                ctx.cycleEndMusic = ctx.barPositionMusic + quarterNotesPerBar;
            }
        }
        ctx.systemTime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        data.processContext = &ctx;
        SafeProcessCall(processor, data);
    }
    
    resetEventList.clear();
}

void VstHost::Impl::ShowGui() {
    if (guiWindow && IsWindow(guiWindow)) {
        ShowWindow(guiWindow, SW_SHOW); SetForegroundWindow(guiWindow); return;
    }
    if (!controller) return;

    plugView = owned(controller->createView("editor"));
    if (!plugView) {
        DbgPrint("[VST3 GUI] Failed to create editor view");
        return;
    }

    ViewRect vr; plugView->getSize(&vr);

    // Check if plugin supports resizing (for logging purposes)
    bool canResize = (plugView->canResize() == kResultTrue);
    DbgPrint("[VST3 GUI] Plugin canResize: %hs", canResize ? "yes" : "no");

    // Always allow window resizing regardless of plugin support
    DWORD windowStyle = settings.vst.forceResize ? WS_OVERLAPPEDWINDOW : WS_OVERLAPPED | WS_CAPTION;

    RECT rc = { 0, 0, vr.right - vr.left, vr.bottom - vr.top };
    AdjustWindowRectEx(&rc, windowStyle, FALSE, 0);

    WNDCLASS wc{};
    wc.lpfnWndProc = [](HWND hWnd, uint32_t msg, WPARAM wp, LPARAM lp) -> LRESULT {
        VstHost::Impl* self = nullptr;
        if (msg == WM_CREATE) {
            self = (VstHost::Impl*)((CREATESTRUCT*)lp)->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
            SetTimer(hWnd, 1001, 30, NULL);
        }
        else {
            self = (VstHost::Impl*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        }

        if (msg == WM_TIMER && wp == 1001) {
            if (self) {
                self->ProcessGuiUpdates();
            }
            return 0;
        }

        if (msg == WM_SIZE && self && self->plugView) {
            // Always try to resize the plugin view, even if canResize() is not supported
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            ViewRect newSize;
            newSize.left = 0;
            newSize.top = 0;
            newSize.right = clientRect.right - clientRect.left;
            newSize.bottom = clientRect.bottom - clientRect.top;

            ViewRect currentSize;
            if (self->plugView->getSize(&currentSize) == kResultOk) {
                if (currentSize.getWidth() != newSize.getWidth() ||
                    currentSize.getHeight() != newSize.getHeight()) {
                    // Try to resize even if canResize() returns false
                    // Some plugins don't implement canResize() correctly
                    if (self->plugView->onSize(&newSize) == kResultOk) DbgPrint("[VST3 GUI] Resized to %dx%d", newSize.getWidth(), newSize.getHeight());
                    else DbgPrint("[VST3 GUI] Resize request rejected by plugin");
                }
            }
            return 0;
        }

        if (msg == WM_CLOSE) return 0;

        if (msg == WM_DESTROY) {
            KillTimer(hWnd, 1001);

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
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    // Register window class only if not already registered
    WNDCLASS existingClass;
    if (!GetClassInfo(hInstance, wc.lpszClassName, &existingClass)) {
        if (!RegisterClass(&wc)) {
            DbgPrint("[VST3 GUI] Failed to register window class");
            plugView.reset();
            return;
        }
    }

    // Extract plugin name from path for window title
    std::wstring pluginName = L"VST3 Plugin";
    if (!currentPluginPath.empty()) {
        std::wstring wPath = StringUtils::Utf8ToWide(currentPluginPath);
        size_t lastSlash = wPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            pluginName = wPath.substr(lastSlash + 1);
            // Remove extension
            size_t lastDot = pluginName.find_last_of(L".");
            if (lastDot != std::wstring::npos) pluginName = pluginName.substr(0, lastDot);
        }
    }

    guiWindow = CreateWindowEx(0, wc.lpszClassName, pluginName.c_str(), windowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, this);
    if (!guiWindow) {
        DbgPrint("[VST3 GUI] Failed to create window (Error: %d)", GetLastError());
        plugView.reset();
        return;
    }

    windowController = new WindowController(plugView, guiWindow);
    windowController->connect();
    if (plugView->attached(guiWindow, kPlatformTypeHWND) != kResultOk) {
        DbgPrint("[VST3 GUI] Failed to attach plugin view to window");
        DestroyWindow(guiWindow);
        return;
    }

    DbgPrint("[VST3 GUI] Plugin GUI window created successfully (canResize: %hs)", canResize ? "yes" : "no");
    ShowWindow(guiWindow, SW_SHOW);
}

void VstHost::Impl::HideGui() {
    if (guiWindow) DestroyWindow(guiWindow);
}

std::string VstHost::Impl::GetState() {
    if (!provider || !component || !controller) return "";
    try {
        MemoryStream cStream, tStream;
        if (component->getState(&cStream) != kResultOk) return "";

        if (controller->getState(&tStream) != kResultOk) DbgPrint("[EAP2 Error] Exception inside VST3 GetState");

        int64_t cs = cStream.getSize();
        int64_t ts = tStream.getSize();
        if (cs < 0 || ts < 0 || cs > 200 * 1024 * 1024) return "";

        MemoryStream full;
        int32_t b;
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
    int32_t br;
    int64 cs = 0, ts = 0;
    stream.read(&cs, sizeof(cs), &br);
    if (cs > 0 && component) {
        std::vector<BYTE> d(cs); stream.read(d.data(), (int32_t)cs, &br);
        MemoryStream s(d.data(), cs);
        if (component->setState(&s) != kResultOk) return false;
    }
    stream.read(&ts, sizeof(ts), &br);
    if (ts > 0 && controller) {
        std::vector<BYTE> d(ts); stream.read(d.data(), (int32_t)ts, &br);
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

void VstHost::SetSampleRate(double sampleRate) {
    m_impl->SetSampleRate(sampleRate);
}

double VstHost::GetSampleRate() const {
    return m_impl->currentSampleRate;
}

void VstHost::ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, int64_t currentSampleIndex, double bpm, int32_t tsNum, int32_t tsDenom, const std::vector<MidiEvent>& midiEvents) {
    m_impl->ProcessAudio(inL, inR, outL, outR, numSamples, numChannels, currentSampleIndex, bpm, tsNum, tsDenom, midiEvents);
}

void VstHost::Reset(int64_t currentSampleIndex, double bpm, int32_t timeSigNum, int32_t timeSigDenom) { m_impl->Reset(currentSampleIndex, bpm, timeSigNum, timeSigDenom); }
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

int32_t VstHost::GetLatencySamples() {
    return m_impl->GetLatencySamples();
}

int32_t VstHost::GetParameterCount() {
    return m_impl->GetParameterCount();
}

bool VstHost::GetParameterInfo(int32_t index, ParameterInfo& info) {
    return m_impl->GetParameterInfo(index, info);
}

uint32_t VstHost::GetParameterID(int32_t index) {
    return m_impl->GetParameterID(index);
}
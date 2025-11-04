#include "AudioPluginFactory.h"
#include "VstHost.h" 
#include "ClapHost.h" 
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <atomic>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

    class MyHostApplication : public IHostApplication {
    public:
        tresult PLUGIN_API getName(String128 name) override {
            str8ToStr16(name, "AviUtl2 VST3 Host", 128);
            return kResultOk;
        }

        tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj) override {
            if (FUnknownPrivate::iidEqual(cid, IMessage::iid)) {
                *obj = new HostMessage;
                return kResultOk;
            }
            *obj = nullptr;
            return kNoInterface;
        }

        tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
            if (FUnknownPrivate::iidEqual(_iid, IHostApplication::iid) ||
                FUnknownPrivate::iidEqual(_iid, FUnknown::iid)) {
                *obj = static_cast<IHostApplication*>(this);
                addRef();
                return kResultOk;
            }
            *obj = nullptr;
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

    FUnknownPtr<MyHostApplication> g_host_app_context;

}

bool AudioPluginFactory::Initialize(HINSTANCE hInst) {
    if (!g_host_app_context) {
        g_host_app_context = FUnknownPtr<MyHostApplication>(new MyHostApplication);
        PluginContextFactory::instance().setPluginContext(g_host_app_context);
    }
    return true;
}

void AudioPluginFactory::Uninitialize() {
    PluginContextFactory::instance().setPluginContext(nullptr);
    g_host_app_context = nullptr;
}

std::unique_ptr<IAudioPluginHost> AudioPluginFactory::Create(PluginType type, HINSTANCE hInst) {
    switch (type) {
    case PluginType::VST3:
        return std::make_unique<VstHost>(hInst);

    case PluginType::CLAP:
        return std::make_unique<ClapHost>(hInst);

    default:
        return nullptr;
    }
}
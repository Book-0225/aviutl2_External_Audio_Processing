#pragma once
#include "IAudioPluginHost.h"
#include "PluginType.h"
#include <memory>
#include <wtypes.h>

class AudioPluginFactory {
public:
    static bool Initialize(HINSTANCE hInst);
    static void Uninitialize();
    static std::unique_ptr<IAudioPluginHost> Create(PluginType type, HINSTANCE hInst);
};
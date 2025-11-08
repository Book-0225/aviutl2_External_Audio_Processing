#pragma once
#include <string>

class IAudioPluginHost {
public:
    virtual ~IAudioPluginHost() = default;

    virtual bool LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize) = 0;

    virtual void ProcessAudio(
        const float* inL, const float* inR,
        float* outL, float* outR,
        int32_t numSamples,
        int32_t numChannels
    ) = 0;
    virtual void Reset() = 0;

    virtual void ShowGui() = 0;
    virtual void HideGui() = 0;

    virtual std::string GetState() = 0;
    virtual bool SetState(const std::string& state_b64) = 0;

    virtual void Cleanup() = 0;
    virtual bool IsGuiVisible() const = 0;
    virtual std::string GetPluginPath() const = 0;
};
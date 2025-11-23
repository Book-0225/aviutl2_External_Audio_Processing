#pragma once
#include "IAudioPluginHost.h"
#include <windows.h>
#include <string>
#include <memory>
#include <atomic>

class VstHost : public IAudioPluginHost {
public:
    VstHost(HINSTANCE hInstance);
    ~VstHost() override;
    bool LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize) override;
    void ProcessAudio(const float* inL, const float* inR, float* outL, float* outR, int32_t numSamples, int32_t numChannels, int64_t currentSampleIndex, double bpm, int32_t tsNum, int32_t tsDenom) override;
    void Reset() override;
    void ShowGui() override;
    void HideGui() override;
    std::string GetState() override;
    bool SetState(const std::string& state_b64) override;
    void Cleanup() override;
    bool IsGuiVisible() const override;
    std::string GetPluginPath() const override;
    void SetParameter(uint32_t paramId, float value) override;
    int32_t GetLastTouchedParamID() override;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
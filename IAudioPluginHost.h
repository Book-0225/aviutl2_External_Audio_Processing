#pragma once
#include <string>
#include <vector>

class IAudioPluginHost {
public:
    struct MidiEvent {
        int32_t deltaFrames;
        uint8_t status;
        uint8_t data1;
        uint8_t data2;
    };

    virtual ~IAudioPluginHost() = default;

    virtual bool LoadPlugin(const std::string& path, double sampleRate, int32_t blockSize) = 0;

    virtual void ProcessAudio(
        const float* inL, const float* inR,
        float* outL, float* outR,
        int32_t numSamples,
        int32_t numChannels,
        int64_t currentSampleIndex,
        double bpm,
        int32_t timeSigNum,
        int32_t timeSigDenom,
        const std::vector<MidiEvent>& midiEvents
    ) = 0;
    virtual void Reset() = 0;

    virtual void ShowGui() = 0;
    virtual void HideGui() = 0;

    virtual std::string GetState() = 0;
    virtual bool SetState(const std::string& state_b64) = 0;

    virtual void Cleanup() = 0;
    virtual bool IsGuiVisible() const = 0;
    virtual std::string GetPluginPath() const = 0;

    virtual void SetParameter(uint32_t paramId, float value) = 0;
    virtual int32_t GetLastTouchedParamID() = 0;

};
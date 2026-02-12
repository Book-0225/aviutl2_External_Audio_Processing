#pragma once
#include <filesystem>
#include <vector>

struct RawMidiEvent {
    uint32_t absoluteTick;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

struct TempoEvent {
    uint32_t absoluteTick;
    uint32_t mpqn;
    double bpm;
};

struct TempoMapEntry {
    double time;
    uint32_t tick;
    uint32_t mpqn;
};

struct TimeSignatureEvent {
    uint32_t absoluteTick;
    uint8_t numerator;
    uint8_t denominator;
};

class MidiParser {
public:
    MidiParser();
    ~MidiParser();

    bool Load(const std::filesystem::path& path);
    void Clear();
    const std::vector<RawMidiEvent>& GetEvents() const { return m_events; }
    const std::vector<TempoEvent>& GetTempoEvents() const { return m_tempoEvents; }
    uint16_t GetTPQN() const { return m_tpqn; }
    int64_t GetTickAtTime(double time) const;
    double GetBpmAtTime(double time) const;
    TimeSignatureEvent GetTimeSignatureAt(uint32_t tick) const;

    static float CalculateFrequency(int32_t noteNumber, int32_t pitchBendVal, float bendRangeSemiTones = 2.0f) {
        float bend = (pitchBendVal - 8192) / 8192.0f;
        float note = noteNumber + (bend * bendRangeSemiTones);
        return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    }

    static int32_t CombineBytes14(uint8_t lsb, uint8_t msb) {
        return (msb << 7) | lsb;
    }

private:
    void BuildTempoMap();

    uint16_t m_tpqn = 480;
    std::vector<RawMidiEvent> m_events;
    std::vector<TempoEvent> m_tempoEvents;
    std::vector<TimeSignatureEvent> m_timeSigEvents;
    std::vector<TempoMapEntry> m_tempoMap;
};
#pragma once
#include <string>
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

    bool Load(const std::string& path);
    void Clear();

    const std::vector<RawMidiEvent>& GetEvents() const { return m_events; }
    const std::vector<TempoEvent>& GetTempoEvents() const { return m_tempoEvents; }
    uint16_t GetTPQN() const { return m_tpqn; }
    int64_t GetTickAtTime(double time) const;
    double GetBpmAtTime(double time) const;
    TimeSignatureEvent GetTimeSignatureAt(uint32_t tick) const {
        if (m_timeSigEvents.empty()) return { 0, 4, 4 };
        for (auto it = m_timeSigEvents.rbegin(); it != m_timeSigEvents.rend(); ++it) if (it->absoluteTick <= tick) return *it;
        return m_timeSigEvents.front();
    }

private:
    void BuildTempoMap();

    uint32_t m_tpqn = 480;
    std::vector<RawMidiEvent> m_events;
    std::vector<TempoEvent> m_tempoEvents;
    std::vector<TempoMapEntry> m_tempoMap;
    std::vector<TimeSignatureEvent> m_timeSigEvents;
};
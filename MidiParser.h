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

class MidiParser {
public:
    MidiParser();
    ~MidiParser();

    bool Load(const std::string& path);
    void Clear();

    const std::vector<RawMidiEvent>& GetEvents() const { return m_events; }
    uint16_t GetTPQN() const { return m_tpqn; }
    int64_t GetTickAtTime(double time) const;
    double GetBpmAtTime(double time) const;

private:
    void BuildTempoMap();

    uint32_t m_tpqn = 480;
    std::vector<RawMidiEvent> m_events;
    std::vector<TempoEvent> m_tempoEvents;
    std::vector<TempoMapEntry> m_tempoMap;
};
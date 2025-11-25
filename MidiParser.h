#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct RawMidiEvent {
    uint32_t absoluteTick;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

class MidiParser {
public:
    MidiParser();
    ~MidiParser();

    bool Load(const std::string& path);
    void Clear();

    uint32_t GetTPQN() const { return m_tpqn; }
    const std::vector<RawMidiEvent>& GetEvents() const { return m_events; }

private:
    uint32_t m_tpqn = 480;
    std::vector<RawMidiEvent> m_events;
};

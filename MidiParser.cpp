#include "MidiParser.h"
#include <fstream>
#include <algorithm>

static uint16_t ReadBE16(std::ifstream& f) {
    uint8_t b[2];
    f.read((char*)b, 2);
    return (b[0] << 8) | b[1];
}

static uint32_t ReadBE32(std::ifstream& f) {
    uint8_t b[4];
    f.read((char*)b, 4);
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

static uint32_t ReadVarInt(std::ifstream& f) {
    uint32_t value = 0;
    uint8_t byte;
    do {
        f.read((char*)&byte, 1);
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);
    return value;
}

MidiParser::MidiParser() {}
MidiParser::~MidiParser() {}

void MidiParser::Clear() {
    m_events.clear();
    m_tempoEvents.clear();
    m_tpqn = 480;
}

bool MidiParser::Load(const std::string& path) {
    Clear();
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    char chunkType[4];
    f.read(chunkType, 4);
    if (memcmp(chunkType, "MThd", 4) != 0) return false;

    uint32_t headerLength = ReadBE32(f);
    if (headerLength < 6) return false;

    uint16_t format = ReadBE16(f);
    uint16_t numTracks = ReadBE16(f);
    m_tpqn = ReadBE16(f);

    if (headerLength > 6) {
        f.seekg(headerLength - 6, std::ios::cur);
    }

    for (int32_t t = 0; t < numTracks; ++t) {
        f.read(chunkType, 4);
        while (memcmp(chunkType, "MTrk", 4) != 0) {
            uint32_t len = ReadBE32(f);
            f.seekg(len, std::ios::cur);
            if (!f.read(chunkType, 4)) return false;
        }

        uint32_t trackLength = ReadBE32(f);
        std::streampos trackStart = f.tellg();
        std::streampos trackEnd = trackStart + (std::streampos)trackLength;

        uint32_t currentTick = 0;
        uint8_t runningStatus = 0;

        while (f.tellg() < trackEnd) {
            uint32_t deltaTime = ReadVarInt(f);
            currentTick += deltaTime;

            uint8_t status = 0;
            f.read((char*)&status, 1);

            if (status >= 0xF0) {
                if (status == 0xFF) {
                    uint8_t type;
                    f.read((char*)&type, 1);
                    uint32_t len = ReadVarInt(f);

                    if (type == 0x51 && len == 3) {
                        uint8_t bytes[3];
                        f.read((char*)bytes, 3);

                        uint32_t microseconds = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];

                        if (microseconds > 0) {
                            double bpm = 60000000.0 / (double)microseconds;
                            m_tempoEvents.push_back({ currentTick, microseconds, bpm });
                        }
                    }
                    else {
                        f.seekg(len, std::ios::cur);
                    }
                }
                else if (status == 0xF0 || status == 0xF7) {
                    uint32_t len = ReadVarInt(f);
                    f.seekg(len, std::ios::cur);
                }
                else {
                    if (status == 0xF1 || status == 0xF3) f.seekg(1, std::ios::cur);
                    else if (status == 0xF2) f.seekg(2, std::ios::cur);
                }
                runningStatus = 0;
            }
            else if (status & 0x80) {
                runningStatus = status;
                uint8_t data1 = 0;
                f.read((char*)&data1, 1);
                uint8_t type = status & 0xF0;
                uint8_t data2 = 0;
                if (type != 0xC0 && type != 0xD0) {
                    f.read((char*)&data2, 1);
                }

                m_events.push_back({ currentTick, status, data1, data2 });
            }
            else {
                if (runningStatus == 0) {
                    break;
                }
                uint8_t data1 = status;
                uint8_t type = runningStatus & 0xF0;
                uint8_t data2 = 0;
                if (type != 0xC0 && type != 0xD0) {
                    f.read((char*)&data2, 1);
                }
                m_events.push_back({ currentTick, runningStatus, data1, data2 });
            }
        }
        f.seekg(trackEnd);
    }

    std::sort(m_events.begin(), m_events.end(), [](const RawMidiEvent& a, const RawMidiEvent& b) {
        return a.absoluteTick < b.absoluteTick;
        });

    std::sort(m_tempoEvents.begin(), m_tempoEvents.end(), [](const TempoEvent& a, const TempoEvent& b) {
        return a.absoluteTick < b.absoluteTick;
        });
    BuildTempoMap();
    return true;
}

void MidiParser::BuildTempoMap() {
    m_tempoMap.clear();
    uint32_t currentTick = 0;
    double currentTime = 0.0;
    uint32_t currentMpqn = 500000;

    m_tempoMap.push_back({ currentTime, currentTick, currentMpqn });

    for (const auto& te : m_tempoEvents) {
        if (te.absoluteTick <= currentTick) {
            currentMpqn = te.mpqn;
            if (!m_tempoMap.empty()) m_tempoMap.back().mpqn = currentMpqn;
            continue;
        }

        uint32_t deltaTick = te.absoluteTick - currentTick;
        double deltaTime = (double)deltaTick * (double)currentMpqn / (1000000.0 * (double)m_tpqn);

        currentTime += deltaTime;
        currentTick = te.absoluteTick;
        currentMpqn = te.mpqn;

        m_tempoMap.push_back({ currentTime, currentTick, currentMpqn });
    }
}

int64_t MidiParser::GetTickAtTime(double time) const {
    if (m_tempoMap.empty()) return 0;

    size_t idx = 0;
    for (size_t i = 0; i < m_tempoMap.size(); ++i) {
        if (m_tempoMap[i].time > time) break;
        idx = i;
    }

    const auto& entry = m_tempoMap[idx];
    double dt = time - entry.time;
    if (dt < 0) dt = 0;

    double ticks = dt * 1000000.0 * (double)m_tpqn / (double)entry.mpqn;

    return entry.tick + (int64_t)(ticks + 0.5);
}

double MidiParser::GetBpmAtTime(double time) const {
    if (m_tempoMap.empty()) return 120.0;

    size_t idx = 0;
    for (size_t i = 0; i < m_tempoMap.size(); ++i) {
        if (m_tempoMap[i].time > time) break;
        idx = i;
    }

    uint32_t mpqn = m_tempoMap[idx].mpqn;
    return (mpqn > 0) ? (60000000.0 / (double)mpqn) : 120.0;
}

#include "MidiParser.h"
#include <fstream>
#include <algorithm>
#include <cstring>

static uint16_t ReadBE16(std::ifstream& f) {
    uint8_t b[2];
    if (!f.read((char*)b, 2)) return 0;
    return (b[0] << 8) | b[1];
}

static uint32_t ReadBE32(std::ifstream& f) {
    uint8_t b[4];
    if (!f.read((char*)b, 4)) return 0;
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
}

static uint32_t ReadVarInt(std::ifstream& f) {
    uint32_t value = 0;
    uint8_t byte;
    for (int32_t i = 0; i < 4; ++i) {
        if (!f.read((char*)&byte, 1)) break;
        value = (value << 7) | (byte & 0x7F);
        if (!(byte & 0x80)) break;
    }
    return value;
}

MidiParser::MidiParser() {}
MidiParser::~MidiParser() {}

void MidiParser::Clear() {
    m_events.clear();
    m_tempoEvents.clear();
    m_timeSigEvents.clear();
    m_tempoMap.clear();
    m_tpqn = 480;
}

bool MidiParser::Load(const std::filesystem::path& path) {
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
    if (headerLength > 6) f.seekg(headerLength - 6, std::ios::cur);
    for (int32_t t = 0; t < numTracks; ++t) {
        while (true) {
            if (!f.read(chunkType, 4)) return false;
            uint32_t len = ReadBE32(f);
            if (memcmp(chunkType, "MTrk", 4) == 0) {
                std::streampos trackStart = f.tellg();
                std::streampos trackEnd = trackStart + (std::streampos)len;
                uint32_t currentTick = 0;
                uint8_t runningStatus = 0;
                while (f.tellg() < trackEnd) {
                    uint32_t deltaTime = ReadVarInt(f);
                    currentTick += deltaTime;
                    uint8_t status = 0;
                    if (!f.read((char*)&status, 1)) break;
                    if (status < 0x80) {
                        if (runningStatus == 0) break;
                        uint8_t data1 = status;
                        uint8_t type = runningStatus & 0xF0;
                        if (type == 0xC0 || type == 0xD0) {
                            m_events.push_back({ currentTick, runningStatus, data1, 0 });
                        }
                        else {
                            uint8_t data2 = 0;
                            f.read((char*)&data2, 1);
                            m_events.push_back({ currentTick, runningStatus, data1, data2 });
                        }
                    }
                    else if (status == 0xFF) {
                        uint8_t type;
                        f.read((char*)&type, 1);
                        uint32_t metaLen = ReadVarInt(f);
                        if (type == 0x51 && metaLen == 3) {
                            uint8_t b[3];
                            f.read((char*)b, 3);
                            uint32_t mpqn = (b[0] << 16) | (b[1] << 8) | b[2];
                            if (mpqn > 0) {
                                double bpm = 60000000.0 / (double)mpqn;
                                m_tempoEvents.push_back({ currentTick, mpqn, bpm });
                            }
                        }
                        else if (type == 0x58 && metaLen >= 4) {
                            uint8_t d[4];
                            f.read((char*)d, 4);
                            if (metaLen > 4) f.seekg(metaLen - 4, std::ios::cur);
                            uint8_t num = d[0];
                            uint8_t den = 1 << d[1];
                            m_timeSigEvents.push_back({ currentTick, num, den });
                        }
                        else if (type == 0x2F) {
                            f.seekg(trackEnd);
                            break;
                        }
                        else {
                            f.seekg(metaLen, std::ios::cur);
                        }
                    }
                    else if (status == 0xF0 || status == 0xF7) {
                        uint32_t sysExLen = ReadVarInt(f);
                        f.seekg(sysExLen, std::ios::cur);
                        runningStatus = 0;
                    }
                    else {
                        runningStatus = status;
                        uint8_t type = status & 0xF0;
                        uint8_t data1 = 0;
                        f.read((char*)&data1, 1);
                        if (type == 0xC0 || type == 0xD0) {
                            m_events.push_back({ currentTick, status, data1, 0 });
                        }
                        else {
                            uint8_t data2 = 0;
                            f.read((char*)&data2, 1);
                            m_events.push_back({ currentTick, status, data1, data2 });
                        }
                    }
                }
                f.seekg(trackEnd);
                break;
            }
            else {
                f.seekg(len, std::ios::cur);
            }
        }
    }
    std::stable_sort(m_events.begin(), m_events.end(), [](const RawMidiEvent& a, const RawMidiEvent& b) {
        return a.absoluteTick < b.absoluteTick;
                     });

    std::stable_sort(m_tempoEvents.begin(), m_tempoEvents.end(), [](const TempoEvent& a, const TempoEvent& b) {
        return a.absoluteTick < b.absoluteTick;
                     });

    std::stable_sort(m_timeSigEvents.begin(), m_timeSigEvents.end(), [](const TimeSignatureEvent& a, const TimeSignatureEvent& b) {
        return a.absoluteTick < b.absoluteTick;
                     });
    BuildTempoMap();
    return true;
}

void MidiParser::BuildTempoMap() {
    m_tempoMap.clear();
    uint32_t currentMpqn = 500000;
    if (!m_tempoEvents.empty() && m_tempoEvents[0].absoluteTick == 0) currentMpqn = m_tempoEvents[0].mpqn;
    double currentTime = 0.0;
    uint32_t lastTick = 0;
    m_tempoMap.push_back({ 0.0, 0, currentMpqn });
    for (const auto& te : m_tempoEvents) {
        if (te.absoluteTick <= lastTick) {
            currentMpqn = te.mpqn;
            m_tempoMap.back().mpqn = currentMpqn;
            continue;
        }
        uint32_t deltaTick = te.absoluteTick - lastTick;
        double deltaTime = (double)deltaTick * (double)currentMpqn / (1000000.0 * (double)m_tpqn);
        currentTime += deltaTime;
        lastTick = te.absoluteTick;
        currentMpqn = te.mpqn;
        m_tempoMap.push_back({ currentTime, lastTick, currentMpqn });
    }
}

int64_t MidiParser::GetTickAtTime(double time) const {
    if (m_tempoMap.empty()) return (int64_t)(time * 120.0 * m_tpqn / 60.0);
    auto it = std::upper_bound(m_tempoMap.begin(), m_tempoMap.end(), time,
                               [](double t, const TempoMapEntry& entry) {
                                   return t < entry.time;
                               });

    if (it == m_tempoMap.begin()) return 0;
    --it;
    double dt = time - it->time;
    double ticks = dt * 1000000.0 * (double)m_tpqn / (double)it->mpqn;
    return it->tick + (int64_t)(ticks + 0.5);
}

double MidiParser::GetBpmAtTime(double time) const {
    if (m_tempoMap.empty()) return 120.0;
    auto it = std::upper_bound(m_tempoMap.begin(), m_tempoMap.end(), time,
                               [](double t, const TempoMapEntry& entry) {
                                   return t < entry.time;
                               });
    if (it == m_tempoMap.begin()) return 60000000.0 / m_tempoMap[0].mpqn;
    --it;
    return (it->mpqn > 0) ? (60000000.0 / (double)it->mpqn) : 120.0;
}

TimeSignatureEvent MidiParser::GetTimeSignatureAt(uint32_t tick) const {
    if (m_timeSigEvents.empty()) return { 0, 4, 4 };
    for (auto it = m_timeSigEvents.rbegin(); it != m_timeSigEvents.rend(); ++it) if (it->absoluteTick <= tick) return *it;
    return m_timeSigEvents.front();
}
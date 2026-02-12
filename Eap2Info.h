#pragma once
#include <Windows.h>
#include <sstream>
#include <iomanip>

#if _MSC_VER >= 1950
#define VS_VERSION 2026
#elif _MSC_VER >= 1930
#define VS_VERSION 2022
#elif _MSC_VER >= 1920
#define VS_VERSION 2019
#elif _MSC_VER >= 1910
#define VS_VERSION 2017
#else
#define VS_VERSION -1
#endif

struct Version {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t patch = 0;
    uint16_t letter = 0;
    auto to_tuple() const {
        return std::tie(major, minor, patch, letter);
    }
    bool operator<(const Version& other) const { return to_tuple() < other.to_tuple(); }
    bool operator>(const Version& other) const { return other < *this; }
    bool operator<=(const Version& other) const { return !(*this > other); }
    bool operator>=(const Version& other) const { return !(*this < other); }
    bool operator==(const Version& other) const { return to_tuple() == other.to_tuple(); }

    uint64_t pack() const {
        return (static_cast<uint64_t>(major) << 48) |
            (static_cast<uint64_t>(minor) << 32) |
            (static_cast<uint64_t>(patch) << 16) |
            (static_cast<uint64_t>(letter));
    }

    std::string to_hex_string() const {
        std::stringstream ss;
        ss << std::hex << std::setfill('0') << std::setw(16) << pack();
        return ss.str();
    }

    std::wstring to_hex_wstring() const {
        std::wstringstream wss;
        wss << std::hex << std::setfill(L'0') << std::setw(16) << pack();
        return wss.str();
    }

    void from_hex_string(const std::string& hex) {
        if (hex.empty()) return;
        uint64_t val = std::stoull(hex, nullptr, 16);
        major = static_cast<uint16_t>(val >> 48);
        minor = static_cast<uint16_t>(val >> 32);
        patch = static_cast<uint16_t>(val >> 16);
        letter = static_cast<uint16_t>(val & 0xFFFF);
    }

    void from_hex_wstring(const std::wstring& hex) {
        if (hex.empty()) return;
        uint64_t val = std::stoull(hex, nullptr, 16);
        major = static_cast<uint16_t>(val >> 48);
        minor = static_cast<uint16_t>(val >> 32);
        patch = static_cast<uint16_t>(val >> 16);
        letter = static_cast<uint16_t>(val & 0xFFFF);
    }
};

inline Version parseVersion(std::wstring_view v) {
    Version res;
    try {
        size_t dash = v.find(L'-');
        if (dash != std::wstring_view::npos) v.remove_prefix(dash + 1);
        size_t pos = 0;
        std::wstring ws(v);
        res.major = static_cast<uint16_t>(std::stoi(ws, &pos));
        ws = ws.substr(pos + 1);
        res.minor = static_cast<uint16_t>(std::stoi(ws, &pos));
        ws = ws.substr(pos + 1);
        res.patch = static_cast<uint16_t>(std::stoi(ws, &pos));
        if (pos < ws.size()) res.letter = static_cast<uint16_t>(ws[pos]);
        else res.letter = 0;
    }
    catch (...) {
    }
    return res;
}

extern const wchar_t regex_info_name[];
extern const wchar_t regex_tool_name[];
extern const wchar_t filter_name[];
extern const wchar_t filter_info[];
extern const wchar_t filter_name_media[];
extern const wchar_t tool_name[];
extern const wchar_t label[];
extern const wchar_t plugin_version[];
extern Version plugin_version_data;
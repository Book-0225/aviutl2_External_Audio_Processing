#pragma once
#include "Eap2Version.h"

#include <Windows.h>
#include <cstdint>
#include <iomanip>
#include <sstream>

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

namespace version_detail {
constexpr uint16_t ReadDigits(std::wstring_view s, size_t& pos, wchar_t stop_char) {
    uint16_t value = 0;
    bool any_digit = false;
    while (pos < s.size() && s[pos] >= L'0' && s[pos] <= L'9') {
        value = static_cast<uint16_t>(value * 10 + (s[pos] - L'0'));
        ++pos;
        any_digit = true;
    }
    if (!any_digit)
        throw std::invalid_argument("Version: expected digit");
    if (stop_char != L'\0') {
        if (pos >= s.size() || s[pos] != stop_char)
            throw std::invalid_argument("Version: expected separator");
        ++pos;
    }
    return value;
}
} // namespace version_detail

constexpr Version ParseVersionStrict(std::wstring_view v) {
    if (size_t dash = v.find(L'-'); dash != std::wstring_view::npos)
        v.remove_prefix(dash + 1);
    else if (!v.empty() && v.front() == L'v')
        v.remove_prefix(1);
    Version res{};
    size_t pos = 0;
    res.major = version_detail::ReadDigits(v, pos, L'.');
    res.minor = version_detail::ReadDigits(v, pos, L'.');
    res.patch = version_detail::ReadDigits(v, pos, L'\0');
    if (pos < v.size())
        res.letter = static_cast<uint16_t>(v[pos++]);
    if (pos != v.size())
        throw std::invalid_argument("Version: trailing characters");
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
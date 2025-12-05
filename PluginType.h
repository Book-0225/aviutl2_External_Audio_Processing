#pragma once

enum class PluginType {
    Unknown,
    VST3,
    CLAP
};

inline PluginType GetPluginTypeFromPath(const std::wstring& path) {
    if (path.empty()) return PluginType::Unknown;
    size_t pos = path.find_last_of(L'.');
    if (pos == std::wstring::npos) return PluginType::Unknown;
    std::wstring ext = path.substr(pos);
    if (_wcsicmp(ext.c_str(), L".vst3") == 0) return PluginType::VST3;
    if (_wcsicmp(ext.c_str(), L".clap") == 0) return PluginType::CLAP;
    return PluginType::Unknown;
}
#pragma once
#include "Eap2Info.h"
#include <cwctype>
#include <functional>
#include <limits>
#include <string>
#include <vector>

inline std::wstring TrimCopy(std::wstring s) {
    auto is_space = [](wchar_t ch) { return std::iswspace(ch) != 0; };
    while (!s.empty() && is_space(s.front())) s.erase(s.begin());
    while (!s.empty() && is_space(s.back())) s.pop_back();
    return s;
}

inline std::wstring ToLowerCopy(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}

inline bool TryParseBool(const std::wstring& raw, bool& out) {
    std::wstring s = ToLowerCopy(TrimCopy(raw));
    if (s == L"1" || s == L"true" || s == L"on" || s == L"yes") {
        out = true;
        return true;
    }
    if (s == L"0" || s == L"false" || s == L"off" || s == L"no") {
        out = false;
        return true;
    }
    return false;
}

inline bool TryParseInt32(const std::wstring& raw, int32_t& out, int32_t minValue, int32_t maxValue) {
    try {
        std::wstring s = TrimCopy(raw);
        size_t pos = 0;
        long value = std::stol(s, &pos, 10);
        if (pos != s.size()) return false;
        if (value < minValue || value > maxValue) return false;
        out = static_cast<int32_t>(value);
        return true;
    }
    catch (...) {
        return false;
    }
}

struct ConfigEntry {
    std::wstring key;
    std::wstring defaultValue;
    bool reload;
    std::function<bool(const std::wstring&)> load;
    std::function<std::wstring()> save;

    template<typename T>
    static ConfigEntry Create(std::wstring key, std::wstring def, T* target, bool canReload) {
        auto entry = ConfigEntry{
            key, def, canReload,
            [target](const std::wstring& s) {
                if constexpr (std::is_same_v<T, bool>) {
                    bool value = false;
                    if (!TryParseBool(s, value)) return false;
                    *target = value;
                    return true;
                }
                else if constexpr (std::is_same_v<T, int32_t>) {
                    int32_t value = 0;
                    if (!TryParseInt32(s, value, (std::numeric_limits<int32_t>::min)(), (std::numeric_limits<int32_t>::max)())) return false;
                    *target = value;
                    return true;
                }
                else if constexpr (std::is_same_v<T, std::wstring>) {
                    *target = s;
                    return true;
                }
                else if constexpr (std::is_same_v<T, Version>) {
                    try {
                        target->from_hex_wstring(s);
                        return true;
                    }
                    catch (...) {
                        return false;
                    }
                }
                return false;
            },
            [target]() {
                if constexpr (std::is_same_v<T, bool>) return *target ? L"1" : L"0";
                else if constexpr (std::is_same_v<T, int32_t>) return std::to_wstring(*target);
                else if constexpr (std::is_same_v<T, std::wstring>) return *target;
                else if constexpr (std::is_same_v<T, Version>) return target->to_hex_wstring();
            }
        };
        entry.load(def);
        return entry;
    }
};

struct ConfigInfo {
    std::wstring categoryName = L"Info";
    std::wstring version;
    std::wstring version_data;
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"Version", plugin_version, &version, false),
            ConfigEntry::Create(L"VersionData", parseVersion(plugin_version).to_hex_wstring(), &version_data, false)
        };
    }
};

struct GeneralConfig {
    std::wstring categoryName = L"General";
    bool auto_rename_disable;
    bool enable_experimental;
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"AutoRenameDisable", L"1", &auto_rename_disable, true),
            ConfigEntry::Create(L"EnableExperimental", L"0", &enable_experimental, false)
        };
    }
};

struct ModuleConfig {
    std::wstring categoryName = L"Module";
    bool all_tool_disable;
    bool host_disable;
    bool chain_tool_disable;
    bool host_filter_disable;
    bool host_media_disable;
    bool auto_wah_disable;
    bool chain_comp_disable;
    bool chain_dynamic_eq_disable;
    bool chain_filter_disable;
    bool chain_gate_disable;
    bool chain_send_disable;
    bool deesser_disable;
    bool distortion_disable;
    bool dynamics_disable;
    bool eq_disable;
    bool generator_disable;
    bool maximizer_disable;
    bool modulation_disable;
    bool notes_send_disable;
    bool phaser_disable;
    bool pitch_shift_disable;
    bool reverb_disable;
    bool spatial_disable;
    bool spectral_gate_disable;
    bool stereo_disable;
    bool utility_disable;
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"AllToolDisable", L"0", &all_tool_disable, false),
            ConfigEntry::Create(L"HostDisable", L"0", &host_disable, false),
            ConfigEntry::Create(L"ChainToolDisable", L"0", &chain_tool_disable, false),
            ConfigEntry::Create(L"HostFilterDisable", L"0", &host_filter_disable, false),
            ConfigEntry::Create(L"HostMediaDisable", L"0", &host_media_disable, false),
            ConfigEntry::Create(L"AutoWahDisable", L"0", &auto_wah_disable, false),
            ConfigEntry::Create(L"ChainCompDisable", L"0", &chain_comp_disable, false),
            ConfigEntry::Create(L"ChainDynamicEQDisable", L"0", &chain_dynamic_eq_disable, false),
            ConfigEntry::Create(L"ChainFilterDisable", L"0", &chain_filter_disable, false),
            ConfigEntry::Create(L"ChainGateDisable", L"0", &chain_gate_disable, false),
            ConfigEntry::Create(L"ChainSendDisable", L"0", &chain_send_disable, false),
            ConfigEntry::Create(L"DeEsserDisable", L"0", &deesser_disable, false),
            ConfigEntry::Create(L"DistortionDisable", L"0", &distortion_disable, false),
            ConfigEntry::Create(L"DynamicsDisable", L"0", &dynamics_disable, false),
            ConfigEntry::Create(L"EQDisable", L"0", &eq_disable, false),
            ConfigEntry::Create(L"GeneratorDisable", L"0", &generator_disable, false),
            ConfigEntry::Create(L"MaximizerDisable", L"0", &maximizer_disable, false),
            ConfigEntry::Create(L"ModulationDisable", L"0", &modulation_disable, false),
            ConfigEntry::Create(L"NotesSendDisable", L"0", &notes_send_disable, false),
            ConfigEntry::Create(L"PhaserDisable", L"0", &phaser_disable, false),
            ConfigEntry::Create(L"PitchShiftDisable", L"0", &pitch_shift_disable, false),
            ConfigEntry::Create(L"ReverbDisable", L"0", &reverb_disable, false),
            ConfigEntry::Create(L"SpatialDisable", L"0", &spatial_disable, false),
            ConfigEntry::Create(L"SpectralGateDisable", L"0", &spectral_gate_disable, false),
            ConfigEntry::Create(L"StereoDisable", L"0", &stereo_disable, false),
            ConfigEntry::Create(L"UtilityDisable", L"0", &utility_disable, false)
        };
    }
};

struct VstConfig {
    std::wstring categoryName = L"VST";
    bool forceResize;
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"ForceResize", L"0", &forceResize, true)
        };
    }
};

struct ExperimentalConfig{
    std::wstring categoryName = L"Experimental";
    bool use_experimental_generator;
    bool enable_experimental_midi_generator;
    bool use_experimental_reverb;
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"UseExperimentalGenerator", L"0", &use_experimental_generator, false),
            ConfigEntry::Create(L"EnableExperimentalMIDIGenerator", L"0", &enable_experimental_midi_generator, false),
            ConfigEntry::Create(L"UseExperimentalReverb", L"0", &use_experimental_reverb, false)
        };
    }
};

struct AppSettings {
    ConfigInfo info;
    GeneralConfig general;
    ModuleConfig module;
    VstConfig vst;
    ExperimentalConfig exp;
};

extern AppSettings settings;
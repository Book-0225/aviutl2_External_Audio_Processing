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
        int64_t value = std::stoll(s, &pos, 10);
        if (pos != s.size()) return false;
        if (!std::isfinite(value)) return false;
        if (value < minValue || value > maxValue) return false;
        out = static_cast<int32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

inline bool TryParseDouble(const std::wstring& raw, double& out, double minValue, double maxValue) {
    try {
        std::wstring s = TrimCopy(raw);
        size_t pos = 0;
        double value = std::stod(s, &pos);
        if (pos != s.size()) return false;
        if (!std::isfinite(value)) return false;
        if (value < minValue || value > maxValue) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

struct ConfigEntry {
    std::wstring key;
    std::wstring defaultValue;
    bool reload;
    std::function<bool(const std::wstring&)> load;
    std::function<std::wstring()> save;

    template <typename T>
    static ConfigEntry Create(std::wstring key, std::wstring def, T* target, bool canReload) {
        auto entry = ConfigEntry{
            key, def, canReload,
            [target](const std::wstring& s) {
                if constexpr (std::is_same_v<T, bool>) {
                    bool value = false;
                    if (!TryParseBool(s, value)) return false;
                    *target = value;
                    return true;
                } else if constexpr (std::is_same_v<T, int32_t>) {
                    int32_t value = 0;
                    if (!TryParseInt32(s, value, (std::numeric_limits<int32_t>::min)(), (std::numeric_limits<int32_t>::max)())) return false;
                    *target = value;
                    return true;
                } else if constexpr (std::is_same_v<T, double>) {
                    double value = 0.0;
                    if (!TryParseDouble(s, value, std::numeric_limits<double>::lowest(), (std::numeric_limits<double>::max)())) return false;
                    *target = value;
                    return true;
                } else if constexpr (std::is_same_v<T, std::wstring>) {
                    *target = s;
                    return true;
                } else if constexpr (std::is_same_v<T, Version>) {
                    try {
                        target->from_hex_wstring(s);
                        return true;
                    } catch (...) {
                        return false;
                    }
                }
                return false;
            },
            [target]() {
                if constexpr (std::is_same_v<T, bool>) return *target ? L"1" : L"0";
                else if constexpr (std::is_same_v<T, int32_t>) return std::to_wstring(*target);
                else if constexpr (std::is_same_v<T, double>) return std::to_wstring(*target);
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
    bool auto_rename_disable = false;
    bool enable_experimental = false;
    bool compress_plugin_state = true;
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"AutoRenameDisable", L"1", &auto_rename_disable, true),
            ConfigEntry::Create(L"EnableExperimental", L"0", &enable_experimental, false),
            ConfigEntry::Create(L"CompressPluginState", L"1", &compress_plugin_state, true)
        };
    }
};

struct ModuleConfig {
    std::wstring categoryName = L"Module";
    bool all_tool_disable = false;
    bool host_disable = false;
    bool chain_tool_disable = false;
    bool host_filter_disable = false;
    bool host_media_disable = false;
    bool auto_wah_disable = false;
    bool chain_comp_disable = false;
    bool chain_dynamic_eq_disable = false;
    bool chain_filter_disable = false;
    bool chain_gate_disable = false;
    bool chain_send_disable = false;
    bool deesser_disable = false;
    bool distortion_disable = false;
    bool dynamics_disable = false;
    bool eq_disable = false;
    bool generator_disable = false;
    bool maximizer_disable = false;
    bool modulation_disable = false;
    bool notes_send_disable = false;
    bool phaser_disable = false;
    bool pitch_shift_disable = false;
    bool reverb_disable = false;
    bool spatial_disable = false;
    bool spectral_gate_disable = false;
    bool stereo_disable = false;
    bool utility_disable = false;
    bool midi_gen_disable = false;
    bool analyzer_disable = false;
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
            ConfigEntry::Create(L"UtilityDisable", L"0", &utility_disable, false),
            ConfigEntry::Create(L"MIDIGeneratorDisable", L"0", &midi_gen_disable, false),
            ConfigEntry::Create(L"AnalyzerDisable", L"0", &analyzer_disable, false)
        };
    }
};

struct CompatConfig {
    std::wstring categoryName = L"Compat";
    bool use_new_reverb = false;
    bool use_new_generator = false;
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"UseNewReverb", L"0", &use_new_reverb, true),
            ConfigEntry::Create(L"UseNewGenerator", L"0", &use_new_generator, true)
        };
    }
};

struct VstConfig {
    std::wstring categoryName = L"VST";
    bool forceResize = false;
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"ForceResize", L"0", &forceResize, true)
        };
    }
};

struct AnalyzerConfig {
    std::wstring categoryName = L"Analyzer";
    double target_lufs = -14.0; // 目標 Integrated LUFS
    double target_peak = -1.0;  // 目標 True Peak [dBTP]
    double sil_db = -60.0;      // 無音判定しきい値 [dBFS]
    double sil_min_s = 0.5;     // 無音最短継続 [秒]
    double lufs_tol = 1.0;      // PASS 判定の許容幅 [LU] (±)
    double lufs_fail = 2.0;     // LUFS FAIL判定条件(target + lufs_fail < lufs)
    double lufs_warn = 8.0;     // LUFS WARN判定条件(target - lufs_warn >= lufs)
    double peak_fail = 1.0;     // TP FAIL判定条件(target + peak_fail < peak)
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"TargetLUFS", L"-14.0", &target_lufs, true),
            ConfigEntry::Create(L"TargetPeak", L"-1.0", &target_peak, true),
            ConfigEntry::Create(L"Silentdb", L"-60.0", &sil_db, true),
            ConfigEntry::Create(L"SilentMinSec", L"0.5", &sil_min_s, true),
            ConfigEntry::Create(L"LUFSTolerance", L"1.0", &lufs_tol, true),
            ConfigEntry::Create(L"LUFSFAIL", L"2.0", &lufs_fail, false),
            ConfigEntry::Create(L"LUFSWARN", L"8.0", &lufs_warn, false),
            ConfigEntry::Create(L"PEAKFAIL", L"1.0", &peak_fail, false)
        };
    }
};

struct ExperimentalConfig {
    std::wstring categoryName = L"Experimental";
    bool use_experimental_script_module = false;
    std::vector<ConfigEntry> getEntries() {
        return {
            ConfigEntry::Create(L"UseExperimentalScriptModule", L"0", &use_experimental_script_module, false)
        };
    }
};

struct AppSettings {
    ConfigInfo info;
    GeneralConfig general;
    ModuleConfig module;
    CompatConfig compat;
    VstConfig vst;
    AnalyzerConfig analyzer;
    ExperimentalConfig exp;
};

extern AppSettings settings;
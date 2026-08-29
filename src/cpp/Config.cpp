#include "Eap2Common.h"
#include "Eap2Config.h"
#include "MigrateConfig.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

AppSettings settings;
AppSettings new_settings;

struct ConfigLoadReport {
    bool has_error = false;
    std::vector<std::wstring> messages;

    void Add(const std::wstring& categoryName, const std::wstring& key, const std::wstring& rawValue) {
        has_error = true;
        messages.push_back(L"[" + categoryName + L"] " + key + L" = " + rawValue);
    }
};

std::wstring FormatVersionLabel(const Version& v) {
    std::wstring s = L"v" + std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) + L"." + std::to_wstring(v.patch);
    if (v.letter != 0)
        s += static_cast<wchar_t>(v.letter);
    return s;
}

void ShowBreakingChangeNotices() {
    std::vector<const BreakingChangeEntry*> pending;
    for (const auto& change : GetBreakingChanges())
        if (change.version > settings.info.acked_breaking_version)
            pending.push_back(&change);
    if (pending.empty())
        return;
    std::sort(pending.begin(), pending.end(), [](auto* a, auto* b) { return a->version < b->version; });
    std::wstring message = std::wstring(TrText(L"EAP2は以下のバージョンで破壊的変更が行われました:")) + L"\n\n";
    for (auto* change : pending)
        message += L"[" + FormatVersionLabel(change->version) + L"]\n" + change->message + L"\n\n";
    DbgMessage(message, LOG_WARN);
    Version current = parseVersion(plugin_version);
    Version newest_breaking = pending.back()->version;
    Version ack_to = current > newest_breaking ? current : newest_breaking;
    settings.info.acked_breaking_version = ack_to;
    new_settings.info.acked_breaking_version = ack_to;
    SaveConfig();
}

std::filesystem::path GetConfigPath() {
    return GetDllPath().replace_extension(L".ini");
}

void ShowConfigLoadWarning(const ConfigLoadReport& report) {
    if (!report.has_error)
        return;
    std::wstring message = std::wstring(TrText(L"設定ファイル内に無効な値が見つかったため、")) + L"\n" + TrText(L"該当する項目はデフォルト設定で読み込まれました。");
    DbgPrint(message, LOG_WARN);
    constexpr size_t max_lines = 10;
    for (size_t i = 0; i < report.messages.size() && i < max_lines; ++i)
        message += report.messages[i] + L"\n";
    if (report.messages.size() > max_lines)
        message += L"...";
    MessageBox(nullptr, message.c_str(), L"EAP2 Config Warning", MB_OK | MB_ICONWARNING);
}

void LoadEntryWithFallback(const std::wstring& categoryName, const ConfigEntry& item, const std::wstring& rawValue, ConfigLoadReport& report) {
    if (item.load(rawValue))
        return;
    DbgPrint(L"[Config] Invalid value detected. category=" + categoryName + L" key=" + item.key + L" value=" + rawValue + L" fallback=" + item.defaultValue, LOG_WARN);
    report.Add(categoryName, item.key, rawValue);
    if (!item.load(item.defaultValue))
        DbgPrint(L"[Config] Failed to apply default value. category=" + categoryName + L" key=" + item.key + L" default=" + item.defaultValue, LOG_WARN);
}

std::set<std::wstring> GetExistingKeys(const std::wstring& categoryName, const std::filesystem::path& path) {
    std::set<std::wstring> keys;
    DWORD buffer_size = 4096;

    for (;;) {
        std::vector<wchar_t> buffer(buffer_size, L'\0');
        DWORD copied = GetPrivateProfileSection(categoryName.c_str(), buffer.data(), buffer_size, path.c_str());
        if (copied == 0) return keys;
        if (copied < buffer_size - 2) {
            const wchar_t* current = buffer.data();
            while (*current != L'\0') {
                std::wstring_view entry(current);
                size_t eq = entry.find(L'=');
                if (eq != std::wstring_view::npos && eq > 0)
                    keys.insert(std::wstring(entry.substr(0, eq)));
                current += entry.size() + 1;
            }
            return keys;
        }
        buffer_size *= 2;
    }
}

void EnsureCategoryDefaults(const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
    std::set<std::wstring> existing_keys = GetExistingKeys(categoryName, path);
    for (const auto& item : entries) {
        if (existing_keys.find(item.key) != existing_keys.end()) continue;
        WritePrivateProfileString(categoryName.c_str(), item.key.c_str(), item.defaultValue.c_str(), path.c_str());
        DbgPrint(L"[Config] Added missing key. category=" + categoryName + L" key=" + item.key + L" default=" + item.defaultValue, LOG_INFO);
    }
}

template <typename Func>
void ApplyToAllCategories(Func func, AppSettings& setting, const std::filesystem::path& path) {
    auto categories = std::tie(setting.info, setting.general, setting.module, setting.compat, setting.vst, setting.analyzer, setting.exp);
    std::apply([&](auto&... cat) {
        (func(cat.categoryName, cat.getEntries(), path), ...);
    },
               categories);
}

void CreateConfig(const std::filesystem::path& path) {
    std::ofstream{ path };
    ApplyToAllCategories([](const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
        for (auto& item : entries)
            WritePrivateProfileString(categoryName.c_str(), item.key.c_str(), item.defaultValue.c_str(), path.c_str());
    },
                         settings, path);
}

void LoadCategory(const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path, ConfigLoadReport& report) {
    for (auto& item : entries) {
        wchar_t buffer[MAX_PATH];
        GetPrivateProfileString(categoryName.c_str(), item.key.c_str(), item.defaultValue.c_str(), buffer, MAX_PATH, path.c_str());
        LoadEntryWithFallback(categoryName, item, buffer, report);
    }
}

void LoadConfig() {
    std::filesystem::path path = GetConfigPath();
    bool is_new_install = !std::filesystem::exists(path);
    if (is_new_install)
        CreateConfig(path);

    if (!MigrateConfig(path)) {
        DbgMessage(std::wstring(TrText(L"設定ファイル移行エラー")) + L"\n" + TrText(L"デフォルト設定で読み込まれました。"), LOG_ERROR);
        settings = AppSettings{};
        new_settings = settings;
        return;
    }

    ApplyToAllCategories([](const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
        EnsureCategoryDefaults(categoryName, entries, path);
    },
                         settings, path);
    ConfigLoadReport report;
    ApplyToAllCategories([&report](const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
        LoadCategory(categoryName, entries, path, report);
    },
                         settings, path);
    if (parseVersion(settings.info.version) > parseVersion(plugin_version))
        DbgMessage(L"設定ファイルが現在のプラグインより新しいバージョンで作成されています", LOG_WARN);
    new_settings = settings;
    if (is_new_install) {
        settings.info.acked_breaking_version = parseVersion(plugin_version);
        new_settings.info.acked_breaking_version = settings.info.acked_breaking_version;
        SaveConfig();
    } else {
        if (!settings.general.disable_breaking_change_notices)
            ShowBreakingChangeNotices();
    }
    ShowConfigLoadWarning(report);
}

void ReloadCategory(const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path, ConfigLoadReport& report) {
    for (auto& item : entries) {
        if (item.reload) {
            wchar_t buffer[MAX_PATH];
            GetPrivateProfileString(categoryName.c_str(), item.key.c_str(), item.defaultValue.c_str(), buffer, MAX_PATH, path.c_str());
            LoadEntryWithFallback(categoryName, item, buffer, report);
        }
    }
}

void ReloadConfig() {
    std::filesystem::path path = GetConfigPath();
    if (!std::filesystem::exists(path)) {
        CreateConfig(path);
        return;
    }
    ConfigLoadReport report;
    ApplyToAllCategories([&report](const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
        ReloadCategory(categoryName, entries, path, report);
    },
                         settings, path);
    new_settings = settings;
    ShowConfigLoadWarning(report);
}

void SaveConfig() {
    std::filesystem::path path = GetConfigPath();
    if (!std::filesystem::exists(path))
        CreateConfig(path);
    ApplyToAllCategories([](const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
        for (auto& item : entries)
            WritePrivateProfileString(categoryName.c_str(), item.key.c_str(), item.save().c_str(), path.c_str());
    },
                         new_settings, path);
}

void ResetConfig() {
    std::filesystem::path path = GetConfigPath();
    ApplyToAllCategories([](const std::wstring&, std::vector<ConfigEntry> entries, auto) {
        for (auto& e : entries)
            e.load(e.defaultValue);
    },
                         new_settings, path);
    CreateConfig(path);
}

void OpenConfig() {
    std::filesystem::path path = GetConfigPath();
    if (!std::filesystem::exists(path))
        CreateConfig(path);
    ShellExecute(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
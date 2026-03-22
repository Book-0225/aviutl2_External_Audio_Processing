#include "Eap2Common.h"
#include "Eap2Config.h"
#include <filesystem>
#include <fstream>
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

std::filesystem::path GetConfigPath() {
    HMODULE hModule = NULL;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&GetConfigPath, &hModule);
    wchar_t path[MAX_PATH];
    GetModuleFileName(hModule, path, MAX_PATH);
    std::filesystem::path dllPath(path);
    return dllPath.replace_extension(L".ini");
}

void ShowConfigLoadWarning(const ConfigLoadReport& report) {
    if (!report.has_error) return;
    std::wstring message = TrText(L"設定ファイル内に無効な値が見つかったため、該当する項目はデフォルト設定で読み込まれました。");
    constexpr size_t max_lines = 10;
    for (size_t i = 0; i < report.messages.size() && i < max_lines; ++i) message += report.messages[i] + L"\n";
    if (report.messages.size() > max_lines) message += L"...";
    MessageBox(nullptr, message.c_str(), L"EAP2 Config Warning", MB_OK | MB_ICONWARNING);
}

void LoadEntryWithFallback(const std::wstring& categoryName, const ConfigEntry& item, const std::wstring& rawValue, ConfigLoadReport& report) {
    if (item.load(rawValue)) return;
    DbgPrint("[EAP2 Config] Invalid value detected. category=%ls key=%ls value=%ls fallback=%ls", categoryName.c_str(), item.key.c_str(), rawValue.c_str(), item.defaultValue.c_str());
    report.Add(categoryName, item.key, rawValue);
    if (!item.load(item.defaultValue)) DbgPrint("[EAP2 Config] Failed to apply default value. category=%ls key=%ls default=%ls", categoryName.c_str(), item.key.c_str(), item.defaultValue.c_str());
}

template<typename Func>
void ApplyToAllCategories(Func func, AppSettings& setting, const std::filesystem::path& path) {
    auto categories = std::tie(setting.info, setting.general, setting.module, setting.vst, setting.exp);
    std::apply([&](auto&... cat) {
        (func(cat.categoryName, cat.getEntries(), path), ...);
        }, categories);
}

void CreateConfig(const std::filesystem::path& path) {
    std::ofstream{path};
    ApplyToAllCategories([](const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
        for (auto& item : entries) WritePrivateProfileString(categoryName.c_str(), item.key.c_str(), item.defaultValue.c_str(), path.c_str());
        }, settings, path);
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
    if (!std::filesystem::exists(path)) CreateConfig(path);
    ConfigLoadReport report;
    ApplyToAllCategories([&report](const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
        LoadCategory(categoryName, entries, path, report);
        }, settings, path);
    new_settings = settings;
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
        }, settings, path);
    new_settings = settings;
    ShowConfigLoadWarning(report);
}

void SaveConfig() {
    std::filesystem::path path = GetConfigPath();
    if (!std::filesystem::exists(path)) CreateConfig(path);
    ApplyToAllCategories([](const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
        for (auto& item : entries) WritePrivateProfileString(categoryName.c_str(), item.key.c_str(), item.save().c_str(), path.c_str());
        }, new_settings, path);
}

void ResetConfig() {
    std::filesystem::path path = GetConfigPath();
    ApplyToAllCategories([](const std::wstring&, std::vector<ConfigEntry> entries, auto) {
        for (auto& e : entries) e.load(e.defaultValue);
        }, new_settings, path);
    CreateConfig(path);
}

void OpenConfig() {
    std::filesystem::path path = GetConfigPath();
    if (!std::filesystem::exists(path)) CreateConfig(path);
    ShellExecute(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
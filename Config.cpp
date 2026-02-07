#include "Eap2Common.h"
#include <filesystem>
#include <fstream>
#include <string>

AppSettings settings;
AppSettings new_settings;

std::filesystem::path GetConfigPath() {
    HMODULE hModule = NULL;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&GetConfigPath, &hModule);
    wchar_t path[MAX_PATH];
    GetModuleFileName(hModule, path, MAX_PATH);
    std::filesystem::path dllPath(path);
    return dllPath.replace_extension(L".ini");
}

template<typename Func>
void ApplyToAllCategories(Func func, AppSettings& setting, const std::filesystem::path& path) {
    auto categories = std::tie(setting.info, setting.module, setting.vst);
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

void LoadCategory(const std::wstring& categoryName, const std::vector<ConfigEntry>& entries, const std::filesystem::path& path) {
    for (auto& item : entries) {
        wchar_t buffer[MAX_PATH];
        GetPrivateProfileString(categoryName.c_str(), item.key.c_str(), item.defaultValue.c_str(), buffer, MAX_PATH, path.c_str());
        item.load(buffer);
    }
}

void LoadConfig() {
    std::filesystem::path path = GetConfigPath();
    if (!std::filesystem::exists(path)) CreateConfig(path);
    ApplyToAllCategories(LoadCategory, settings, path);
    new_settings = settings;
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
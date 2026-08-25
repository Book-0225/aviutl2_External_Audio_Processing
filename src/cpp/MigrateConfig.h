#pragma once
#include "Eap2Common.h"
#include "Eap2Config.h"
#include "Migrate0To1.h"

int32_t GetConfigVersion(const std::filesystem::path& path) {
    wchar_t buf[32];
    GetPrivateProfileString(L"Info", L"ConfigVersion", L"0", buf, _countof(buf), path.c_str());

    int32_t version;
    if (!TryParseInt32(buf, version, 0, INT32_MAX))
        return -1;

    return version;
}

inline bool MigrateConfig(const std::filesystem::path& path) {
    int32_t version = GetConfigVersion(path);

    if (version == _wtoi(CONFIG_VERSION))
        return true;

    if (version < 0 || version > _wtoi(CONFIG_VERSION)) {
        DbgMessage(std::wstring(TrText(L"設定ファイルのバージョン異常")) + L": " + std::to_wstring(version) + L"\n" + L"デフォルト設定で読み込まれました。", LOG_ERROR);
        return false;
    }

    while (version < _wtoi(CONFIG_VERSION)) {
        switch (version) {
            case 0:
                if (!Migrate0To1(path))
                    return false;
                ++version;
                break;

            default:
                return false;
        }
    }

    WritePrivateProfileString(L"Info", L"ConfigVersion", CONFIG_VERSION, path.c_str());
    return true;
}
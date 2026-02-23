#pragma once
#include <string>
#include <vector>
#include <charconv>
#include <windows.h>

namespace StringUtils {

    inline std::string WideToUtf8(LPCWSTR w) {
        if (!w || !w[0]) return "";
        int32_t size_needed = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (size_needed <= 0) return "";

        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &result[0], size_needed, nullptr, nullptr);

        if (!result.empty() && result.back() == '\0') {
            result.pop_back();
        }
        return result;
    }

    inline std::wstring Utf8ToWide(const std::string& s) {
        if (s.empty()) return L"";
        int32_t size_needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (size_needed <= 0) return L"";

        std::wstring result(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &result[0], size_needed);

        if (!result.empty() && result.back() == L'\0') {
            result.pop_back();
        }
        return result;
    }

    inline std::string GenerateUUID() {
        UUID u;
        static std::mutex uuid_gen_mutex;
        std::lock_guard<std::mutex> lock(uuid_gen_mutex);
        if (UuidCreate(&u) != RPC_S_OK) return "";
        RPC_CSTR s = nullptr;
        if (UuidToStringA(&u, &s) != RPC_S_OK) return "";

        std::string r((char*)s);
        RpcStringFreeA(&s);
        return r;
    }

    inline std::string HexToString(const std::string& hex) {
        if (hex.empty()) return {};
        std::string res;
        res.reserve(hex.size() / 2);

        for (size_t i = 0; i + 1 < hex.size(); i += 2) {
            uint8_t byte;
            auto result = std::from_chars(hex.data() + i, hex.data() + i + 2, byte, 16);
            if (result.ec != std::errc{}) {
                continue;
            }
            res.push_back(static_cast<char>(byte));
        }
        while (!res.empty() && res.back() == '\0') {
            res.pop_back();
        }
        return res;
    }

    inline std::string Base64Encode(const BYTE* data, DWORD len) {
        if (!data || len == 0) return "";
        DWORD b64_len = 0;
        DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
        if (!CryptBinaryToStringA(data, len, flags, nullptr, &b64_len)) return "";
        std::string s(b64_len, '\0');
        if (!CryptBinaryToStringA(data, len, flags, &s[0], &b64_len)) return "";

        while (!s.empty() && (s.back() == '\0' || s.back() == '\r' || s.back() == '\n')) {
            s.pop_back();
        }
        return s;
    }

    inline std::vector<BYTE> Base64Decode(const std::string& b64) {
        if (b64.empty()) return {};

        DWORD bin_len = 0;
        DWORD flags = CRYPT_STRING_BASE64_ANY;

        if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), flags, nullptr, &bin_len, nullptr, nullptr)) {
            return {};
        }

        std::vector<BYTE> v(bin_len);
        if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), flags, v.data(), &bin_len, nullptr, nullptr)) {
            return {};
        }

        v.resize(bin_len);

        return v;
    }
}
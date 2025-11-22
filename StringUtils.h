#pragma once

#include <string>
#include <windows.h>
#include <rpc.h>

namespace StringUtils {

    inline std::string WideToUtf8(LPCWSTR w) {
        if (!w || !w[0]) return "";
        int s = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
        if (s == 0) return "";
        std::string r(s, 0);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &r[0], s, 0, 0);
        r.pop_back();
        return r;
    }

    inline std::string GenerateUUID() {
        UUID u;
        UuidCreate(&u);
        RPC_CSTR s;
        UuidToStringA(&u, &s);
        std::string r((char*)s);
        RpcStringFreeA(&s);
        return r;
    }

    inline std::string HexToString(const std::string& hex) {
        std::string res;
        if (hex.empty()) return res;
        res.reserve(hex.length() / 2);
        for (size_t i = 0; i < hex.length(); i += 2) {
            if (i + 1 >= hex.length()) break;
            char byteString[3] = { hex[i], hex[i + 1], '\0' };
            char byte = static_cast<char>(strtol(byteString, nullptr, 16));
            res.push_back(byte);
        }
        return res;
    }

    static std::string Base64Encode(const BYTE* data, DWORD len) {
        if (!data || len == 0) return "";
        DWORD b64_len = 0;
        DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
        if (!CryptBinaryToStringA(data, len, flags, nullptr, &b64_len)) return "";
        std::string s(b64_len, '\0');
        if (!CryptBinaryToStringA(data, len, flags, &s[0], &b64_len)) return "";
        size_t nullPos = s.find('\0');
        if (nullPos != std::string::npos) {
            s.resize(nullPos);
        }
        else {
            s.resize(b64_len);
        }
        while (!s.empty() && (s.back() == '\0' || s.back() == '\r' || s.back() == '\n')) {
            s.pop_back();
        }
        return s;
    }

    static std::vector<BYTE> Base64Decode(const std::string& b64) {
        if (b64.empty()) return {};
        std::string safe_b64 = b64;
        while (safe_b64.size() % 4 != 0) {
            safe_b64 += '=';
        }
        DWORD bin_len = 0;
        DWORD flags = CRYPT_STRING_BASE64_ANY;
        if (!CryptStringToBinaryA(safe_b64.c_str(), (DWORD)safe_b64.size(), flags, nullptr, &bin_len, nullptr, nullptr)) {
            return {};
        }
        std::vector<BYTE> v(bin_len);
        if (!CryptStringToBinaryA(safe_b64.c_str(), (DWORD)safe_b64.size(), flags, v.data(), &bin_len, nullptr, nullptr)) {
            return {};
        }
        return v;
    }

}
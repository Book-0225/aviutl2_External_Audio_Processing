#pragma once
#include <charconv>
#include <compressapi.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

namespace StringUtils {

inline constexpr char kCompressedBlobMagic[8] = { 'E', 'A', 'P', '2', 'C', 'M', 'P', '1' };

struct CompressedBlobHeader {
    char magic[sizeof(kCompressedBlobMagic)];
    uint64_t originalSize;
};

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

    std::string r(reinterpret_cast<char*>(s));
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

    if (!CryptStringToBinaryA(b64.c_str(), static_cast<DWORD>(b64.size()), flags, nullptr, &bin_len, nullptr, nullptr)) {
        return {};
    }

    std::vector<BYTE> v(bin_len);
    if (!CryptStringToBinaryA(b64.c_str(), static_cast<DWORD>(b64.size()), flags, v.data(), &bin_len, nullptr, nullptr)) {
        return {};
    }

    v.resize(bin_len);

    return v;
}

inline bool CompressBlob(const BYTE* data, size_t len, std::vector<BYTE>& out) {
    out.clear();
    if (!data || len == 0) return false;
    if (len > (std::numeric_limits<size_t>::max)()) return false;

    COMPRESSOR_HANDLE compressor = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor)) return false;

    size_t compressedSize = 0;
    BOOL ok = Compress(compressor, data, static_cast<size_t>(len), nullptr, 0, &compressedSize);
    if (!ok && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseCompressor(compressor);
        return false;
    }

    out.resize(sizeof(CompressedBlobHeader) + compressedSize);
    CompressedBlobHeader header{};
    std::memcpy(header.magic, kCompressedBlobMagic, sizeof(kCompressedBlobMagic));
    header.originalSize = static_cast<uint64_t>(len);
    std::memcpy(out.data(), &header, sizeof(header));

    size_t writtenSize = compressedSize;
    ok = Compress(compressor, data, static_cast<size_t>(len), out.data() + sizeof(CompressedBlobHeader), compressedSize, &writtenSize);
    CloseCompressor(compressor);
    if (!ok) {
        out.clear();
        return false;
    }

    out.resize(sizeof(CompressedBlobHeader) + writtenSize);
    return true;
}

inline bool DecompressBlob(const BYTE* data, size_t len, std::vector<BYTE>& out, size_t maxOutputSize = (std::numeric_limits<size_t>::max)()) {
    out.clear();
    if (!data || len < sizeof(CompressedBlobHeader)) return false;

    CompressedBlobHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (std::memcmp(header.magic, kCompressedBlobMagic, sizeof(kCompressedBlobMagic)) != 0) return false;
    if (header.originalSize == 0 || header.originalSize > maxOutputSize || header.originalSize > (std::numeric_limits<size_t>::max)()) return false;

    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &decompressor)) return false;

    out.resize(static_cast<size_t>(header.originalSize));
    size_t writtenSize = static_cast<size_t>(header.originalSize);
    BOOL ok = Decompress(
        decompressor,
        data + sizeof(CompressedBlobHeader),
        static_cast<size_t>(len - sizeof(CompressedBlobHeader)),
        out.data(),
        static_cast<size_t>(out.size()),
        &writtenSize);
    CloseDecompressor(decompressor);
    if (!ok || writtenSize != out.size()) {
        out.clear();
        return false;
    }

    return true;
}

inline std::string EncodeCompressedStatePayload(std::string_view rawPrefix, std::string_view compressedPrefix, const BYTE* data, size_t len, bool enableCompression = true) {
    if (!data || len == 0) return "";

    std::vector<BYTE> compressed;
    if (enableCompression && CompressBlob(data, len, compressed) && compressed.size() < len) {
        return std::string(compressedPrefix) + Base64Encode(compressed.data(), static_cast<DWORD>(compressed.size()));
    }

    return std::string(rawPrefix) + Base64Encode(data, static_cast<DWORD>(len));
}

inline bool DecodeStatePayload(const std::string& encodedState, std::string_view rawPrefix, std::string_view compressedPrefix, std::vector<BYTE>& decodedPayload, size_t maxDecodedSize = (std::numeric_limits<size_t>::max)()) {
    decodedPayload.clear();
    if (encodedState.rfind(rawPrefix.data(), 0) == 0) {
        decodedPayload = Base64Decode(encodedState.substr(rawPrefix.size()));
        return !decodedPayload.empty();
    }

    if (encodedState.rfind(compressedPrefix.data(), 0) == 0) {
        std::vector<BYTE> compressedPayload = Base64Decode(encodedState.substr(compressedPrefix.size()));
        if (compressedPayload.empty()) return false;
        return DecompressBlob(compressedPayload.data(), compressedPayload.size(), decodedPayload, maxDecodedSize);
    }

    return false;
}
} // namespace StringUtils
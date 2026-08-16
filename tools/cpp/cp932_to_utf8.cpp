#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {

std::vector<char> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("failed to open for read: " + path.string());
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteFile(const std::filesystem::path& path, const std::vector<char>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("failed to open for write: " + path.string());
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

bool HasUtf8Bom(const std::vector<char>& data) {
    return data.size() >= 3 &&
           static_cast<unsigned char>(data[0]) == 0xEF &&
           static_cast<unsigned char>(data[1]) == 0xBB &&
           static_cast<unsigned char>(data[2]) == 0xBF;
}

std::vector<char> ConvertCp932ToUtf8Bom(const std::vector<char>& src) {
    if (src.empty()) {
        return { static_cast<char>(0xEF), static_cast<char>(0xBB), static_cast<char>(0xBF) };
    }

    int32_t wlen = MultiByteToWideChar(932, 0, src.data(), static_cast<int32_t>(src.size()), nullptr, 0);
    if (wlen <= 0) {
        throw std::runtime_error("MultiByteToWideChar failed");
    }
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(932, 0, src.data(), static_cast<int32_t>(src.size()), wide.data(), wlen);

    int32_t ulen = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0)
        throw std::runtime_error("WideCharToMultiByte failed");

    std::vector<char> out;
    out.reserve(static_cast<size_t>(ulen) + 3);
    out.push_back(static_cast<char>(0xEF));
    out.push_back(static_cast<char>(0xBB));
    out.push_back(static_cast<char>(0xBF));
    out.resize(3 + static_cast<size_t>(ulen));
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, out.data() + 3, ulen, nullptr, nullptr);
    return out;
}

} // namespace

int32_t main(int32_t argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <directory>\n", argv[0]);
        return 1;
    }

    std::filesystem::path root(argv[1]);
    if (!std::filesystem::exists(root)) {
        std::fprintf(stderr, "directory not found: %s\n", root.string().c_str());
        return 1;
    }

    int converted = 0;
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".h") continue;

            std::vector<char> src = ReadFile(entry.path());
            if (HasUtf8Bom(src))
                continue;

            std::vector<char> dst = ConvertCp932ToUtf8Bom(src);
            WriteFile(entry.path(), dst);
            std::printf("  %s\n", entry.path().string().c_str());
            ++converted;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::printf("Converted %d file(s).\n", converted);
    return 0;
}

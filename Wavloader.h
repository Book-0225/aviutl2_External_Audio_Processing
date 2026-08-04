#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace WavLoader {

struct WavData {
    bool valid = false;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    std::vector<float> left;
    std::vector<float> right;
    std::string error;
};

struct WavDataMulti {
    bool valid = false;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    std::vector<std::vector<float>> ch;
    std::string error;
};

namespace detail {

inline float PcmToFloat(int32_t sample, int32_t bits) {
    switch (bits) {
        case 8:
            return (static_cast<float>(sample) - 128.0f) / 128.0f;
        case 16:
            return static_cast<float>(sample) / 32768.0f;
        case 24:
            return static_cast<float>(sample) / 8388608.0f;
        case 32:
            return static_cast<float>(sample) / 2147483648.0f;
        default:
            return 0.0f;
    }
}

inline int32_t ReadIntLE(const uint8_t* p, int32_t bytes, bool is_signed) {
    uint32_t v = 0;
    for (int32_t i = 0; i < bytes; ++i) v |= (static_cast<uint32_t>(p[i]) << (8 * i));
    if (is_signed && bytes < 4) {
        uint32_t sign_bit = 1u << (bytes * 8 - 1);
        if (v & sign_bit) v |= (~0u << (bytes * 8));
    }
    return static_cast<int32_t>(v);
}

struct ParsedWav {
    bool valid = false;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    std::vector<std::vector<float>> ch;
    std::string error;
};

inline ParsedWav ParseAllChannels(const std::wstring& path) {
    ParsedWav out;

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) {
        out.error = "ファイルを開けませんでした";
        return out;
    }

    std::vector<uint8_t> buf;
    {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz <= 44) {
            fclose(fp);
            out.error = "ファイルが小さすぎます";
            return out;
        }
        buf.resize(static_cast<size_t>(sz));
        size_t read = fread(buf.data(), 1, buf.size(), fp);
        fclose(fp);
        if (read != buf.size()) {
            out.error = "読み込みに失敗しました";
            return out;
        }
    }

    if (buf.size() < 12 || std::memcmp(buf.data(), "RIFF", 4) != 0 ||
        std::memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        out.error = "RIFF/WAVEヘッダが不正です";
        return out;
    }

    int32_t fmt_channels = 0;
    int32_t fmt_sample_rate = 0;
    int32_t fmt_bits = 0;
    int32_t fmt_format_tag = 0;
    bool fmt_found = false;

    const uint8_t* data_ptr = nullptr;
    size_t data_size = 0;

    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        char chunk_id[5] = { 0 };
        std::memcpy(chunk_id, buf.data() + pos, 4);
        uint32_t chunk_size = static_cast<uint32_t>(ReadIntLE(buf.data() + pos + 4, 4, false));
        size_t chunk_data_pos = pos + 8;

        if (chunk_data_pos + chunk_size > buf.size())
            chunk_size = static_cast<uint32_t>(buf.size() - chunk_data_pos);

        if (std::memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            const uint8_t* f = buf.data() + chunk_data_pos;
            fmt_format_tag = ReadIntLE(f + 0, 2, false);
            fmt_channels = ReadIntLE(f + 2, 2, false);
            fmt_sample_rate = ReadIntLE(f + 4, 4, false);
            fmt_bits = ReadIntLE(f + 14, 2, false);
            if (fmt_format_tag == 0xFFFE && chunk_size >= 40)
                fmt_format_tag = ReadIntLE(f + 24, 2, false);
            fmt_found = true;
        } else if (std::memcmp(chunk_id, "data", 4) == 0) {
            data_ptr = buf.data() + chunk_data_pos;
            data_size = chunk_size;
        }

        pos = chunk_data_pos + chunk_size;
        if (chunk_size % 2 != 0) pos += 1;
    }

    if (!fmt_found || !data_ptr || data_size == 0) {
        out.error = "fmt/dataチャンクが見つかりません";
        return out;
    }
    if (fmt_channels <= 0) {
        out.error = "チャンネル数が不正です";
        return out;
    }
    if (fmt_format_tag != 1 && fmt_format_tag != 3) {
        out.error = "非対応のフォーマットです(PCM/IEEE Float以外のフォーマットです。ADPCM等の圧縮WAVは非対応です)";
        return out;
    }
    bool bits_ok = (fmt_format_tag == 1 && (fmt_bits == 8 || fmt_bits == 16 || fmt_bits == 24 || fmt_bits == 32)) || (fmt_format_tag == 3 && (fmt_bits == 32 || fmt_bits == 64));
    if (!bits_ok) {
        out.error = "非対応のビット深度です";
        return out;
    }

    const int32_t bytes_per_sample = fmt_bits / 8;
    const int32_t block_align = bytes_per_sample * fmt_channels;
    if (block_align <= 0) {
        out.error = "ブロックサイズが不正です";
        return out;
    }
    const size_t total_frames = data_size / static_cast<size_t>(block_align);
    if (total_frames == 0) {
        out.error = "サンプルデータが空です";
        return out;
    }

    out.ch.assign(static_cast<size_t>(fmt_channels), std::vector<float>(total_frames));

    for (size_t i = 0; i < total_frames; ++i) {
        const uint8_t* frame = data_ptr + i * block_align;
        for (int32_t c = 0; c < fmt_channels; ++c) {
            const uint8_t* s = frame + c * bytes_per_sample;
            float v;
            if (fmt_format_tag == 3 && fmt_bits == 32) {
                float f;
                std::memcpy(&f, s, 4);
                v = f;
            } else if (fmt_format_tag == 3 && fmt_bits == 64) {
                double d;
                std::memcpy(&d, s, 8);
                v = static_cast<float>(d);
            } else {
                int32_t raw = ReadIntLE(s, bytes_per_sample, fmt_bits != 8);
                v = PcmToFloat(raw, fmt_bits);
            }
            out.ch[static_cast<size_t>(c)][i] = v;
        }
    }

    out.valid = true;
    out.sample_rate = fmt_sample_rate;
    out.channels = fmt_channels;
    return out;
}

} // namespace detail

inline WavData Load(const std::wstring& path) {
    WavData out;
    detail::ParsedWav p = detail::ParseAllChannels(path);
    if (!p.valid) {
        out.error = p.error;
        return out;
    }
    out.valid = true;
    out.sample_rate = p.sample_rate;
    out.channels = p.channels;
    out.left = p.ch[0];
    out.right = (p.channels >= 2) ? p.ch[1] : p.ch[0];
    return out;
}

inline WavDataMulti LoadMulti(const std::wstring& path) {
    WavDataMulti out;
    detail::ParsedWav p = detail::ParseAllChannels(path);
    if (!p.valid) {
        out.error = p.error;
        return out;
    }
    out.valid = true;
    out.sample_rate = p.sample_rate;
    out.channels = p.channels;
    out.ch = std::move(p.ch);
    return out;
}

inline std::vector<float> ResampleLinear(const std::vector<float>& in, int32_t src_rate, int32_t dst_rate) {
    if (src_rate <= 0 || dst_rate <= 0 || src_rate == dst_rate || in.empty()) return in;
    const double ratio = static_cast<double>(src_rate) / static_cast<double>(dst_rate);
    const size_t out_len = static_cast<size_t>(static_cast<double>(in.size()) / ratio);
    std::vector<float> out(out_len);
    for (size_t i = 0; i < out_len; ++i) {
        double src_pos = static_cast<double>(i) * ratio;
        size_t idx = static_cast<size_t>(src_pos);
        double frac = src_pos - static_cast<double>(idx);
        float a = in[idx];
        float b = (idx + 1 < in.size()) ? in[idx + 1] : in[idx];
        out[i] = static_cast<float>(a + (b - a) * frac);
    }
    return out;
}

} // namespace WavLoader
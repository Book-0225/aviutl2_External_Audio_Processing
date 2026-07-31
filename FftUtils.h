#pragma once
#include <cmath>
#include <complex>
#include <cstdint>

namespace FftUtils {

inline void FftInplace(std::complex<float>* data, int32_t n, bool inverse) {
    for (int32_t i = 1, j = 0; i < n; ++i) {
        int32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (int32_t len = 2; len <= n; len <<= 1) {
        const float ang = static_cast<float>((inverse ? M_PI * 2 : M_PI * -2) / len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int32_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int32_t j = 0; j < (len >> 1); ++j) {
                auto u = data[i + j];
                auto v = data[i + j + (len >> 1)] * w;
                data[i + j] = u + v;
                data[i + j + (len >> 1)] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        const float inv_n = 1.0f / static_cast<float>(n);
        for (int32_t i = 0; i < n; ++i) data[i] *= inv_n;
    }
}

inline int32_t NextPow2(int32_t n) {
    int32_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

} // namespace FftUtils
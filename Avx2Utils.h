#pragma once
#include <immintrin.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace Avx2Utils {
    inline void CopyBufferAVX2(float* dst, const float* src, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        for (; i < aligned_count; i += 32) {
            __m256 r0 = _mm256_loadu_ps(src + i);
            __m256 r1 = _mm256_loadu_ps(src + i + 8);
            __m256 r2 = _mm256_loadu_ps(src + i + 16);
            __m256 r3 = _mm256_loadu_ps(src + i + 24);
            _mm256_storeu_ps(dst + i, r0);
            _mm256_storeu_ps(dst + i + 8, r1);
            _mm256_storeu_ps(dst + i + 16, r2);
            _mm256_storeu_ps(dst + i + 24, r3);
        }
        for (; i < (count - (count % 8)); i += 8) {
            _mm256_storeu_ps(dst + i, _mm256_loadu_ps(src + i));
        }
        for (; i < count; ++i) dst[i] = src[i];
        _mm256_zeroupper();
    }

    inline void FillBufferAVX2(float* out, size_t count, float value) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_val = _mm256_set1_ps(value);
        for (; i < aligned_count; i += 32) {
            _mm256_storeu_ps(out + i, v_val);
            _mm256_storeu_ps(out + i + 8, v_val);
            _mm256_storeu_ps(out + i + 16, v_val);
            _mm256_storeu_ps(out + i + 24, v_val);
        }
        for (; i < (count - (count % 8)); i += 8) {
            _mm256_storeu_ps(out + i, v_val);
        }
        for (; i < count; ++i) out[i] = value;
        _mm256_zeroupper();
    }

    inline void ScaleBufferAVX2(float* out, const float* in, size_t count, float scale) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_scale = _mm256_set1_ps(scale);
        for (; i < aligned_count; i += 32) {
            _mm256_storeu_ps(out + i, _mm256_mul_ps(_mm256_loadu_ps(in + i), v_scale));
            _mm256_storeu_ps(out + i + 8, _mm256_mul_ps(_mm256_loadu_ps(in + i + 8), v_scale));
            _mm256_storeu_ps(out + i + 16, _mm256_mul_ps(_mm256_loadu_ps(in + i + 16), v_scale));
            _mm256_storeu_ps(out + i + 24, _mm256_mul_ps(_mm256_loadu_ps(in + i + 24), v_scale));
        }
        for (; i < (count - (count % 8)); i += 8) {
            _mm256_storeu_ps(out + i, _mm256_mul_ps(_mm256_loadu_ps(in + i), v_scale));
        }
        for (; i < count; ++i) out[i] = in[i] * scale;
        _mm256_zeroupper();
    }

    inline void AccumulateAVX2(float* dst, const float* src, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        for (; i < aligned_count; i += 32) {
            _mm256_storeu_ps(dst + i, _mm256_add_ps(_mm256_loadu_ps(dst + i), _mm256_loadu_ps(src + i)));
            _mm256_storeu_ps(dst + i + 8, _mm256_add_ps(_mm256_loadu_ps(dst + i + 8), _mm256_loadu_ps(src + i + 8)));
            _mm256_storeu_ps(dst + i + 16, _mm256_add_ps(_mm256_loadu_ps(dst + i + 16), _mm256_loadu_ps(src + i + 16)));
            _mm256_storeu_ps(dst + i + 24, _mm256_add_ps(_mm256_loadu_ps(dst + i + 24), _mm256_loadu_ps(src + i + 24)));
        }
        for (; i < (count - (count % 8)); i += 8) {
            _mm256_storeu_ps(dst + i, _mm256_add_ps(_mm256_loadu_ps(dst + i), _mm256_loadu_ps(src + i)));
        }
        for (; i < count; ++i) dst[i] += src[i];
        _mm256_zeroupper();
    }

    inline void AccumulateScaledAVX2(float* dst, const float* src, size_t count, float scale) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_scale = _mm256_set1_ps(scale);
        for (; i < aligned_count; i += 32) {
            _mm256_storeu_ps(dst + i, _mm256_fmadd_ps(_mm256_loadu_ps(src + i), v_scale, _mm256_loadu_ps(dst + i)));
            _mm256_storeu_ps(dst + i + 8, _mm256_fmadd_ps(_mm256_loadu_ps(src + i + 8), v_scale, _mm256_loadu_ps(dst + i + 8)));
            _mm256_storeu_ps(dst + i + 16, _mm256_fmadd_ps(_mm256_loadu_ps(src + i + 16), v_scale, _mm256_loadu_ps(dst + i + 16)));
            _mm256_storeu_ps(dst + i + 24, _mm256_fmadd_ps(_mm256_loadu_ps(src + i + 24), v_scale, _mm256_loadu_ps(dst + i + 24)));
        }
        for (; i < (count - (count % 8)); i += 8) {
            _mm256_storeu_ps(dst + i, _mm256_fmadd_ps(_mm256_loadu_ps(src + i), v_scale, _mm256_loadu_ps(dst + i)));
        }
        for (; i < count; ++i) dst[i] += src[i] * scale;
        _mm256_zeroupper();
    }

    inline void MultiplyBufferAVX2(float* dst, const float* src, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        for (; i < aligned_count; i += 32) {
            _mm256_storeu_ps(dst + i, _mm256_mul_ps(_mm256_loadu_ps(dst + i), _mm256_loadu_ps(src + i)));
            _mm256_storeu_ps(dst + i + 8, _mm256_mul_ps(_mm256_loadu_ps(dst + i + 8), _mm256_loadu_ps(src + i + 8)));
            _mm256_storeu_ps(dst + i + 16, _mm256_mul_ps(_mm256_loadu_ps(dst + i + 16), _mm256_loadu_ps(src + i + 16)));
            _mm256_storeu_ps(dst + i + 24, _mm256_mul_ps(_mm256_loadu_ps(dst + i + 24), _mm256_loadu_ps(src + i + 24)));
        }
        for (; i < (count - (count % 8)); i += 8) {
            _mm256_storeu_ps(dst + i, _mm256_mul_ps(_mm256_loadu_ps(dst + i), _mm256_loadu_ps(src + i)));
        }
        for (; i < count; ++i) dst[i] *= src[i];
        _mm256_zeroupper();
    }

    inline void MultiplyBufferAVX2(float* out, const float* src1, const float* src2, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        for (; i < aligned_count; i += 32) {
            _mm256_storeu_ps(out + i, _mm256_mul_ps(_mm256_loadu_ps(src1 + i), _mm256_loadu_ps(src2 + i)));
            _mm256_storeu_ps(out + i + 8, _mm256_mul_ps(_mm256_loadu_ps(src1 + i + 8), _mm256_loadu_ps(src2 + i + 8)));
            _mm256_storeu_ps(out + i + 16, _mm256_mul_ps(_mm256_loadu_ps(src1 + i + 16), _mm256_loadu_ps(src2 + i + 16)));
            _mm256_storeu_ps(out + i + 24, _mm256_mul_ps(_mm256_loadu_ps(src1 + i + 24), _mm256_loadu_ps(src2 + i + 24)));
        }
        for (; i < (count - (count % 8)); i += 8) {
            _mm256_storeu_ps(out + i, _mm256_mul_ps(_mm256_loadu_ps(src1 + i), _mm256_loadu_ps(src2 + i)));
        }
        for (; i < count; ++i) out[i] = src1[i] * src2[i];
        _mm256_zeroupper();
    }

    inline void MixAudioAVX2(float* out, const float* in, size_t count, float wet, float dry, float vol) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_wet = _mm256_set1_ps(wet);
        __m256 v_dry = _mm256_set1_ps(dry);
        __m256 v_vol = _mm256_set1_ps(vol);

        for (; i < aligned_count; i += 32) {
            __m256 mix0 = _mm256_fmadd_ps(_mm256_loadu_ps(out + i), v_wet, _mm256_mul_ps(_mm256_loadu_ps(in + i), v_dry));
            __m256 mix1 = _mm256_fmadd_ps(_mm256_loadu_ps(out + i + 8), v_wet, _mm256_mul_ps(_mm256_loadu_ps(in + i + 8), v_dry));
            __m256 mix2 = _mm256_fmadd_ps(_mm256_loadu_ps(out + i + 16), v_wet, _mm256_mul_ps(_mm256_loadu_ps(in + i + 16), v_dry));
            __m256 mix3 = _mm256_fmadd_ps(_mm256_loadu_ps(out + i + 24), v_wet, _mm256_mul_ps(_mm256_loadu_ps(in + i + 24), v_dry));

            _mm256_storeu_ps(out + i, _mm256_mul_ps(mix0, v_vol));
            _mm256_storeu_ps(out + i + 8, _mm256_mul_ps(mix1, v_vol));
            _mm256_storeu_ps(out + i + 16, _mm256_mul_ps(mix2, v_vol));
            _mm256_storeu_ps(out + i + 24, _mm256_mul_ps(mix3, v_vol));
        }
        for (; i < (count - (count % 8)); i += 8) {
            __m256 v_mix = _mm256_fmadd_ps(_mm256_loadu_ps(out + i), v_wet, _mm256_mul_ps(_mm256_loadu_ps(in + i), v_dry));
            _mm256_storeu_ps(out + i, _mm256_mul_ps(v_mix, v_vol));
        }
        for (; i < count; ++i) out[i] = (out[i] * wet + in[i] * dry) * vol;
        _mm256_zeroupper();
    }

    inline void HardClipAVX2(float* buf, size_t count, float min_val, float max_val) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_min = _mm256_set1_ps(min_val);
        __m256 v_max = _mm256_set1_ps(max_val);
        for (; i < aligned_count; i += 32) {
            __m256 v0 = _mm256_loadu_ps(buf + i);
            __m256 v1 = _mm256_loadu_ps(buf + i + 8);
            __m256 v2 = _mm256_loadu_ps(buf + i + 16);
            __m256 v3 = _mm256_loadu_ps(buf + i + 24);

            v0 = _mm256_min_ps(_mm256_max_ps(v0, v_min), v_max);
            v1 = _mm256_min_ps(_mm256_max_ps(v1, v_min), v_max);
            v2 = _mm256_min_ps(_mm256_max_ps(v2, v_min), v_max);
            v3 = _mm256_min_ps(_mm256_max_ps(v3, v_min), v_max);

            _mm256_storeu_ps(buf + i, v0);
            _mm256_storeu_ps(buf + i + 8, v1);
            _mm256_storeu_ps(buf + i + 16, v2);
            _mm256_storeu_ps(buf + i + 24, v3);
        }
        for (; i < (count - (count % 8)); i += 8) {
            __m256 v = _mm256_loadu_ps(buf + i);
            _mm256_storeu_ps(buf + i, _mm256_min_ps(_mm256_max_ps(v, v_min), v_max));
        }
        for (; i < count; ++i) {
            if (buf[i] < min_val) buf[i] = min_val;
            else if (buf[i] > max_val) buf[i] = max_val;
        }
        _mm256_zeroupper();
    }

    inline void SwapChannelsAVX2(float* bufL, float* bufR, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        for (; i < aligned_count; i += 32) {
            __m256 l0 = _mm256_loadu_ps(bufL + i), r0 = _mm256_loadu_ps(bufR + i);
            _mm256_storeu_ps(bufL + i, r0); _mm256_storeu_ps(bufR + i, l0);

            __m256 l1 = _mm256_loadu_ps(bufL + i + 8), r1 = _mm256_loadu_ps(bufR + i + 8);
            _mm256_storeu_ps(bufL + i + 8, r1); _mm256_storeu_ps(bufR + i + 8, l1);

            __m256 l2 = _mm256_loadu_ps(bufL + i + 16), r2 = _mm256_loadu_ps(bufR + i + 16);
            _mm256_storeu_ps(bufL + i + 16, r2); _mm256_storeu_ps(bufR + i + 16, l2);

            __m256 l3 = _mm256_loadu_ps(bufL + i + 24), r3 = _mm256_loadu_ps(bufR + i + 24);
            _mm256_storeu_ps(bufL + i + 24, r3); _mm256_storeu_ps(bufR + i + 24, l3);
        }
        for (; i < (count - (count % 8)); i += 8) {
            __m256 v_l = _mm256_loadu_ps(bufL + i);
            __m256 v_r = _mm256_loadu_ps(bufR + i);
            _mm256_storeu_ps(bufL + i, v_r);
            _mm256_storeu_ps(bufR + i, v_l);
        }
        for (; i < count; ++i) std::swap(bufL[i], bufR[i]);
        _mm256_zeroupper();
    }

    inline void InvertBufferAVX2(float* buf, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_neg = _mm256_set1_ps(-1.0f);
        for (; i < aligned_count; i += 32) {
            _mm256_storeu_ps(buf + i, _mm256_mul_ps(_mm256_loadu_ps(buf + i), v_neg));
            _mm256_storeu_ps(buf + i + 8, _mm256_mul_ps(_mm256_loadu_ps(buf + i + 8), v_neg));
            _mm256_storeu_ps(buf + i + 16, _mm256_mul_ps(_mm256_loadu_ps(buf + i + 16), v_neg));
            _mm256_storeu_ps(buf + i + 24, _mm256_mul_ps(_mm256_loadu_ps(buf + i + 24), v_neg));
        }
        for (; i < (count - (count % 8)); i += 8) {
            _mm256_storeu_ps(buf + i, _mm256_mul_ps(_mm256_loadu_ps(buf + i), v_neg));
        }
        for (; i < count; ++i) buf[i] = -buf[i];
        _mm256_zeroupper();
    }

    inline void MatrixMixStereoAVX2(float* outL, float* outR, const float* inL, const float* inR, size_t count, float cLL, float cRL, float cLR, float cRR)
    {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_cLL = _mm256_set1_ps(cLL), v_cRL = _mm256_set1_ps(cRL);
        __m256 v_cLR = _mm256_set1_ps(cLR), v_cRR = _mm256_set1_ps(cRR);

        for (; i < aligned_count; i += 32) {
            auto mix_chunk = [&](int offset) {
                __m256 l = _mm256_loadu_ps(inL + i + offset);
                __m256 r = _mm256_loadu_ps(inR + i + offset);
                _mm256_storeu_ps(outL + i + offset, _mm256_fmadd_ps(l, v_cLL, _mm256_mul_ps(r, v_cRL)));
                _mm256_storeu_ps(outR + i + offset, _mm256_fmadd_ps(l, v_cLR, _mm256_mul_ps(r, v_cRR)));
                };
            mix_chunk(0); mix_chunk(8); mix_chunk(16); mix_chunk(24);
        }
        for (; i < (count - (count % 8)); i += 8) {
            __m256 l = _mm256_loadu_ps(inL + i);
            __m256 r = _mm256_loadu_ps(inR + i);
            _mm256_storeu_ps(outL + i, _mm256_fmadd_ps(l, v_cLL, _mm256_mul_ps(r, v_cRL)));
            _mm256_storeu_ps(outR + i, _mm256_fmadd_ps(l, v_cLR, _mm256_mul_ps(r, v_cRR)));
        }
        for (; i < count; ++i) {
            float l = inL[i], r = inR[i];
            outL[i] = l * cLL + r * cRL;
            outR[i] = l * cLR + r * cRR;
        }
        _mm256_zeroupper();
    }

    inline void ReadRingBufferAVX2(float* dst, const std::vector<float>& buf, int32_t buf_size, int32_t read_pos, int32_t count) {
        if (read_pos < 0) read_pos += buf_size;

        int32_t first_chunk = count;
        if (read_pos + count > buf_size) first_chunk = buf_size - read_pos;
        CopyBufferAVX2(dst, buf.data() + read_pos, first_chunk);
        if (first_chunk < count) CopyBufferAVX2(dst + first_chunk, buf.data(), count - first_chunk);
    }

    inline void WriteRingBufferAVX2(std::vector<float>& buf, const float* src, int32_t buf_size, int32_t write_pos, int32_t count) {
        int32_t first_chunk = count;
        if (write_pos + count > buf_size) first_chunk = buf_size - write_pos;
        CopyBufferAVX2(buf.data() + write_pos, src, first_chunk);
        if (first_chunk < count) CopyBufferAVX2(buf.data(), src + first_chunk, count - first_chunk);
    }

    inline void SoftClipTanhAVX2(float* buf, size_t count, float drive_gain) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_gain = _mm256_set1_ps(drive_gain);

        __m256 v_3 = _mm256_set1_ps(3.0f);
        __m256 v_neg3 = _mm256_set1_ps(-3.0f);
        __m256 v_1 = _mm256_set1_ps(1.0f);
        __m256 v_neg0 = _mm256_set1_ps(-0.0f);

        for (; i < aligned_count; i += 32) {
            auto process_chunk = [&](int offset) {
                __m256 x = _mm256_loadu_ps(buf + i + offset);
                x = _mm256_mul_ps(x, v_gain);
                x = _mm256_max_ps(x, v_neg3);
                x = _mm256_min_ps(x, v_3);

                __m256 abs_x = _mm256_andnot_ps(v_neg0, x);
                __m256 denom = _mm256_add_ps(v_1, abs_x);
                __m256 res = _mm256_div_ps(x, denom);

                _mm256_storeu_ps(buf + i + offset, res);
                };
            process_chunk(0); process_chunk(8); process_chunk(16); process_chunk(24);
        }

        for (; i < (count - (count % 8)); i += 8) {
            __m256 x = _mm256_loadu_ps(buf + i);
            x = _mm256_mul_ps(x, v_gain);
            x = _mm256_max_ps(x, v_neg3);
            x = _mm256_min_ps(x, v_3);

            __m256 abs_x = _mm256_andnot_ps(v_neg0, x);
            __m256 denom = _mm256_add_ps(v_1, abs_x);
            _mm256_storeu_ps(buf + i, _mm256_div_ps(x, denom));
        }

        for (; i < count; ++i) {
            float x = buf[i] * drive_gain;
            if (x < -3.0f) x = -3.0f;
            else if (x > 3.0f) x = 3.0f;
            buf[i] = x / (1.0f + std::abs(x));
        }
        _mm256_zeroupper();
    }

    inline void FuzzShapeAVX2(float* buf, size_t count, float drive) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_drive_scale = _mm256_set1_ps(1.0f + drive * 10.0f);
        __m256 v_max = _mm256_set1_ps(0.8f);
        __m256 v_min = _mm256_set1_ps(-0.8f);

        for (; i < aligned_count; i += 32) {
            auto process_chunk = [&](int offset) {
                __m256 x = _mm256_loadu_ps(buf + i + offset);
                x = _mm256_mul_ps(x, v_drive_scale);
                x = _mm256_min_ps(x, v_max);
                x = _mm256_max_ps(x, v_min);
                _mm256_storeu_ps(buf + i + offset, x);
                };
            process_chunk(0); process_chunk(8); process_chunk(16); process_chunk(24);
        }
        for (; i < (count - (count % 8)); i += 8) {
            __m256 x = _mm256_loadu_ps(buf + i);
            x = _mm256_mul_ps(x, v_drive_scale);
            x = _mm256_min_ps(x, v_max);
            x = _mm256_max_ps(x, v_min);
            _mm256_storeu_ps(buf + i, x);
        }
        for (; i < count; ++i) {
            float val = buf[i] * (1.0f + drive * 10.0f);
            if (val > 0.8f) val = 0.8f;
            else if (val < -0.8f) val = -0.8f;
            buf[i] = val;
        }
        _mm256_zeroupper();
    }

    inline void QuantizeAVX2(float* buf, size_t count, float step_size) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_step = _mm256_set1_ps(step_size);
        __m256 v_inv_step = _mm256_set1_ps(1.0f / step_size);

        for (; i < aligned_count; i += 32) {
            auto process_chunk = [&](int offset) {
                __m256 x = _mm256_loadu_ps(buf + i + offset);
                x = _mm256_mul_ps(x, v_inv_step);
                x = _mm256_floor_ps(x);
                x = _mm256_mul_ps(x, v_step);
                _mm256_storeu_ps(buf + i + offset, x);
                };
            process_chunk(0); process_chunk(8); process_chunk(16); process_chunk(24);
        }
        for (; i < (count - (count % 8)); i += 8) {
            __m256 x = _mm256_loadu_ps(buf + i);
            x = _mm256_mul_ps(x, v_inv_step);
            x = _mm256_floor_ps(x);
            x = _mm256_mul_ps(x, v_step);
            _mm256_storeu_ps(buf + i, x);
        }
        for (; i < count; ++i) {
            buf[i] = std::floor(buf[i] / step_size) * step_size;
        }
        _mm256_zeroupper();
    }

    inline float GetPeakAbsAVX2(const float* src, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_max0 = _mm256_setzero_ps();
        __m256 v_max1 = _mm256_setzero_ps();
        __m256 v_max2 = _mm256_setzero_ps();
        __m256 v_max3 = _mm256_setzero_ps();
        __m256 v_sign_mask = _mm256_set1_ps(-0.0f);

        for (; i < aligned_count; i += 32) {
            v_max0 = _mm256_max_ps(v_max0, _mm256_andnot_ps(v_sign_mask, _mm256_loadu_ps(src + i)));
            v_max1 = _mm256_max_ps(v_max1, _mm256_andnot_ps(v_sign_mask, _mm256_loadu_ps(src + i + 8)));
            v_max2 = _mm256_max_ps(v_max2, _mm256_andnot_ps(v_sign_mask, _mm256_loadu_ps(src + i + 16)));
            v_max3 = _mm256_max_ps(v_max3, _mm256_andnot_ps(v_sign_mask, _mm256_loadu_ps(src + i + 24)));
        }

        __m256 v_max = _mm256_max_ps(_mm256_max_ps(v_max0, v_max1), _mm256_max_ps(v_max2, v_max3));

        for (; i < (count - (count % 8)); i += 8) {
            __m256 v_val = _mm256_loadu_ps(src + i);
            v_val = _mm256_andnot_ps(v_sign_mask, v_val);
            v_max = _mm256_max_ps(v_max, v_val);
        }

        alignas(32) float temp[8];
        _mm256_store_ps(temp, v_max);
        float max_val = 0.0f;
        for (int32_t k = 0; k < 8; ++k) if (temp[k] > max_val) max_val = temp[k];

        for (; i < count; ++i) {
            max_val = (std::max)(max_val, std::abs(src[i]));
        }
        _mm256_zeroupper();
        return max_val;
    }
}
#pragma once
#include <immintrin.h>
#include <vector>
#include <cmath>

namespace Avx2Utils {
    inline void CopyBufferAVX2(float* dst, const float* src, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        for (; i < aligned_count; i += 8) {
            _mm256_storeu_ps(dst + i, _mm256_loadu_ps(src + i));
        }
        for (; i < count; ++i) dst[i] = src[i];
    }

    inline void FillBufferAVX2(float* out, size_t count, float value) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_val = _mm256_set1_ps(value);
        for (; i < aligned_count; i += 8) {
            _mm256_storeu_ps(out + i, v_val);
        }
        for (; i < count; ++i) out[i] = value;
    }

    inline void ScaleBufferAVX2(float* out, const float* in, size_t count, float scale) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_scale = _mm256_set1_ps(scale);
        for (; i < aligned_count; i += 8) {
            _mm256_storeu_ps(out + i, _mm256_mul_ps(_mm256_loadu_ps(in + i), v_scale));
        }
        for (; i < count; ++i) out[i] = in[i] * scale;
    }

    inline void AccumulateAVX2(float* dst, const float* src, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        for (; i < aligned_count; i += 8) {
            __m256 v_dst = _mm256_loadu_ps(dst + i);
            __m256 v_src = _mm256_loadu_ps(src + i);
            _mm256_storeu_ps(dst + i, _mm256_add_ps(v_dst, v_src));
        }
        for (; i < count; ++i) dst[i] += src[i];
    }

    inline void AccumulateScaledAVX2(float* dst, const float* src, size_t count, float scale) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_scale = _mm256_set1_ps(scale);
        for (; i < aligned_count; i += 8) {
            __m256 v_dst = _mm256_loadu_ps(dst + i);
            __m256 v_src = _mm256_loadu_ps(src + i);
            __m256 v_scaled = _mm256_mul_ps(v_src, v_scale);
            _mm256_storeu_ps(dst + i, _mm256_add_ps(v_dst, v_scaled));
        }
        for (; i < count; ++i) dst[i] += src[i] * scale;
    }

    inline void MultiplyBufferAVX2(float* dst, const float* src, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        for (; i < aligned_count; i += 8) {
            __m256 v_dst = _mm256_loadu_ps(dst + i);
            __m256 v_src = _mm256_loadu_ps(src + i);
            _mm256_storeu_ps(dst + i, _mm256_mul_ps(v_dst, v_src));
        }
        for (; i < count; ++i) dst[i] *= src[i];
    }

    inline void MultiplyBufferAVX2(float* out, const float* src1, const float* src2, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        for (; i < aligned_count; i += 8) {
            __m256 v1 = _mm256_loadu_ps(src1 + i);
            __m256 v2 = _mm256_loadu_ps(src2 + i);
            _mm256_storeu_ps(out + i, _mm256_mul_ps(v1, v2));
        }
        for (; i < count; ++i) out[i] = src1[i] * src2[i];
    }

    inline void MixAudioAVX2(float* out, const float* in, size_t count, float wet, float dry, float vol) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_wet = _mm256_set1_ps(wet);
        __m256 v_dry = _mm256_set1_ps(dry);
        __m256 v_vol = _mm256_set1_ps(vol);
        for (; i < aligned_count; i += 8) {
            __m256 v_out = _mm256_loadu_ps(out + i);
            __m256 v_in = _mm256_loadu_ps(in + i);
            __m256 v_mix = _mm256_add_ps(_mm256_mul_ps(v_out, v_wet), _mm256_mul_ps(v_in, v_dry));
            _mm256_storeu_ps(out + i, _mm256_mul_ps(v_mix, v_vol));
        }
        for (; i < count; ++i) out[i] = (out[i] * wet + in[i] * dry) * vol;
    }

    inline void HardClipAVX2(float* buf, size_t count, float min_val, float max_val) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_min = _mm256_set1_ps(min_val);
        __m256 v_max = _mm256_set1_ps(max_val);
        for (; i < aligned_count; i += 8) {
            __m256 v = _mm256_loadu_ps(buf + i);
            v = _mm256_max_ps(v, v_min);
            v = _mm256_min_ps(v, v_max);
            _mm256_storeu_ps(buf + i, v);
        }
        for (; i < count; ++i) {
            if (buf[i] < min_val) buf[i] = min_val;
            else if (buf[i] > max_val) buf[i] = max_val;
        }
    }

    inline void SwapChannelsAVX2(float* bufL, float* bufR, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        for (; i < aligned_count; i += 8) {
            __m256 v_l = _mm256_loadu_ps(bufL + i);
            __m256 v_r = _mm256_loadu_ps(bufR + i);
            _mm256_storeu_ps(bufL + i, v_r);
            _mm256_storeu_ps(bufR + i, v_l);
        }
        for (; i < count; ++i) std::swap(bufL[i], bufR[i]);
    }

    inline void InvertBufferAVX2(float* buf, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_neg = _mm256_set1_ps(-1.0f);
        for (; i < aligned_count; i += 8) {
            _mm256_storeu_ps(buf + i, _mm256_mul_ps(_mm256_loadu_ps(buf + i), v_neg));
        }
        for (; i < count; ++i) buf[i] = -buf[i];
    }

    inline void MatrixMixStereoAVX2(
        float* outL, float* outR,
        const float* inL, const float* inR,
        size_t count,
        float cLL, float cRL, float cLR, float cRR)
    {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_cLL = _mm256_set1_ps(cLL), v_cRL = _mm256_set1_ps(cRL);
        __m256 v_cLR = _mm256_set1_ps(cLR), v_cRR = _mm256_set1_ps(cRR);

        for (; i < aligned_count; i += 8) {
            __m256 v_l = _mm256_loadu_ps(inL + i);
            __m256 v_r = _mm256_loadu_ps(inR + i);

            __m256 resL = _mm256_add_ps(_mm256_mul_ps(v_l, v_cLL), _mm256_mul_ps(v_r, v_cRL));
            __m256 resR = _mm256_add_ps(_mm256_mul_ps(v_l, v_cLR), _mm256_mul_ps(v_r, v_cRR));

            _mm256_storeu_ps(outL + i, resL);
            _mm256_storeu_ps(outR + i, resR);
        }
        for (; i < count; ++i) {
            float l = inL[i], r = inR[i];
            outL[i] = l * cLL + r * cRL;
            outR[i] = l * cLR + r * cRR;
        }
    }

    inline void ReadRingBufferAVX2(float* dst, const std::vector<float>& buf, int32_t buf_size, int32_t read_pos, int32_t count) {
        if (read_pos < 0) read_pos += buf_size;

        int32_t first_chunk = count;
        if (read_pos + count > buf_size) {
            first_chunk = buf_size - read_pos;
        }

        CopyBufferAVX2(dst, buf.data() + read_pos, first_chunk);

        if (first_chunk < count) {
            CopyBufferAVX2(dst + first_chunk, buf.data(), count - first_chunk);
        }
    }

    inline void WriteRingBufferAVX2(std::vector<float>& buf, const float* src, int32_t buf_size, int32_t write_pos, int32_t count) {
        int32_t first_chunk = count;
        if (write_pos + count > buf_size) {
            first_chunk = buf_size - write_pos;
        }

        CopyBufferAVX2(buf.data() + write_pos, src, first_chunk);

        if (first_chunk < count) {
            CopyBufferAVX2(buf.data(), src + first_chunk, count - first_chunk);
        }
    }
    inline void SoftClipTanhAVX2(float* buf, size_t count, float drive_gain) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_gain = _mm256_set1_ps(drive_gain);

        __m256 v_3 = _mm256_set1_ps(3.0f);
        __m256 v_neg3 = _mm256_set1_ps(-3.0f);
        __m256 v_1 = _mm256_set1_ps(1.0f);
        __m256 v_neg1 = _mm256_set1_ps(-1.0f);

        for (; i < aligned_count; i += 8) {
            __m256 x = _mm256_loadu_ps(buf + i);
            x = _mm256_mul_ps(x, v_gain);
            x = _mm256_max_ps(x, v_neg3);
            x = _mm256_min_ps(x, v_3);

            __m256 x2 = _mm256_mul_ps(x, x);
            __m256 x3 = _mm256_mul_ps(x2, x);
            __m256 term = _mm256_mul_ps(x3, _mm256_set1_ps(1.0f / 27.0f));
            __m256 res = _mm256_sub_ps(x, term);
            __m256 abs_x = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), x);
            __m256 denom = _mm256_add_ps(_mm256_set1_ps(1.0f), abs_x);
            res = _mm256_div_ps(x, denom);

            _mm256_storeu_ps(buf + i, res);
        }

        for (; i < count; ++i) {
            float x = buf[i] * drive_gain;
            if (x < -3.0f) buf[i] = -1.0f;
            else if (x > 3.0f) buf[i] = 1.0f;
            else buf[i] = std::tanh(x);
        }
    }

    inline void FuzzShapeAVX2(float* buf, size_t count, float drive) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_drive_scale = _mm256_set1_ps(1.0f + drive * 10.0f);
        __m256 v_max = _mm256_set1_ps(0.8f);
        __m256 v_min = _mm256_set1_ps(-0.8f);

        for (; i < aligned_count; i += 8) {
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
    }

    inline void QuantizeAVX2(float* buf, size_t count, float step_size) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_step = _mm256_set1_ps(step_size);
        __m256 v_inv_step = _mm256_set1_ps(1.0f / step_size);

        for (; i < aligned_count; i += 8) {
            __m256 x = _mm256_loadu_ps(buf + i);
            x = _mm256_mul_ps(x, v_inv_step);
            x = _mm256_floor_ps(x);
            x = _mm256_mul_ps(x, v_step);
            _mm256_storeu_ps(buf + i, x);
        }
        for (; i < count; ++i) {
            buf[i] = std::floor(buf[i] / step_size) * step_size;
        }
    }
    inline float GetPeakAbsAVX2(const float* src, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);

        __m256 v_max = _mm256_setzero_ps();
        __m256 v_sign_mask = _mm256_set1_ps(-0.0f);

        for (; i < aligned_count; i += 8) {
            __m256 v_val = _mm256_loadu_ps(src + i);
            v_val = _mm256_andnot_ps(v_sign_mask, v_val);
            v_max = _mm256_max_ps(v_max, v_val);
        }

        alignas(32) float temp[8];
        _mm256_store_ps(temp, v_max);
        float max_val = 0.0f;
        for (int32_t k = 0; k < 8; ++k) {
            if (temp[k] > max_val) max_val = temp[k];
        }

        for (; i < count; ++i) {
            float abs_val = std::abs(src[i]);
            if (abs_val > max_val) max_val = abs_val;
        }

        return max_val;
    }
}
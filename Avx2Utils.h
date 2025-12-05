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
        for (; i < (count - (count % 8)); i += 8) _mm256_storeu_ps(dst + i, _mm256_loadu_ps(src + i));
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
        for (; i < (count - (count % 8)); i += 8) _mm256_storeu_ps(out + i, v_val);
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
        for (; i < (count - (count % 8)); i += 8) _mm256_storeu_ps(out + i, _mm256_mul_ps(_mm256_loadu_ps(in + i), v_scale));
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
        for (; i < (count - (count % 8)); i += 8) _mm256_storeu_ps(dst + i, _mm256_add_ps(_mm256_loadu_ps(dst + i), _mm256_loadu_ps(src + i)));
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
        for (; i < (count - (count % 8)); i += 8) _mm256_storeu_ps(dst + i, _mm256_fmadd_ps(_mm256_loadu_ps(src + i), v_scale, _mm256_loadu_ps(dst + i)));
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
        for (; i < (count - (count % 8)); i += 8) _mm256_storeu_ps(dst + i, _mm256_mul_ps(_mm256_loadu_ps(dst + i), _mm256_loadu_ps(src + i)));
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
        for (; i < (count - (count % 8)); i += 8) _mm256_storeu_ps(out + i, _mm256_mul_ps(_mm256_loadu_ps(src1 + i), _mm256_loadu_ps(src2 + i)));
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
        for (; i < (count - (count % 8)); i += 8) _mm256_storeu_ps(buf + i, _mm256_mul_ps(_mm256_loadu_ps(buf + i), v_neg));
        for (; i < count; ++i) buf[i] = -buf[i];
        _mm256_zeroupper();
    }

    inline void MatrixMixStereoAVX2(float* outL, float* outR, const float* inL, const float* inR, size_t count, float cLL, float cRL, float cLR, float cRR) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_cLL = _mm256_set1_ps(cLL), v_cRL = _mm256_set1_ps(cRL);
        __m256 v_cLR = _mm256_set1_ps(cLR), v_cRR = _mm256_set1_ps(cRR);

        for (; i < aligned_count; i += 32) {
            auto mix_chunk = [&](int32_t offset) {
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
            auto process_chunk = [&](int32_t offset) {
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
            auto process_chunk = [&](int32_t offset) {
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
            auto process_chunk = [&](int32_t offset) {
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
        for (; i < count; ++i) buf[i] = std::floor(buf[i] / step_size) * step_size;
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

        for (; i < count; ++i) max_val = (std::max)(max_val, std::abs(src[i]));
        _mm256_zeroupper();
        return max_val;
    }

    inline void EnvelopeFollowerAVX2(float* envelope, const float* input, size_t count, float attack_coeff, float release_coeff) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_attack = _mm256_set1_ps(attack_coeff);
        __m256 v_release = _mm256_set1_ps(release_coeff);
        __m256 v_one = _mm256_set1_ps(1.0f);
        __m256 v_one_m_attack = _mm256_set1_ps(1.0f - attack_coeff);
        __m256 v_one_m_release = _mm256_set1_ps(1.0f - release_coeff);

        for (; i < aligned_count; i += 8) {
            __m256 v_env = _mm256_loadu_ps(envelope + i);
            __m256 v_in = _mm256_loadu_ps(input + i);
            __m256 v_cmp = _mm256_cmp_ps(v_in, v_env, _CMP_GT_OS);
            __m256 v_attack_result = _mm256_fmadd_ps(v_env, v_attack, _mm256_mul_ps(v_in, v_one_m_attack));
            __m256 v_release_result = _mm256_fmadd_ps(v_env, v_release, _mm256_mul_ps(v_in, v_one_m_release));
            __m256 v_result = _mm256_blendv_ps(v_release_result, v_attack_result, v_cmp);
            _mm256_storeu_ps(envelope + i, v_result);
        }
        
        for (; i < count; ++i) {
            if (input[i] > envelope[i]) envelope[i] = attack_coeff * envelope[i] + (1.0f - attack_coeff) * input[i];
            else envelope[i] = release_coeff * envelope[i] + (1.0f - release_coeff) * input[i];
        }
        _mm256_zeroupper();
    }

    inline void AbsAVX2(float* out, const float* in, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_mask = _mm256_set1_ps(-0.0f);

        for (; i < aligned_count; i += 32) {
            __m256 v0 = _mm256_loadu_ps(in + i);
            __m256 v1 = _mm256_loadu_ps(in + i + 8);
            __m256 v2 = _mm256_loadu_ps(in + i + 16);
            __m256 v3 = _mm256_loadu_ps(in + i + 24);
            _mm256_storeu_ps(out + i, _mm256_andnot_ps(v_mask, v0));
            _mm256_storeu_ps(out + i + 8, _mm256_andnot_ps(v_mask, v1));
            _mm256_storeu_ps(out + i + 16, _mm256_andnot_ps(v_mask, v2));
            _mm256_storeu_ps(out + i + 24, _mm256_andnot_ps(v_mask, v3));
        }

        for (; i < (count - (count % 8)); i += 8) {
            __m256 v = _mm256_loadu_ps(in + i);
            _mm256_storeu_ps(out + i, _mm256_andnot_ps(v_mask, v));
        }

        for (; i < count; ++i) out[i] = std::abs(in[i]);
        _mm256_zeroupper();
    }

    inline void MaxBufferAVX2(float* out, const float* src1, const float* src2, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);

        for (; i < aligned_count; i += 32) {
            __m256 v0_a = _mm256_loadu_ps(src1 + i), v0_b = _mm256_loadu_ps(src2 + i);
            __m256 v1_a = _mm256_loadu_ps(src1 + i + 8), v1_b = _mm256_loadu_ps(src2 + i + 8);
            __m256 v2_a = _mm256_loadu_ps(src1 + i + 16), v2_b = _mm256_loadu_ps(src2 + i + 16);
            __m256 v3_a = _mm256_loadu_ps(src1 + i + 24), v3_b = _mm256_loadu_ps(src2 + i + 24);
            _mm256_storeu_ps(out + i, _mm256_max_ps(v0_a, v0_b));
            _mm256_storeu_ps(out + i + 8, _mm256_max_ps(v1_a, v1_b));
            _mm256_storeu_ps(out + i + 16, _mm256_max_ps(v2_a, v2_b));
            _mm256_storeu_ps(out + i + 24, _mm256_max_ps(v3_a, v3_b));
        }

        for (; i < (count - (count % 8)); i += 8) {
            __m256 va = _mm256_loadu_ps(src1 + i);
            __m256 vb = _mm256_loadu_ps(src2 + i);
            _mm256_storeu_ps(out + i, _mm256_max_ps(va, vb));
        }

        for (; i < count; ++i) out[i] = (std::max)(src1[i], src2[i]);
        _mm256_zeroupper();
    }

    inline void ThresholdAVX2(float* out, const float* in, size_t count, float threshold) {
        size_t i = 0;
        size_t aligned_count = count - (count % 32);
        __m256 v_threshold = _mm256_set1_ps(threshold);
        __m256 v_one = _mm256_set1_ps(1.0f);
        __m256 v_zero = _mm256_set1_ps(0.0f);

        for (; i < aligned_count; i += 32) {
            __m256 v0 = _mm256_loadu_ps(in + i);
            __m256 v1 = _mm256_loadu_ps(in + i + 8);
            __m256 v2 = _mm256_loadu_ps(in + i + 16);
            __m256 v3 = _mm256_loadu_ps(in + i + 24);
            __m256 cmp0 = _mm256_cmp_ps(v0, v_threshold, _CMP_GT_OS);
            __m256 cmp1 = _mm256_cmp_ps(v1, v_threshold, _CMP_GT_OS);
            __m256 cmp2 = _mm256_cmp_ps(v2, v_threshold, _CMP_GT_OS);
            __m256 cmp3 = _mm256_cmp_ps(v3, v_threshold, _CMP_GT_OS);
            _mm256_storeu_ps(out + i, _mm256_blendv_ps(v_zero, v_one, cmp0));
            _mm256_storeu_ps(out + i + 8, _mm256_blendv_ps(v_zero, v_one, cmp1));
            _mm256_storeu_ps(out + i + 16, _mm256_blendv_ps(v_zero, v_one, cmp2));
            _mm256_storeu_ps(out + i + 24, _mm256_blendv_ps(v_zero, v_one, cmp3));
        }

        for (; i < (count - (count % 8)); i += 8) {
            __m256 v = _mm256_loadu_ps(in + i);
            __m256 cmp = _mm256_cmp_ps(v, v_threshold, _CMP_GT_OS);
            _mm256_storeu_ps(out + i, _mm256_blendv_ps(v_zero, v_one, cmp));
        }

        for (; i < count; ++i) out[i] = in[i] > threshold ? 1.0f : 0.0f;
        _mm256_zeroupper();
    }

    inline void PeakDetectStereoAVX2(float* out_peak, const float* inL, const float* inR, size_t count) {
        size_t i = 0;
        size_t aligned_count = count - (count % 8);
        __m256 v_sign_mask = _mm256_set1_ps(-0.0f);

        for (; i < aligned_count; i += 8) {
            __m256 vL = _mm256_loadu_ps(inL + i);
            __m256 vR = _mm256_loadu_ps(inR + i);
            vL = _mm256_andnot_ps(v_sign_mask, vL);
            vR = _mm256_andnot_ps(v_sign_mask, vR);
            _mm256_storeu_ps(out_peak + i, _mm256_max_ps(vL, vR));
        }

        for (; i < count; ++i) out_peak[i] = (std::max)(std::abs(inL[i]), std::abs(inR[i]));
        _mm256_zeroupper();
    }

    struct ParticleBatchParams {
        int32_t start_idx;
        int32_t countPerStep;
        int32_t k_min;
        float emissionInterval;
        float timeSinceStart;
        uint32_t baseSeed;
        float cx, cy;
        int32_t scrollMode;
        float gravity;
        float particleLife;
    };

    inline int ComputeParticleBatchAVX2(const ParticleBatchParams& p, float* out_x, float* out_y, float* out_age) {
        __m256 v_gravity = _mm256_set1_ps(p.gravity);
        __m256 v_life = _mm256_set1_ps(p.particleLife);
        __m256 v_zero = _mm256_setzero_ps();
        __m256 v_cx = _mm256_set1_ps(p.cx);
        __m256 v_cy = _mm256_set1_ps(p.cy);
        __m256i v_idx = _mm256_setr_epi32(p.start_idx, p.start_idx + 1, p.start_idx + 2, p.start_idx + 3, p.start_idx + 4, p.start_idx + 5, p.start_idx + 6, p.start_idx + 7);
        __m256 v_idx_ps = _mm256_cvtepi32_ps(v_idx);
        __m256 v_count_ps = _mm256_cvtepi32_ps(_mm256_set1_epi32(p.countPerStep));
        __m256 v_k_rel_ps = _mm256_floor_ps(_mm256_div_ps(v_idx_ps, v_count_ps));
        __m256i v_k_rel_i = _mm256_cvtps_epi32(v_k_rel_ps);
        __m256i v_k_i = _mm256_add_epi32(_mm256_set1_epi32(p.k_min), v_k_rel_i);
        __m256i v_count_i = _mm256_set1_epi32(p.countPerStep);
        __m256i v_p_i = _mm256_sub_epi32(v_idx, _mm256_mullo_epi32(v_k_rel_i, v_count_i));
        __m256 v_k_ps = _mm256_cvtepi32_ps(v_k_i);
        __m256 v_emitTime = _mm256_mul_ps(v_k_ps, _mm256_set1_ps(p.emissionInterval));
        __m256 v_age = _mm256_sub_ps(_mm256_set1_ps(p.timeSinceStart), v_emitTime);
        __m256i v_baseSeed = _mm256_set1_epi32(p.baseSeed);
        __m256i v_seed = _mm256_add_epi32(v_baseSeed, _mm256_mullo_epi32(v_k_i, _mm256_set1_epi32(7193)));
        v_seed = _mm256_add_epi32(v_seed, _mm256_mullo_epi32(v_p_i, _mm256_set1_epi32(31337)));
        v_seed = _mm256_xor_si256(v_seed, _mm256_slli_epi32(v_seed, 13));
        v_seed = _mm256_xor_si256(v_seed, _mm256_srli_epi32(v_seed, 17));
        v_seed = _mm256_xor_si256(v_seed, _mm256_slli_epi32(v_seed, 5));
        __m256 v_rand1 = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_and_si256(v_seed, _mm256_set1_epi32(0xFFFFFF))), _mm256_set1_ps(1.0f / 16777215.0f));
        v_seed = _mm256_xor_si256(v_seed, _mm256_slli_epi32(v_seed, 13));
        v_seed = _mm256_xor_si256(v_seed, _mm256_srli_epi32(v_seed, 17));
        v_seed = _mm256_xor_si256(v_seed, _mm256_slli_epi32(v_seed, 5));
        __m256 v_rand2 = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_and_si256(v_seed, _mm256_set1_epi32(0xFFFFFF))), _mm256_set1_ps(1.0f / 16777215.0f));
        v_seed = _mm256_xor_si256(v_seed, _mm256_slli_epi32(v_seed, 13));
        v_seed = _mm256_xor_si256(v_seed, _mm256_srli_epi32(v_seed, 17));
        v_seed = _mm256_xor_si256(v_seed, _mm256_slli_epi32(v_seed, 5));
        __m256 v_rand3 = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_and_si256(v_seed, _mm256_set1_epi32(0xFFFFFF))), _mm256_set1_ps(1.0f / 16777215.0f));
        __m256 v_one = _mm256_set1_ps(1.0f);
        __m256 v_two = _mm256_set1_ps(2.0f);
        __m256 v_rx = _mm256_sub_ps(_mm256_mul_ps(v_rand1, v_two), v_one);
        __m256 v_ry = _mm256_sub_ps(_mm256_mul_ps(v_rand2, v_two), v_one);
        __m256 v_lenSq = _mm256_add_ps(_mm256_mul_ps(v_rx, v_rx), _mm256_mul_ps(v_ry, v_ry));
        __m256 v_invLen = _mm256_rsqrt_ps(_mm256_add_ps(v_lenSq, _mm256_set1_ps(1e-6f)));
        __m256 v_speed = _mm256_add_ps(_mm256_set1_ps(50.0f), _mm256_mul_ps(v_rand3, _mm256_set1_ps(250.0f)));
        __m256 v_vx = _mm256_mul_ps(v_rx, _mm256_mul_ps(v_invLen, v_speed));
        __m256 v_vy = _mm256_mul_ps(v_ry, _mm256_mul_ps(v_invLen, v_speed));
        __m256 v_px = _mm256_add_ps(v_cx, _mm256_mul_ps(v_vx, v_age));
        __m256 v_py = _mm256_add_ps(v_cy, _mm256_mul_ps(v_vy, v_age));
        __m256 v_g_delta = _mm256_mul_ps(_mm256_set1_ps(0.5f), _mm256_mul_ps(v_gravity, _mm256_mul_ps(v_age, v_age)));
        if (p.scrollMode == 3) v_py = _mm256_sub_ps(v_py, v_g_delta);
        else v_py = _mm256_add_ps(v_py, v_g_delta);
        __m256 v_mask = _mm256_and_ps(
            _mm256_cmp_ps(v_age, v_zero, _CMP_GT_OQ),
            _mm256_cmp_ps(v_age, v_life, _CMP_LT_OQ)
        );
        _mm256_storeu_ps(out_x, v_px);
        _mm256_storeu_ps(out_y, v_py);
        _mm256_storeu_ps(out_age, v_age);
        _mm256_zeroupper();
        return _mm256_movemask_ps(v_mask);
    }

    inline void FillBufferRGBAx8(PIXEL_RGBA* buf, size_t pixelCount, PIXEL_RGBA color) {
        if (pixelCount <= 0) return;
        uint32_t colorU32 = *(uint32_t*)&color;
        __m256i v_color = _mm256_setr_epi32(colorU32, colorU32, colorU32, colorU32, colorU32, colorU32, colorU32, colorU32);
        
        size_t i = 0;
        size_t aligned = pixelCount - (pixelCount % 8);
        uint32_t* buf32 = (uint32_t*)buf;
        
        for (; i < aligned; i += 8) _mm256_storeu_si256((__m256i*)(buf32 + i), v_color);
        for (; i < pixelCount; ++i) buf[i] = color;
    }

    inline void BlendPixelBatchAVX2(PIXEL_RGBA* buf, const int32_t* xs, const int32_t* ys, int count, int imgW, int imgH, PIXEL_RGBA col) {
        if (count <= 0 || col.a == 0) return;
        __m256 v_alpha = _mm256_set1_ps(col.a / 255.0f);
        __m256 v_invAlpha = _mm256_set1_ps(1.0f - col.a / 255.0f);
        __m256 v_cr = _mm256_set1_ps((float)col.r);
        __m256 v_cg = _mm256_set1_ps((float)col.g);
        __m256 v_cb = _mm256_set1_ps((float)col.b);
        __m256 v_ca = _mm256_set1_ps((float)col.a);

        for (int k = 0; k < count; ++k) {
            int32_t x = xs[k];
            int32_t y = ys[k];
            if (x < 0 || y < 0 || x >= imgW || y >= imgH) continue;
            int32_t idx = y * imgW + x;
            if (col.a == 255) {
                buf[idx] = col;
            } else {
                PIXEL_RGBA bg = buf[idx];
                __m256 v_bgr = _mm256_set1_ps((float)bg.r);
                __m256 v_bgg = _mm256_set1_ps((float)bg.g);
                __m256 v_bgb = _mm256_set1_ps((float)bg.b);
                __m256 v_bga = _mm256_set1_ps((float)bg.a);
                __m256 v_outr = _mm256_add_ps(_mm256_mul_ps(v_cr, v_alpha), _mm256_mul_ps(v_bgr, v_invAlpha));
                __m256 v_outg = _mm256_add_ps(_mm256_mul_ps(v_cg, v_alpha), _mm256_mul_ps(v_bgg, v_invAlpha));
                __m256 v_outb = _mm256_add_ps(_mm256_mul_ps(v_cb, v_alpha), _mm256_mul_ps(v_bgb, v_invAlpha));
                __m256 v_outa = _mm256_min_ps(_mm256_set1_ps(255.0f), _mm256_add_ps(v_bga, v_ca));
                alignas(32) float outr[8], outg[8], outb[8], outa[8];
                _mm256_store_ps(outr, v_outr);
                _mm256_store_ps(outg, v_outg);
                _mm256_store_ps(outb, v_outb);
                _mm256_store_ps(outa, v_outa);
                buf[idx].r = (uint8_t)outr[0];
                buf[idx].g = (uint8_t)outg[0];
                buf[idx].b = (uint8_t)outb[0];
                buf[idx].a = (uint8_t)outa[0];
            }
        }
        _mm256_zeroupper();
    }

    inline void BlendPointsAVX2(PIXEL_RGBA* img, int imgW, int imgH, const float* px, const float* py, const float* ages, int count, PIXEL_RGBA color, float particleLife) {
        if (count <= 0) return;
        __m256 v_life = _mm256_set1_ps(particleLife);
        __m256 v_zero = _mm256_setzero_ps();
        __m256 v_one = _mm256_set1_ps(1.0f);
        __m256 v_alpha_scale = _mm256_set1_ps(color.a / 255.0f);
        __m256 v_px = _mm256_loadu_ps(px);
        __m256 v_py = _mm256_loadu_ps(py);
        __m256 v_age = _mm256_loadu_ps(ages);
        __m256 v_valid_lo = _mm256_cmp_ps(v_age, v_zero, _CMP_GT_OQ);
        __m256 v_valid_hi = _mm256_cmp_ps(v_age, v_life, _CMP_LT_OQ);
        __m256 v_valid = _mm256_and_ps(v_valid_lo, v_valid_hi);
        int32_t valid_mask = _mm256_movemask_ps(v_valid);
        __m256 v_age_norm = _mm256_div_ps(v_age, v_life);
        __m256 v_alpha = _mm256_mul_ps(_mm256_sub_ps(v_one, v_age_norm), v_alpha_scale);
        __m256 v_cr = _mm256_set1_ps((float)color.r);
        __m256 v_cg = _mm256_set1_ps((float)color.g);
        __m256 v_cb = _mm256_set1_ps((float)color.b);
        __m256 v_r = _mm256_mul_ps(v_cr, v_alpha);
        __m256 v_g = _mm256_mul_ps(v_cg, v_alpha);
        __m256 v_b = _mm256_mul_ps(v_cb, v_alpha);

        alignas(32) float pxs[8], pys[8], rx[8], gx[8], bx[8], ax[8];
        _mm256_store_ps(pxs, v_px);
        _mm256_store_ps(pys, v_py);
        _mm256_store_ps(rx, v_r);
        _mm256_store_ps(gx, v_g);
        _mm256_store_ps(bx, v_b);
        _mm256_store_ps(ax, v_alpha);

        for (int k = 0; k < count; ++k) {
            if (!((valid_mask >> k) & 1)) continue;
            int ix = (int)pxs[k];
            int iy = (int)pys[k];
            if (ix < 0 || iy < 0 || ix >= imgW || iy >= imgH) continue;
            float alpha = ax[k];
            float lifeRatio = ages[k] / particleLife;
            int pSize = (int)(3.0f * (1.0f - lifeRatio) + 1.0f);
            if (pSize < 1) pSize = 1;
            float invA = 1.0f - alpha;
            for (int dy = 0; dy < pSize; ++dy) {
                int yy = iy + dy;
                if (yy >= imgH) break;
                int base = yy * imgW;
                for (int dx = 0; dx < pSize; ++dx) {
                    int xx = ix + dx;
                    if (xx >= imgW) break;
                    int idx = base + xx;
                    PIXEL_RGBA src = img[idx];
                    PIXEL_RGBA out;
                    out.r = (uint8_t)(rx[k] + src.r * invA);
                    out.g = (uint8_t)(gx[k] + src.g * invA);
                    out.b = (uint8_t)(bx[k] + src.b * invA);
                    out.a = (uint8_t)std::min<int>(255, (int)(src.a + color.a * alpha));
                    img[idx] = out;
                }
            }
        }
        _mm256_zeroupper();
    }

    inline void ComputeRingAlphaMaskAVX2(float* outAlpha, int32_t rectX, int32_t rectY, int32_t rectW, int32_t rectH, int32_t imgW, int32_t imgH, float cx, float cy, float radius, float thickness) {
        if (rectW <= 0 || rectH <= 0) return;
        float rOut = radius;
        float rIn = (radius - thickness);
        if (rIn < 0.0f) rIn = 0.0f;
        float rOut2 = rOut * rOut;
        float rIn2 = rIn * rIn;

        const int32_t simdWidth = 8;
        __m256 v_cx = _mm256_set1_ps(cx);
        __m256 v_cy = _mm256_set1_ps(cy);
        __m256 v_rOut2 = _mm256_set1_ps(rOut2);
        __m256 v_rIn2 = _mm256_set1_ps(rIn2);
        __m256 v_one = _mm256_set1_ps(1.0f);

        for (int32_t yy = 0; yy < rectH; ++yy) {
            int32_t y = rectY + yy;
            float dy = (float)y - cy;
            __m256 v_dy = _mm256_set1_ps(dy);
            __m256 v_dy2 = _mm256_mul_ps(v_dy, v_dy);

            int32_t xx = 0;
            for (; xx <= rectW - simdWidth; xx += simdWidth) {
                alignas(32) float xs[8];
                for (int k = 0; k < 8; ++k) xs[k] = (float)(rectX + xx + k);
                __m256 v_x = _mm256_load_ps(xs);
                __m256 v_dx = _mm256_sub_ps(v_x, v_cx);
                __m256 v_dx2 = _mm256_mul_ps(v_dx, v_dx);
                __m256 v_d2 = _mm256_add_ps(v_dx2, v_dy2);
                __m256 v_ge_in = _mm256_cmp_ps(v_d2, v_rIn2, _CMP_GE_OS);
                __m256 v_le_out = _mm256_cmp_ps(v_d2, v_rOut2, _CMP_LE_OS);
                __m256 v_mask = _mm256_and_ps(v_ge_in, v_le_out);
                __m256 v_distOut = _mm256_sub_ps(v_rOut2, v_d2);
                __m256 v_distIn = _mm256_sub_ps(v_d2, v_rIn2);
                __m256 v_alphaOut = _mm256_min_ps(v_one, _mm256_mul_ps(v_distOut, _mm256_set1_ps(0.25f)));
                __m256 v_alphaIn = _mm256_min_ps(v_one, _mm256_mul_ps(v_distIn, _mm256_set1_ps(0.25f)));
                __m256 v_alpha = _mm256_min_ps(v_alphaIn, v_alphaOut);
                v_alpha = _mm256_and_ps(v_alpha, v_mask);

                alignas(32) float outVals[8];
                _mm256_store_ps(outVals, v_alpha);
                for (int k = 0; k < 8; ++k) outAlpha[yy * rectW + xx + k] = outVals[k];
            }
            for (; xx < rectW; ++xx) {
                int32_t x = rectX + xx;
                float dx = (float)x - cx;
                float d2 = dx*dx + dy*dy;
                float a = 0.0f;
                if (d2 >= rIn2 && d2 <= rOut2) {
                    float distOut = rOut2 - d2;
                    float distIn = d2 - rIn2;
                    float ao = (std::min)(1.0f, distOut * 0.25f);
                    float ai = (std::min)(1.0f, distIn * 0.25f);
                    a = (std::min)(ao, ai);
                }
                outAlpha[yy * rectW + xx] = a;
            }
        }
        _mm256_zeroupper();
    }

    inline void FillLineRGBAx8(PIXEL_RGBA* buf, int startX, int y, int lineLen, int imgW, int imgH, PIXEL_RGBA color) {
        if (lineLen <= 0 || y < 0 || y >= imgH) return;
        int endX = (std::min)(startX + lineLen, imgW);
        if (startX >= imgW || endX <= 0) return;
        if (startX < 0) startX = 0;
        
        int pixIdx = y * imgW + startX;
        int remaining = endX - startX;
        uint32_t colorU32 = *(uint32_t*)&color;
        __m256i v_color = _mm256_setr_epi32(colorU32, colorU32, colorU32, colorU32, colorU32, colorU32, colorU32, colorU32);
        
        uint32_t* buf32 = (uint32_t*)(buf + pixIdx);
        int aligned = remaining - (remaining % 8);
        
        for (int i = 0; i < aligned; i += 8) _mm256_storeu_si256((__m256i*)(buf32 + i), v_color);
        for (int i = aligned; i < remaining; ++i) buf[pixIdx + i] = color;
    }

    inline void BlendLineRGBAx8(PIXEL_RGBA* buf, int startX, int y, int lineLen, int imgW, int imgH, PIXEL_RGBA color) {
        if (lineLen <= 0 || y < 0 || y >= imgH || color.a == 0) return;
        if (color.a == 255) {
            FillLineRGBAx8(buf, startX, y, lineLen, imgW, imgH, color);
            return;
        }
        
        int endX = (std::min)(startX + lineLen, imgW);
        if (startX >= imgW || endX <= 0) return;
        if (startX < 0) startX = 0;
        
        int pixIdx = y * imgW + startX;
        int remaining = endX - startX;
        
        __m256 v_alpha = _mm256_set1_ps(color.a / 255.0f);
        __m256 v_invAlpha = _mm256_set1_ps(1.0f - color.a / 255.0f);
        __m256 v_cr = _mm256_set1_ps((float)color.r);
        __m256 v_cg = _mm256_set1_ps((float)color.g);
        __m256 v_cb = _mm256_set1_ps((float)color.b);
        __m256 v_ca = _mm256_set1_ps((float)color.a);
        __m256 v_255 = _mm256_set1_ps(255.0f);
        
        for (int i = 0; i < remaining - 7; i += 8) {
            __m256i bg_packed = _mm256_loadu_si256((__m256i*)(buf + pixIdx + i));
            alignas(32) uint32_t bgPackedArr[8];
            _mm256_store_si256((__m256i*)bgPackedArr, bg_packed);
            
            alignas(32) float bgr[8], bgg[8], bgb[8], bga[8];
            for (int j = 0; j < 8; ++j) {
                uint32_t px = bgPackedArr[j];
                bgr[j] = (float)((uint8_t)(px & 0xFF));
                bgg[j] = (float)((uint8_t)((px >> 8) & 0xFF));
                bgb[j] = (float)((uint8_t)((px >> 16) & 0xFF));
                bga[j] = (float)((uint8_t)((px >> 24) & 0xFF));
            }
            
            __m256 v_bgr = _mm256_load_ps(bgr);
            __m256 v_bgg = _mm256_load_ps(bgg);
            __m256 v_bgb = _mm256_load_ps(bgb);
            __m256 v_bga = _mm256_load_ps(bga);
            __m256 v_outr = _mm256_add_ps(_mm256_mul_ps(v_cr, v_alpha), _mm256_mul_ps(v_bgr, v_invAlpha));
            __m256 v_outg = _mm256_add_ps(_mm256_mul_ps(v_cg, v_alpha), _mm256_mul_ps(v_bgg, v_invAlpha));
            __m256 v_outb = _mm256_add_ps(_mm256_mul_ps(v_cb, v_alpha), _mm256_mul_ps(v_bgb, v_invAlpha));
            __m256 v_outa = _mm256_min_ps(v_255, _mm256_add_ps(v_bga, v_ca));
            
            alignas(32) float outr[8], outg[8], outb[8], outa[8];
            _mm256_store_ps(outr, v_outr);
            _mm256_store_ps(outg, v_outg);
            _mm256_store_ps(outb, v_outb);
            _mm256_store_ps(outa, v_outa);
            
            for (int j = 0; j < 8; ++j) {
                buf[pixIdx + i + j].r = (uint8_t)outr[j];
                buf[pixIdx + i + j].g = (uint8_t)outg[j];
                buf[pixIdx + i + j].b = (uint8_t)outb[j];
                buf[pixIdx + i + j].a = (uint8_t)outa[j];
            }
        }
        
        int aligned = remaining - (remaining % 8);
        for (int i = aligned; i < remaining; ++i) {
            PIXEL_RGBA bg = buf[pixIdx + i];
            float alpha = color.a / 255.0f;
            float invAlpha = 1.0f - alpha;
            buf[pixIdx + i].r = (uint8_t)(color.r * alpha + bg.r * invAlpha);
            buf[pixIdx + i].g = (uint8_t)(color.g * alpha + bg.g * invAlpha);
            buf[pixIdx + i].b = (uint8_t)(color.b * alpha + bg.b * invAlpha);
            buf[pixIdx + i].a = (uint8_t)(std::min)(255, (int)(bg.a + color.a));
        }
        _mm256_zeroupper();
    }

    inline void FillVerticalLineRGBAx8(PIXEL_RGBA* buf, int x, int startY, int lineLen, int imgW, int imgH, PIXEL_RGBA color) {
        if (lineLen <= 0 || x < 0 || x >= imgW) return;
        int endY = (std::min)(startY + lineLen, imgH);
        if (startY >= imgH || endY <= 0) return;
        if (startY < 0) startY = 0;
        
        int remaining = endY - startY;
        for (int i = 0; i < remaining; ++i) buf[(startY + i) * imgW + x] = color;
    }

    inline void BlendVerticalLineRGBAx8(PIXEL_RGBA* buf, int x, int startY, int lineLen, int imgW, int imgH, PIXEL_RGBA color) {
        if (lineLen <= 0 || x < 0 || x >= imgW || color.a == 0) return;
        if (color.a == 255) {
            FillVerticalLineRGBAx8(buf, x, startY, lineLen, imgW, imgH, color);
            return;
        }
        
        int endY = (std::min)(startY + lineLen, imgH);
        if (startY >= imgH || endY <= 0) return;
        if (startY < 0) startY = 0;
        
        int remaining = endY - startY;
        float alpha = color.a / 255.0f;
        float invAlpha = 1.0f - alpha;
        
        for (int i = 0; i < remaining - 7; i += 8) {
            alignas(32) float pixr[8], pixg[8], pixb[8], pixa[8];
            
            for (int j = 0; j < 8; ++j) {
                PIXEL_RGBA bg = buf[(startY + i + j) * imgW + x];
                pixr[j] = (float)bg.r;
                pixg[j] = (float)bg.g;
                pixb[j] = (float)bg.b;
                pixa[j] = (float)bg.a;
            }
            
            __m256 v_bgr = _mm256_load_ps(pixr);
            __m256 v_bgg = _mm256_load_ps(pixg);
            __m256 v_bgb = _mm256_load_ps(pixb);
            __m256 v_bga = _mm256_load_ps(pixa);
            __m256 v_alpha = _mm256_set1_ps(alpha);
            __m256 v_invAlpha = _mm256_set1_ps(invAlpha);
            __m256 v_cr = _mm256_set1_ps((float)color.r);
            __m256 v_cg = _mm256_set1_ps((float)color.g);
            __m256 v_cb = _mm256_set1_ps((float)color.b);
            __m256 v_ca = _mm256_set1_ps((float)color.a);
            __m256 v_255 = _mm256_set1_ps(255.0f);
            __m256 v_outr = _mm256_add_ps(_mm256_mul_ps(v_cr, v_alpha), _mm256_mul_ps(v_bgr, v_invAlpha));
            __m256 v_outg = _mm256_add_ps(_mm256_mul_ps(v_cg, v_alpha), _mm256_mul_ps(v_bgg, v_invAlpha));
            __m256 v_outb = _mm256_add_ps(_mm256_mul_ps(v_cb, v_alpha), _mm256_mul_ps(v_bgb, v_invAlpha));
            __m256 v_outa = _mm256_min_ps(v_255, _mm256_add_ps(v_bga, v_ca));
            
            alignas(32) float outr[8], outg[8], outb[8], outa[8];
            _mm256_store_ps(outr, v_outr);
            _mm256_store_ps(outg, v_outg);
            _mm256_store_ps(outb, v_outb);
            _mm256_store_ps(outa, v_outa);
            
            for (int j = 0; j < 8; ++j) {
                int idx = (startY + i + j) * imgW + x;
                buf[idx].r = (uint8_t)outr[j];
                buf[idx].g = (uint8_t)outg[j];
                buf[idx].b = (uint8_t)outb[j];
                buf[idx].a = (uint8_t)outa[j];
            }
        }
        
        int aligned = remaining - (remaining % 8);
        for (int i = aligned; i < remaining; ++i) {
            int idx = (startY + i) * imgW + x;
            PIXEL_RGBA bg = buf[idx];
            buf[idx].r = (uint8_t)(color.r * alpha + bg.r * invAlpha);
            buf[idx].g = (uint8_t)(color.g * alpha + bg.g * invAlpha);
            buf[idx].b = (uint8_t)(color.b * alpha + bg.b * invAlpha);
            buf[idx].a = (uint8_t)(std::min)(255, (int)(bg.a + color.a));
        }
        _mm256_zeroupper();
    }
}
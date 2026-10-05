#include "atum/color/color_gamut.hpp"

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace atum {

ColorMatrix3x3 ColorMatrix3x3::operator*(const ColorMatrix3x3& other) const noexcept {
    ColorMatrix3x3 res;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            res.m[r * 3 + c] = m[r * 3 + 0] * other.m[0 * 3 + c]
                             + m[r * 3 + 1] * other.m[1 * 3 + c]
                             + m[r * 3 + 2] * other.m[2 * 3 + c];
        }
    }
    return res;
}

ColorMatrix3x3 ColorMatrix3x3::get_transform(ColorPrimaries src, ColorPrimaries dst) noexcept {
    if (src == dst || src == ColorPrimaries::Unspecified || dst == ColorPrimaries::Unspecified ||
        src == ColorPrimaries::Unknown || dst == ColorPrimaries::Unknown) {
        return identity();
    }

    if (src == ColorPrimaries::BT2020) {
        if (dst == ColorPrimaries::BT709) return bt2020_to_bt709();
        if (dst == ColorPrimaries::DCI_P3 || dst == ColorPrimaries::Display_P3) {
            return bt2020_to_dcip3();
        }
    } else if (src == ColorPrimaries::BT709) {
        if (dst == ColorPrimaries::BT2020) return bt709_to_bt2020();
        if (dst == ColorPrimaries::DCI_P3 || dst == ColorPrimaries::Display_P3) {
            return bt709_to_dcip3();
        }
    } else if (src == ColorPrimaries::DCI_P3 || src == ColorPrimaries::Display_P3) {
        if (dst == ColorPrimaries::BT2020) return dcip3_to_bt2020();
        if (dst == ColorPrimaries::BT709) return dcip3_to_bt709();
    }

    return identity();
}

void ColorMatrix3x3::transform_batch(const float* __restrict r_in,
                                     const float* __restrict g_in,
                                     const float* __restrict b_in,
                                     float* __restrict r_out,
                                     float* __restrict g_out,
                                     float* __restrict b_out,
                                     size_t count) const noexcept {
    size_t i = 0;

#if defined(__AVX2__)
    const __m256 m00 = _mm256_set1_ps(m[0]);
    const __m256 m01 = _mm256_set1_ps(m[1]);
    const __m256 m02 = _mm256_set1_ps(m[2]);

    const __m256 m10 = _mm256_set1_ps(m[3]);
    const __m256 m11 = _mm256_set1_ps(m[4]);
    const __m256 m12 = _mm256_set1_ps(m[5]);

    const __m256 m20 = _mm256_set1_ps(m[6]);
    const __m256 m21 = _mm256_set1_ps(m[7]);
    const __m256 m22 = _mm256_set1_ps(m[8]);

    for (; i + 8 <= count; i += 8) {
        const __m256 r = _mm256_loadu_ps(r_in + i);
        const __m256 g = _mm256_loadu_ps(g_in + i);
        const __m256 b = _mm256_loadu_ps(b_in + i);

#if defined(__FMA__)
        const __m256 ro = _mm256_fmadd_ps(m02, b, _mm256_fmadd_ps(m01, g, _mm256_mul_ps(m00, r)));
        const __m256 go = _mm256_fmadd_ps(m12, b, _mm256_fmadd_ps(m11, g, _mm256_mul_ps(m10, r)));
        const __m256 bo = _mm256_fmadd_ps(m22, b, _mm256_fmadd_ps(m21, g, _mm256_mul_ps(m20, r)));
#else
        const __m256 ro = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(m00, r), _mm256_mul_ps(m01, g)), _mm256_mul_ps(m02, b));
        const __m256 go = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(m10, r), _mm256_mul_ps(m11, g)), _mm256_mul_ps(m12, b));
        const __m256 bo = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(m20, r), _mm256_mul_ps(m21, g)), _mm256_mul_ps(m22, b));
#endif

        _mm256_storeu_ps(r_out + i, ro);
        _mm256_storeu_ps(g_out + i, go);
        _mm256_storeu_ps(b_out + i, bo);
    }
#endif

    for (; i < count; ++i) {
        const float r = r_in[i];
        const float g = g_in[i];
        const float b = b_in[i];
        r_out[i] = m[0] * r + m[1] * g + m[2] * b;
        g_out[i] = m[3] * r + m[4] * g + m[5] * b;
        b_out[i] = m[6] * r + m[7] * g + m[8] * b;
    }
}

void ColorMatrix3x3::transform_rgba_batch(float* __restrict rgba, size_t pixel_count) const noexcept {
    for (size_t i = 0; i < pixel_count; ++i) {
        float* p = rgba + i * 4;
        const float r = p[0];
        const float g = p[1];
        const float b = p[2];
        p[0] = m[0] * r + m[1] * g + m[2] * b;
        p[1] = m[3] * r + m[4] * g + m[5] * b;
        p[2] = m[6] * r + m[7] * g + m[8] * b;
        // alpha untouched
    }
}

} // namespace atum

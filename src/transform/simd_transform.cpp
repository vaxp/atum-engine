#include "atum/transform/simd_transform.hpp"
#include "atum/transform/h264_transform.hpp"
#include <algorithm>
#include <cstring>

namespace atum::simd {

#if defined(NISABA_MEDIA_HAS_SSE2)

namespace {

inline void transpose4x4_epi32(__m128i& r0, __m128i& r1, __m128i& r2, __m128i& r3) noexcept {
    __m128 t0 = _mm_castsi128_ps(r0);
    __m128 t1 = _mm_castsi128_ps(r1);
    __m128 t2 = _mm_castsi128_ps(r2);
    __m128 t3 = _mm_castsi128_ps(r3);
    _MM_TRANSPOSE4_PS(t0, t1, t2, t3);
    r0 = _mm_castps_si128(t0);
    r1 = _mm_castps_si128(t1);
    r2 = _mm_castps_si128(t2);
    r3 = _mm_castps_si128(t3);
}

inline void idct4x4_1d_pass(__m128i& r0, __m128i& r1, __m128i& r2, __m128i& r3) noexcept {
    // r0 = c0, r1 = c1, r2 = c2, r3 = c3 across the 4 lanes
    __m128i e0 = _mm_add_epi32(r0, r2);
    __m128i e1 = _mm_sub_epi32(r0, r2);
    __m128i c1_s1 = _mm_srai_epi32(r1, 1);
    __m128i c3_s1 = _mm_srai_epi32(r3, 1);
    __m128i e2 = _mm_sub_epi32(c1_s1, r3);
    __m128i e3 = _mm_add_epi32(r1, c3_s1);

    r0 = _mm_add_epi32(e0, e3);
    r1 = _mm_add_epi32(e1, e2);
    r2 = _mm_sub_epi32(e1, e2);
    r3 = _mm_sub_epi32(e0, e3);
}

} // anonymous namespace

void idct4x4_h264(int32_t* block) noexcept {
    __m128i r0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 0));
    __m128i r1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 4));
    __m128i r2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 8));
    __m128i r3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 12));

    // Horizontal pass: transpose so lanes represent row elements
    transpose4x4_epi32(r0, r1, r2, r3);
    idct4x4_1d_pass(r0, r1, r2, r3);

    // Vertical pass: transpose back so lanes represent column elements
    transpose4x4_epi32(r0, r1, r2, r3);
    idct4x4_1d_pass(r0, r1, r2, r3);

    _mm_storeu_si128(reinterpret_cast<__m128i*>(block + 0), r0);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(block + 4), r1);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(block + 8), r2);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(block + 12), r3);
}

void idct4x4_h264_add(
    const int32_t* residual,
    const uint8_t* pred,
    uint8_t* dst,
    size_t dst_stride,
    size_t pred_stride
) noexcept {
    size_t p_stride = (pred_stride != 0) ? pred_stride : dst_stride;
    int32_t tmp[16];
    std::copy_n(residual, 16, tmp);
    idct4x4_h264(tmp);

    __m128i v_32 = _mm_set1_epi32(32);
    for (int y = 0; y < 4; ++y) {
        __m128i r = _mm_loadu_si128(reinterpret_cast<const __m128i*>(tmp + y * 4));
        r = _mm_srai_epi32(_mm_add_epi32(r, v_32), 6);

        // Convert pred 4 bytes into 4 int32
        uint32_t p_raw;
        std::memcpy(&p_raw, pred + y * p_stride, 4);
        __m128i p_128 = _mm_cvtsi32_si128(static_cast<int>(p_raw));
        p_128 = _mm_unpacklo_epi8(p_128, _mm_setzero_si128());
        p_128 = _mm_unpacklo_epi16(p_128, _mm_setzero_si128());

        __m128i sum = _mm_add_epi32(p_128, r);

        // Pack clamped int32 -> uint16 -> uint8
        __m128i pack16 = _mm_packs_epi32(sum, sum);
        __m128i pack8 = _mm_packus_epi16(pack16, pack16);

        uint32_t out_val = static_cast<uint32_t>(_mm_cvtsi128_si32(pack8));
        std::memcpy(dst + y * dst_stride, &out_val, 4);
    }
}

void idct8x8_h264(int32_t* block) noexcept {
    H264Transform::idct8x8(block);
}

#else

void idct4x4_h264(int32_t* block) noexcept {
    H264Transform::idct4x4(block);
}

void idct4x4_h264_add(
    const int32_t* residual,
    const uint8_t* pred,
    uint8_t* dst,
    size_t dst_stride,
    size_t pred_stride
) noexcept {
    size_t p_stride = (pred_stride != 0) ? pred_stride : dst_stride;
    int32_t tmp[16];
    std::copy_n(residual, 16, tmp);
    H264Transform::idct4x4(tmp);

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int32_t res = (tmp[y * 4 + x] + 32) >> 6;
            int32_t p = pred[y * p_stride + x];
            dst[y * dst_stride + x] = static_cast<uint8_t>(std::clamp(p + res, 0, 255));
        }
    }
}

void idct8x8_h264(int32_t* block) noexcept {
    H264Transform::idct8x8(block);
}

#endif

} // namespace atum::simd

#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include "atum/transform/idct_types.hpp"

namespace atum {

/// Sovereign HEVC / H.265 Inverse Transforms (DCT-II & 4x4 DST-VII).
/// Implements ITU-T H.265 (ISO/IEC 23008-2) Section 8.6.4.
class HevcTransform {
public:
    /// In-place 4x4 Inverse Discrete Cosine Transform (DCT-II).
    static void idct4x4(int32_t* block, int32_t bit_depth = 8) noexcept;

    /// In-place 4x4 Inverse Discrete Sine Transform (DST-VII) used for 4x4 Intra Luma.
    static void idst4x4(int32_t* block, int32_t bit_depth = 8) noexcept;

    /// In-place 8x8 Inverse Discrete Cosine Transform (DCT-II).
    static void idct8x8(int32_t* block, int32_t bit_depth = 8) noexcept;

    /// In-place 16x16 Inverse Discrete Cosine Transform (DCT-II).
    static void idct16x16(int32_t* block, int32_t bit_depth = 8) noexcept;

    /// In-place 32x32 Inverse Discrete Cosine Transform (DCT-II).
    static void idct32x32(int32_t* block, int32_t bit_depth = 8) noexcept;

    /// Generic inverse transform dispatcher based on block size and transform type.
    static void transform_add(
        TransformSize size,
        TransformType type,
        const int32_t* residual,
        const uint8_t* pred,
        uint8_t* dst,
        size_t stride,
        int32_t bit_depth = 8
    ) noexcept;

    // HEVC Core Transform Tables
    static const int8_t hevc_dst4[4][4];
    static const int8_t hevc_dct4[4][4];
    static const int8_t hevc_dct8[8][8];
    static const int8_t hevc_dct16[16][16];
    static const int8_t hevc_dct32[32][32];
};

} // namespace atum

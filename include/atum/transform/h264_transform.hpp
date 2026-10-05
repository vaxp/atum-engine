#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include "atum/transform/idct_types.hpp"

namespace atum {

/// Sovereign H.264 / AVC Inverse Integer Transforms & Dequantization.
/// Implements ITU-T H.264 (ISO/IEC 14496-10) Section 8.5.
class H264Transform {
public:
    /// In-place 4x4 Core Inverse Integer Transform (Section 8.5.8).
    /// `block` is a 16-element array (row-major).
    static void idct4x4(int32_t* block) noexcept;

    /// 4x4 Core Inverse Transform and add residual to 8-bit prediction buffer:
    /// dst[y * stride + x] = clamp(pred[y * stride + x] + residual, 0, 255)
    static void idct4x4_add(
        const int32_t* residual,
        const uint8_t* pred,
        uint8_t* dst,
        size_t stride
    ) noexcept;

    /// In-place 8x8 Inverse Integer Transform for High Profile (Section 8.5.9).
    /// `block` is a 64-element array (row-major).
    static void idct8x8(int32_t* block) noexcept;

    /// 8x8 Inverse Transform and add residual to prediction buffer.
    static void idct8x8_add(
        const int32_t* residual,
        const uint8_t* pred,
        uint8_t* dst,
        size_t stride
    ) noexcept;

    /// In-place 4x4 Luma DC Inverse Hadamard Transform (Section 8.5.6).
    static void hadamard4x4(int32_t* block) noexcept;

    /// In-place 2x2 Chroma DC Inverse Hadamard Transform (Section 8.5.7).
    static void hadamard2x2(int32_t* block) noexcept;

    /// Dequantizes a 4x4 transform block given quantization parameter `qp`.
    static void dequant4x4(int32_t* block, int32_t qp) noexcept;

    /// Dequantizes an 8x8 transform block given quantization parameter `qp`.
    static void dequant8x8(int32_t* block, int32_t qp) noexcept;

    // H.264 Scaling factors table: V(qp % 6, pos)
    static const int32_t dequant_v[6][3];
};

} // namespace atum

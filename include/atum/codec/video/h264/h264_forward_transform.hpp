#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include "atum/transform/idct_types.hpp"

namespace atum {

/// Sovereign H.264 / AVC Forward Integer Transforms & Quantization.
/// Conforms to ITU-T H.264 (ISO/IEC 14496-10) Section 8.5 integer transform standard.
class H264ForwardTransform {
public:
    /// In-place 4x4 Core Forward Integer Transform:
    /// Computes W = C_f * X * C_f^T on a 16-element residual block (row-major).
    static void fdct4x4(int32_t* block) noexcept;

    /// Computes residual X = orig - pred for a 4x4 block, and applies forward transform.
    static void fdct4x4_sub(
        const uint8_t* orig, size_t orig_stride,
        const uint8_t* pred, size_t pred_stride,
        int32_t* dst_transformed
    ) noexcept;

    /// In-place forward 4x4 Luma DC Hadamard transform (Section 8.5.6).
    static void fhadamard4x4(int32_t* block) noexcept;

    /// In-place forward 2x2 Chroma DC Hadamard transform (Section 8.5.7).
    static void fhadamard2x2(int32_t* block) noexcept;

    /// Quantizes a 4x4 transform block given quantization parameter `qp`.
    /// `is_intra`: true uses f = (1 << qbits)/3, false uses f = (1 << qbits)/6.
    /// Returns the number of non-zero coefficients.
    static int32_t quant4x4(int32_t* block, int32_t qp, bool is_intra) noexcept;

    /// Quantizes a 4x4 Luma DC block (after Hadamard transform).
    static int32_t quant4x4_dc(int32_t* block, int32_t qp, bool is_intra) noexcept;

    /// Quantizes a 2x2 Chroma DC block (after Hadamard transform).
    static int32_t quant2x2_dc(int32_t* block, int32_t qp, bool is_intra) noexcept;

    /// H.264 Forward Quantization Multiplier Factors: MF(qp % 6, pos)
    static const int32_t quant_mf[6][3];
};

} // namespace atum

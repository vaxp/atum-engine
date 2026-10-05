#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include "atum/transform/idct_types.hpp"

namespace atum {

/// Sovereign VP9 Inverse Transforms (DCT, ADST, and Walsh-Hadamard).
/// Implements WebM VP9 Bitstream Specification, Section 7.
class Vp9Transform {
public:
    /// In-place 4x4 Inverse Discrete Cosine Transform.
    static void idct4x4(int32_t* block) noexcept;

    /// In-place 4x4 Inverse Asymmetric Discrete Sine Transform (ADST).
    static void iadst4x4(int32_t* block) noexcept;

    /// In-place 8x8 Inverse Discrete Cosine Transform.
    static void idct8x8(int32_t* block) noexcept;

    /// In-place 8x8 Inverse Asymmetric Discrete Sine Transform (ADST).
    static void iadst8x8(int32_t* block) noexcept;

    /// In-place 4x4 Inverse Walsh-Hadamard Transform (for lossless mode).
    static void iwht4x4(int32_t* block) noexcept;

    /// Reconstructs a block and adds residual to prediction buffer:
    /// `tx_type`: 0=DCT_DCT, 1=ADST_DCT, 2=DCT_ADST, 3=ADST_ADST
    static void transform_add(
        uint32_t tx_type,
        TransformSize size,
        const int32_t* residual,
        const uint8_t* pred,
        uint8_t* dst,
        size_t stride
    ) noexcept;
};

} // namespace atum

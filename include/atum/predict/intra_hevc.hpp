#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>

namespace atum {

/// Sovereign HEVC / H.265 Intra Prediction Engine.
/// Implements ITU-T H.265 (ISO/IEC 23008-2) Section 8.4.4 (Planar, DC, and 33 Angular Modes).
class IntraHevc {
public:
    /// Computes HEVC Intra Prediction for a block of size N x N (N = 4, 8, 16, 32).
    /// `mode`: 0 (Planar), 1 (DC), 2..34 (Angular).
    /// `top`: 2*N + 1 reference samples above block (including top-left at index 0).
    /// `left`: 2*N + 1 reference samples left of block (including top-left at index 0).
    /// `dst`: Output residual buffer of size N x N with given stride.
    static void predict(
        uint32_t mode,
        uint32_t n,
        const uint8_t* top,
        const uint8_t* left,
        uint8_t* dst,
        size_t stride
    ) noexcept;

    // HEVC Intra Pred Angles for modes 2..34 (H.265 Table 8-7)
    static const int8_t intra_pred_angle[35];
};

} // namespace atum

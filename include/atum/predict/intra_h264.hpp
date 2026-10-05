#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>

namespace atum {

/// Neighbor availability bitmask flags for Intra prediction.
enum class NeighborFlags : uint8_t {
    None     = 0,
    Left     = 1 << 0,
    Top      = 1 << 1,
    TopLeft  = 1 << 2,
    TopRight = 1 << 3
};

constexpr NeighborFlags operator|(NeighborFlags a, NeighborFlags b) noexcept {
    return static_cast<NeighborFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr bool operator&(NeighborFlags a, NeighborFlags b) noexcept {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

/// Sovereign H.264 / AVC Intra Prediction Engine.
/// Implements ISO/IEC 14496-10 Section 8.3 (Intra 4x4, 16x16, and Chroma 8x8).
class IntraH264 {
public:
    /// Computes 4x4 Intra Prediction for a given mode (0..8).
    /// `top`: 8 samples above the 4x4 block (top[0..3] = top, top[4..7] = top-right).
    /// `left`: 4 samples to the left of the 4x4 block.
    /// `top_left`: 1 sample at the top-left corner.
    /// `dst`: 16-sample output buffer (stride = 4).
    static void predict4x4(
        uint32_t mode,
        NeighborFlags avail,
        const uint8_t* top,
        const uint8_t* left,
        uint8_t top_left,
        uint8_t* dst,
        size_t dst_stride = 4
    ) noexcept;

    /// Computes 16x16 Intra Prediction for a given mode (0..3).
    /// Mode 0: Vertical, Mode 1: Horizontal, Mode 2: DC, Mode 3: Plane.
    static void predict16x16(
        uint32_t mode,
        NeighborFlags avail,
        const uint8_t* top,
        const uint8_t* left,
        uint8_t top_left,
        uint8_t* dst,
        size_t dst_stride = 16
    ) noexcept;

    /// Computes 8x8 Chroma Intra Prediction for a given mode (0..3).
    /// Mode 0: DC, Mode 1: Horizontal, Mode 2: Vertical, Mode 3: Plane.
    static void predict_chroma8x8(
        uint32_t mode,
        NeighborFlags avail,
        const uint8_t* top,
        const uint8_t* left,
        uint8_t top_left,
        uint8_t* dst,
        size_t dst_stride = 8
    ) noexcept;
};

} // namespace atum

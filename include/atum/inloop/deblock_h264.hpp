#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

namespace atum {

/// Sovereign H.264 / AVC Adaptive In-Loop Deblocking Filter.
/// Implements ISO/IEC 14496-10 Section 8.7.
class DeblockH264 {
public:
    /// Filters a single 4-sample edge segment between block P (left/above) and block Q (right/below):
    /// `bS`: Boundary strength (0..4).
    /// `qp`: Average quantization parameter for edge.
    /// `index_a_offset`, `index_b_offset`: slice deblocking parameters (-12..+12).
    /// `pix`: pointer to edge boundary.
    /// `stride`: stride between scanlines.
    /// `is_vertical`: true if vertical edge (horizontal filtering), false if horizontal edge.
    static void filter_edge(
        uint8_t* pix,
        ptrdiff_t stride,
        int32_t bS,
        int32_t qp,
        int32_t index_a_offset = 0,
        int32_t index_b_offset = 0,
        bool is_vertical = true
    ) noexcept;

    /// Filters an entire 16x16 macroblock (vertical edges first, then horizontal edges).
    static void filter_macroblock(
        uint8_t* luma,
        size_t stride,
        const uint8_t bS_vert[4][4],
        const uint8_t bS_horiz[4][4],
        int32_t qp
    ) noexcept;

    // Standard H.264 alpha & beta threshold tables (Table 8-16)
    static const uint8_t alpha_table[52];
    static const uint8_t beta_table[52];
    static const uint8_t tc0_table[52][3];
};

} // namespace atum

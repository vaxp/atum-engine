#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

namespace atum {

/// Sovereign AV1 Constrained Directional Enhancement Filter (CDEF).
/// Implements Alliance for Open Media AV1 Specification, Section 7.15.
class CdefAv1 {
public:
    /// Finds the principal edge direction (0..7) for an 8x8 block.
    static int32_t find_direction(
        const uint8_t* src,
        size_t stride,
        int32_t* cost_out = nullptr
    ) noexcept;

    /// Filters an 8x8 block along the identified direction with specified primary and secondary strengths:
    /// `primary_strength`: threshold for primary directional taps (0..15).
    /// `secondary_strength`: threshold for secondary orthogonal taps (0..4).
    /// `damping`: logarithmic damping factor (3..6).
    static void filter_8x8(
        const uint8_t* src,
        size_t src_stride,
        uint8_t* dst,
        size_t dst_stride,
        int32_t primary_strength,
        int32_t secondary_strength,
        int32_t damping = 5
    ) noexcept;

    // Direction vector offsets for 8 directions: {dy, dx}
    static const int8_t cdef_directions[8][2][2];
};

} // namespace atum

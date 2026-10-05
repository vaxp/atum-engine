#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include "atum/core/media_types.hpp"

namespace atum {

enum class MotionSearchMethod {
    Diamond,    // Large Diamond (LDSP) + Small Diamond (SDSP)
    Hexagonal,  // Large Hexagon (LHSP) + Small Diamond refinement
    FullSearch  // Exhaustive search within window
};

struct MotionVector {
    int16_t x{0}; // In 1/4-pel units (e.g. 4 = 1.0 pixel)
    int16_t y{0}; // In 1/4-pel units
    uint32_t cost{UINT32_MAX};

    [[nodiscard]] constexpr int32_t int_x() const noexcept { return x >> 2; }
    [[nodiscard]] constexpr int32_t int_y() const noexcept { return y >> 2; }
    [[nodiscard]] constexpr int32_t frac_x() const noexcept { return x & 3; }
    [[nodiscard]] constexpr int32_t frac_y() const noexcept { return y & 3; }

    constexpr bool operator==(const MotionVector& other) const noexcept {
        return x == other.x && y == other.y;
    }
};

class MotionEstimator {
public:
    // =========================================================================
    // Distortion Cost Metrics
    // =========================================================================

    /// Sum of Absolute Differences (SAD) for a block of dimensions W x H.
    /// Vectorized with AVX2 / SSE2 when aligned, fallback to scalar.
    static uint32_t sad(
        const uint8_t* src, size_t src_stride,
        const uint8_t* ref, size_t ref_stride,
        uint32_t width, uint32_t height
    ) noexcept;

    static uint32_t sad16x16(
        const uint8_t* src, size_t src_stride,
        const uint8_t* ref, size_t ref_stride
    ) noexcept;

    static uint32_t sad8x8(
        const uint8_t* src, size_t src_stride,
        const uint8_t* ref, size_t ref_stride
    ) noexcept;

    static uint32_t sad4x4(
        const uint8_t* src, size_t src_stride,
        const uint8_t* ref, size_t ref_stride
    ) noexcept;

    /// Sum of Absolute Transformed Differences (SATD) using 4x4 Hadamard transform.
    static uint32_t satd4x4(
        const uint8_t* src, size_t src_stride,
        const uint8_t* ref, size_t ref_stride
    ) noexcept;

    static uint32_t satd16x16(
        const uint8_t* src, size_t src_stride,
        const uint8_t* ref, size_t ref_stride
    ) noexcept;

    // =========================================================================
    // Sub-pixel Fractional Interpolation
    // =========================================================================

    /// Interpolates a sub-pixel block from reference frame at fractional position (sub_x, sub_y) in 1/4-pel units.
    /// Uses H.264 6-tap FIR filter for half-pel and bilinear for quarter-pel.
    static void interpolate_block(
        const uint8_t* ref_plane, size_t ref_stride,
        int32_t frame_width, int32_t frame_height,
        int32_t int_x, int32_t int_y,
        int32_t frac_x, int32_t frac_y,
        uint8_t* dst, size_t dst_stride,
        uint32_t block_w, uint32_t block_h
    ) noexcept;

    // =========================================================================
    // Motion Search Routines
    // =========================================================================

    /// Performs integer motion estimation around search origin (origin_x, origin_y).
    /// `lambda_motion`: Lagrangian multiplier for MV cost penalty.
    static MotionVector search_integer(
        const uint8_t* cur_block, size_t cur_stride,
        const uint8_t* ref_plane, size_t ref_stride,
        int32_t frame_width, int32_t frame_height,
        int32_t cur_x, int32_t cur_y,
        MotionVector mvp,
        int32_t search_range,
        MotionSearchMethod method = MotionSearchMethod::Hexagonal,
        uint32_t lambda_motion = 0,
        uint32_t block_w = 16, uint32_t block_h = 16
    ) noexcept;

    /// Refines integer motion vector to quarter-pel precision (1/2-pel followed by 1/4-pel).
    static MotionVector refine_subpixel(
        const uint8_t* cur_block, size_t cur_stride,
        const uint8_t* ref_plane, size_t ref_stride,
        int32_t frame_width, int32_t frame_height,
        int32_t cur_x, int32_t cur_y,
        MotionVector int_mv,
        MotionVector mvp,
        uint32_t lambda_motion = 0,
        uint32_t block_w = 16, uint32_t block_h = 16
    ) noexcept;

    /// Full motion estimation pipeline (Integer Search + Sub-pixel Refinement).
    static MotionVector estimate_motion(
        const uint8_t* cur_block, size_t cur_stride,
        const uint8_t* ref_plane, size_t ref_stride,
        int32_t frame_width, int32_t frame_height,
        int32_t cur_x, int32_t cur_y,
        MotionVector mvp,
        int32_t search_range = 16,
        MotionSearchMethod method = MotionSearchMethod::Hexagonal,
        uint32_t lambda_motion = 0,
        uint32_t block_w = 16, uint32_t block_h = 16
    ) noexcept;

    /// Calculates Motion Vector Predictor (MVP) via median of Left, Top, Top-Right.
    static MotionVector calculate_mvp(
        MotionVector mv_left, bool has_left,
        MotionVector mv_top, bool has_top,
        MotionVector mv_top_right, bool has_top_right,
        MotionVector mv_top_left, bool has_top_left
    ) noexcept;
};

} // namespace atum

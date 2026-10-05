#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

namespace atum {

/// Sovereign Motion Compensation & Subpixel Interpolation Engine.
/// Implements H.264 6-tap FIR half-pel filter, quarter-pel bilinear, and 1/8-pel chroma interpolation.
class InterFilter {
public:
    /// Interpolates a luma block of size `w x h` from reference picture with subpixel motion vectors:
    /// `mv_x_qpel`, `mv_y_qpel` are in quarter-pel units (1 unit = 1/4 pixel).
    static void interpolate_luma(
        const uint8_t* ref,
        size_t ref_stride,
        int32_t ref_width,
        int32_t ref_height,
        int32_t src_x,
        int32_t src_y,
        int32_t mv_x_qpel,
        int32_t mv_y_qpel,
        uint8_t* dst,
        size_t dst_stride,
        uint32_t w,
        uint32_t h
    ) noexcept;

    /// Interpolates a chroma block with 1/8-pel subpixel motion vectors:
    /// `mv_x_epel`, `mv_y_epel` are in eighth-pel units (1 unit = 1/8 pixel).
    static void interpolate_chroma(
        const uint8_t* ref,
        size_t ref_stride,
        int32_t ref_width,
        int32_t ref_height,
        int32_t src_x,
        int32_t src_y,
        int32_t mv_x_epel,
        int32_t mv_y_epel,
        uint8_t* dst,
        size_t dst_stride,
        uint32_t w,
        uint32_t h
    ) noexcept;
};

} // namespace atum

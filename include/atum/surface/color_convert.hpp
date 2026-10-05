#pragma once

#include <cstdint>
#include <cstddef>
#include "atum/surface/video_frame.hpp"
#include "atum/color/hdr_metadata.hpp"
#include "atum/color/transfer_function.hpp"
#include "atum/color/color_gamut.hpp"
#include "atum/color/tone_mapper.hpp"

namespace atum {

/// High-precision, zero-allocation color space, planar, and HDR/WCG conversion engine.
class ColorConverter {
public:
    // =========================================================================
    // 8-bit Legacy & Standard Conversions
    // =========================================================================

    /// Converts a YUV420P planar surface into a 32-bit RGBA packed surface.
    static void yuv420p_to_rgba(
        const uint8_t* y_plane, size_t y_stride,
        const uint8_t* u_plane, size_t u_stride,
        const uint8_t* v_plane, size_t v_stride,
        uint8_t* dst_rgba, size_t dst_stride,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT709,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    /// Converts a YUV422P planar surface into a 32-bit RGBA packed surface.
    static void yuv422p_to_rgba(
        const uint8_t* y_plane, size_t y_stride,
        const uint8_t* u_plane, size_t u_stride,
        const uint8_t* v_plane, size_t v_stride,
        uint8_t* dst_rgba, size_t dst_stride,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT709,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    /// Converts a YUV444P planar surface into a 32-bit RGBA packed surface.
    static void yuv444p_to_rgba(
        const uint8_t* y_plane, size_t y_stride,
        const uint8_t* u_plane, size_t u_stride,
        const uint8_t* v_plane, size_t v_stride,
        uint8_t* dst_rgba, size_t dst_stride,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT709,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    /// Converts a semi-planar NV12 surface (Y plane + interleaved UV) into packed RGBA.
    static void nv12_to_rgba(
        const uint8_t* y_plane, size_t y_stride,
        const uint8_t* uv_plane, size_t uv_stride,
        uint8_t* dst_rgba, size_t dst_stride,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT709,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    /// Encodes a packed 32-bit RGBA surface into planar YUV420P.
    static void rgba_to_yuv420p(
        const uint8_t* src_rgba, size_t src_stride,
        uint8_t* y_plane, size_t y_stride,
        uint8_t* u_plane, size_t u_stride,
        uint8_t* v_plane, size_t v_stride,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT709,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    // =========================================================================
    // 10-bit High Bit-Depth Conversions
    // =========================================================================

    /// Converts a planar 10-bit YUV420P10 surface into packed 32-bit RGBA.
    static void yuv420p10_to_rgba(
        const uint16_t* y_plane, size_t y_stride_bytes,
        const uint16_t* u_plane, size_t u_stride_bytes,
        const uint16_t* v_plane, size_t v_stride_bytes,
        uint8_t* dst_rgba, size_t dst_stride_bytes,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT2020,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    /// Converts an MSB-aligned semi-planar P010 surface (Y + interleaved UV) into packed RGBA.
    static void p010_to_rgba(
        const uint16_t* y_plane, size_t y_stride_bytes,
        const uint16_t* uv_plane, size_t uv_stride_bytes,
        uint8_t* dst_rgba, size_t dst_stride_bytes,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT2020,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    /// Converts a planar 10-bit YUV422P10 surface into packed 32-bit RGBA.
    static void yuv422p10_to_rgba(
        const uint16_t* y_plane, size_t y_stride_bytes,
        const uint16_t* u_plane, size_t u_stride_bytes,
        const uint16_t* v_plane, size_t v_stride_bytes,
        uint8_t* dst_rgba, size_t dst_stride_bytes,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT2020,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    /// Converts a planar 10-bit YUV444P10 surface into packed 32-bit RGBA.
    static void yuv444p10_to_rgba(
        const uint16_t* y_plane, size_t y_stride_bytes,
        const uint16_t* u_plane, size_t u_stride_bytes,
        const uint16_t* v_plane, size_t v_stride_bytes,
        uint8_t* dst_rgba, size_t dst_stride_bytes,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT2020,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    // =========================================================================
    // 12-bit Cinematic Master Conversions
    // =========================================================================

    /// Converts a planar 12-bit YUV420P12 surface into packed 32-bit RGBA.
    static void yuv420p12_to_rgba(
        const uint16_t* y_plane, size_t y_stride_bytes,
        const uint16_t* u_plane, size_t u_stride_bytes,
        const uint16_t* v_plane, size_t v_stride_bytes,
        uint8_t* dst_rgba, size_t dst_stride_bytes,
        uint32_t width, uint32_t height,
        ColorSpace space = ColorSpace::BT2020,
        ColorRange range = ColorRange::Studio
    ) noexcept;

    // =========================================================================
    // Full Pipeline HDR-to-SDR Tone-Mapped Conversion
    // =========================================================================

    /// Converts an HDR VideoFrame (PQ/HLG with BT.2020 primaries) to SDR RGBA32
    /// with gamut mapping to BT.709/sRGB and selectable Tone Mapping Operator.
    static void convert_hdr_to_sdr_rgba(
        const VideoFrame& src,
        uint8_t* dst_rgba,
        size_t dst_stride_bytes,
        const ToneMapper& tone_mapper
    ) noexcept;

    /// Converts any supported source VideoFrame into target PixelFormat.
    static VideoFrame convert(const VideoFrame& src, PixelFormat target_fmt);
};

} // namespace atum

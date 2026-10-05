#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace atum {

/// Pixel layout and planar format descriptor.
enum class PixelFormat : uint16_t {
    Unknown = 0,
    YUV420P,    ///< Planar Y, U, V (4:2:0 subsampling, U and V are half width & half height)
    NV12,       ///< Semi-planar Y plane followed by interleaved UV plane (4:2:0)
    NV21,       ///< Semi-planar Y plane followed by interleaved VU plane (4:2:0)
    YUV422P,    ///< Planar Y, U, V (4:2:2 subsampling, U and V are half width, full height)
    YUYV422,    ///< Interleaved Y0 U0 Y1 V0 (4:2:2)
    YUV444P,    ///< Planar Y, U, V (4:4:4 full resolution)
    RGBA32,     ///< Packed 32-bit R, G, B, A (8 bits per channel)
    BGRA32,     ///< Packed 32-bit B, G, R, A (8 bits per channel)
    RGB24,      ///< Packed 24-bit R, G, B
    BGR24,      ///< Packed 24-bit B, G, R
    GRAY8,      ///< 8-bit single luminance channel
    // High Bit-Depth (10-bit, 12-bit, 16-bit) Formats
    P010,       ///< Semi-planar 10-bit Y plane followed by interleaved UV plane (MSB-aligned in uint16_t, 4:2:0)
    P012,       ///< Semi-planar 12-bit Y plane followed by interleaved UV plane (MSB-aligned in uint16_t, 4:2:0)
    YUV420P10,  ///< Planar 10-bit Y, U, V (uint16_t samples, 4:2:0 subsampling)
    YUV422P10,  ///< Planar 10-bit Y, U, V (uint16_t samples, 4:2:2 subsampling)
    YUV444P10,  ///< Planar 10-bit Y, U, V (uint16_t samples, 4:4:4 full resolution)
    YUV420P12,  ///< Planar 12-bit Y, U, V (uint16_t samples, 4:2:0 subsampling)
    YUV422P12,  ///< Planar 12-bit Y, U, V (uint16_t samples, 4:2:2 subsampling)
    YUV444P12,  ///< Planar 12-bit Y, U, V (uint16_t samples, 4:4:4 full resolution)
    RGBA64,     ///< Packed 64-bit R, G, B, A (16 bits per channel)
};

/// Color range standard.
enum class ColorRange : uint8_t {
    Studio = 0,  ///< Limited / Narrow range: Y in [16..235] (or [64..940] for 10-bit), Cb/Cr in [16..240] ([64..960])
    Full = 1,    ///< Full range: [0..255] (or [0..1023] for 10-bit, [0..4095] for 12-bit)
};

/// ITU-R color space standard.
enum class ColorSpace : uint8_t {
    BT601 = 0,   ///< ITU-R BT.601 (SDTV standard)
    BT709 = 1,   ///< ITU-R BT.709 (HDTV / Web standard)
    BT2020 = 2,  ///< ITU-R BT.2020 (UHDTV wide-gamut standard)
};

/// Chroma subsampling structure.
enum class ChromaSubsampling : uint8_t {
    Unknown = 0,
    CS_420 = 1,  ///< 4:2:0 subsampling (half width, half height)
    CS_422 = 2,  ///< 4:2:2 subsampling (half width, full height)
    CS_444 = 3,  ///< 4:4:4 full resolution chroma
    Monochrome = 4, ///< Single luma plane
};

constexpr std::string_view pixel_format_to_string(PixelFormat fmt) noexcept {
    switch (fmt) {
        case PixelFormat::YUV420P:   return "YUV420P";
        case PixelFormat::NV12:      return "NV12";
        case PixelFormat::NV21:      return "NV21";
        case PixelFormat::YUV422P:   return "YUV422P";
        case PixelFormat::YUYV422:   return "YUYV422";
        case PixelFormat::YUV444P:   return "YUV444P";
        case PixelFormat::RGBA32:    return "RGBA32";
        case PixelFormat::BGRA32:    return "BGRA32";
        case PixelFormat::RGB24:     return "RGB24";
        case PixelFormat::BGR24:     return "BGR24";
        case PixelFormat::GRAY8:     return "GRAY8";
        case PixelFormat::P010:      return "P010";
        case PixelFormat::P012:      return "P012";
        case PixelFormat::YUV420P10: return "YUV420P10";
        case PixelFormat::YUV422P10: return "YUV422P10";
        case PixelFormat::YUV444P10: return "YUV444P10";
        case PixelFormat::YUV420P12: return "YUV420P12";
        case PixelFormat::YUV422P12: return "YUV422P12";
        case PixelFormat::YUV444P12: return "YUV444P12";
        case PixelFormat::RGBA64:    return "RGBA64";
        default:                     return "Unknown";
    }
}

constexpr size_t pixel_format_plane_count(PixelFormat fmt) noexcept {
    switch (fmt) {
        case PixelFormat::YUV420P:
        case PixelFormat::YUV422P:
        case PixelFormat::YUV444P:
        case PixelFormat::YUV420P10:
        case PixelFormat::YUV422P10:
        case PixelFormat::YUV444P10:
        case PixelFormat::YUV420P12:
        case PixelFormat::YUV422P12:
        case PixelFormat::YUV444P12:
            return 3;
        case PixelFormat::NV12:
        case PixelFormat::NV21:
        case PixelFormat::P010:
        case PixelFormat::P012:
            return 2;
        case PixelFormat::YUYV422:
        case PixelFormat::RGBA32:
        case PixelFormat::BGRA32:
        case PixelFormat::RGB24:
        case PixelFormat::BGR24:
        case PixelFormat::GRAY8:
        case PixelFormat::RGBA64:
            return 1;
        default:
            return 0;
    }
}

constexpr uint32_t pixel_format_bit_depth(PixelFormat fmt) noexcept {
    switch (fmt) {
        case PixelFormat::P010:
        case PixelFormat::YUV420P10:
        case PixelFormat::YUV422P10:
        case PixelFormat::YUV444P10:
            return 10;
        case PixelFormat::P012:
        case PixelFormat::YUV420P12:
        case PixelFormat::YUV422P12:
        case PixelFormat::YUV444P12:
            return 12;
        case PixelFormat::RGBA64:
            return 16;
        default:
            return 8;
    }
}

constexpr size_t pixel_format_bytes_per_sample(PixelFormat fmt) noexcept {
    switch (fmt) {
        case PixelFormat::P010:
        case PixelFormat::P012:
        case PixelFormat::YUV420P10:
        case PixelFormat::YUV422P10:
        case PixelFormat::YUV444P10:
        case PixelFormat::YUV420P12:
        case PixelFormat::YUV422P12:
        case PixelFormat::YUV444P12:
        case PixelFormat::RGBA64:
            return 2;
        default:
            return 1;
    }
}

constexpr bool pixel_format_is_high_bit_depth(PixelFormat fmt) noexcept {
    return pixel_format_bit_depth(fmt) > 8;
}

} // namespace atum

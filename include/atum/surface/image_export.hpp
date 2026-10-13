#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <span>
#include "atum/surface/video_frame.hpp"
#include "atum/core/media_types.hpp"

namespace atum {

/// Sovereign Image Export Subsystem bridging Atum VideoFrames with the Nisaba Image Engine.
/// Supports zero-copy export for packed RGBA32 and automatic color conversion for YUV/NV12/RGB formats.
class ImageExporter {
public:
    /// Saves a VideoFrame to an image file (format determined by extension: .png, .jpg, .jpeg, .qoi, .webp, .bmp).
    /// If the frame is in planar YUV or semi-planar NV12, it is automatically converted to standard RGBA32.
    static MediaResult<void> save_image(
        const VideoFrame& frame,
        const std::string& path,
        int quality_or_compression = 90
    );

    /// Saves a VideoFrame specifically to a PNG file.
    static MediaResult<void> save_png(
        const VideoFrame& frame,
        const std::string& path,
        int compression_level = 6
    );

    /// Saves a VideoFrame specifically to a JPEG file.
    static MediaResult<void> save_jpeg(
        const VideoFrame& frame,
        const std::string& path,
        int quality = 90
    );

    /// Encodes a VideoFrame into in-memory PNG bytes.
    static MediaResult<std::vector<uint8_t>> encode_png(
        const VideoFrame& frame,
        int compression_level = 6
    );

    /// Saves raw RGBA pixels directly to an image file.
    static MediaResult<void> save_raw_rgba(
        const uint8_t* rgba_data,
        uint32_t width,
        uint32_t height,
        size_t stride_bytes,
        const std::string& path,
        int quality_or_compression = 90
    );
};

/// Free-function convenience wrappers
inline MediaResult<void> save_image(const VideoFrame& frame, const std::string& path, int quality_or_compression = 90) {
    return ImageExporter::save_image(frame, path, quality_or_compression);
}

inline MediaResult<void> save_png(const VideoFrame& frame, const std::string& path, int compression_level = 6) {
    return ImageExporter::save_png(frame, path, compression_level);
}

inline MediaResult<void> save_jpeg(const VideoFrame& frame, const std::string& path, int quality = 90) {
    return ImageExporter::save_jpeg(frame, path, quality);
}

inline MediaResult<std::vector<uint8_t>> encode_png(const VideoFrame& frame, int compression_level = 6) {
    return ImageExporter::encode_png(frame, compression_level);
}

} // namespace atum

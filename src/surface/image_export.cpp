#include "atum/surface/image_export.hpp"
#include "atum/surface/color_convert.hpp"
#include <nisaba/canvas/pixmap.hpp>
#include <nisaba/image/image_io.hpp>
#include <nisaba/image/png.hpp>
#include <nisaba/image/jpeg.hpp>
#include <vector>
#include <cstring>

namespace atum {

namespace {

struct PackedRgbaView {
    const uint8_t* data{nullptr};
    size_t size{0};
    std::vector<uint8_t> owned_buffer;
};

PackedRgbaView get_packed_rgba(const VideoFrame& frame) {
    PackedRgbaView view;
    if (!frame.is_valid() || frame.width() == 0 || frame.height() == 0) {
        return view;
    }

    if (frame.pixel_format() == PixelFormat::RGBA32) {
        size_t expected_stride = static_cast<size_t>(frame.width()) * 4;
        if (frame.stride(0) == expected_stride) {
            view.data = frame.plane(0);
            view.size = expected_stride * frame.height();
            return view;
        }

        // Row-padded RGBA: pack rows tightly
        view.owned_buffer.resize(expected_stride * frame.height());
        for (uint32_t y = 0; y < frame.height(); ++y) {
            const uint8_t* src_row = frame.plane(0) + y * frame.stride(0);
            uint8_t* dst_row = view.owned_buffer.data() + y * expected_stride;
            std::memcpy(dst_row, src_row, expected_stride);
        }
        view.data = view.owned_buffer.data();
        view.size = view.owned_buffer.size();
        return view;
    }

    // Convert non-RGBA format to RGBA32 using Atum ColorConverter
    VideoFrame rgba_frame = ColorConverter::convert(frame, PixelFormat::RGBA32);
    if (!rgba_frame.is_valid()) {
        return view;
    }

    size_t expected_stride = static_cast<size_t>(rgba_frame.width()) * 4;
    if (rgba_frame.stride(0) == expected_stride) {
        view.owned_buffer.assign(
            rgba_frame.plane(0),
            rgba_frame.plane(0) + expected_stride * rgba_frame.height()
        );
    } else {
        view.owned_buffer.resize(expected_stride * rgba_frame.height());
        for (uint32_t y = 0; y < rgba_frame.height(); ++y) {
            const uint8_t* src_row = rgba_frame.plane(0) + y * rgba_frame.stride(0);
            uint8_t* dst_row = view.owned_buffer.data() + y * expected_stride;
            std::memcpy(dst_row, src_row, expected_stride);
        }
    }
    view.data = view.owned_buffer.data();
    view.size = view.owned_buffer.size();
    return view;
}

} // namespace

MediaResult<void> ImageExporter::save_image(
    const VideoFrame& frame,
    const std::string& path,
    int quality_or_compression
) {
    if (!frame.is_valid()) {
        return MediaError::InvalidData;
    }

    auto packed = get_packed_rgba(frame);
    if (!packed.data || packed.size == 0) {
        return MediaError::UnsupportedFormat;
    }

    auto pixmap_ref = nisaba::PixmapRef::from_bytes(
        packed.data,
        packed.size,
        frame.width(),
        frame.height(),
        nisaba::PixelFormat::RGBA8888
    );

    if (!pixmap_ref) {
        return MediaError::AllocationFailed;
    }

    bool success = nisaba::image::save_image_file(*pixmap_ref, path, quality_or_compression);
    if (!success) {
        return MediaError::IoError;
    }

    return MediaResult<void>::ok();
}

MediaResult<void> ImageExporter::save_png(
    const VideoFrame& frame,
    const std::string& path,
    int compression_level
) {
    if (!frame.is_valid()) {
        return MediaError::InvalidData;
    }

    auto packed = get_packed_rgba(frame);
    if (!packed.data || packed.size == 0) {
        return MediaError::UnsupportedFormat;
    }

    auto pixmap_ref = nisaba::PixmapRef::from_bytes(
        packed.data,
        packed.size,
        frame.width(),
        frame.height(),
        nisaba::PixelFormat::RGBA8888
    );

    if (!pixmap_ref) {
        return MediaError::AllocationFailed;
    }

    bool success = nisaba::image::save_png_file(*pixmap_ref, path, compression_level);
    if (!success) {
        return MediaError::IoError;
    }

    return MediaResult<void>::ok();
}

MediaResult<void> ImageExporter::save_jpeg(
    const VideoFrame& frame,
    const std::string& path,
    int quality
) {
    if (!frame.is_valid()) {
        return MediaError::InvalidData;
    }

    auto packed = get_packed_rgba(frame);
    if (!packed.data || packed.size == 0) {
        return MediaError::UnsupportedFormat;
    }

    auto pixmap_ref = nisaba::PixmapRef::from_bytes(
        packed.data,
        packed.size,
        frame.width(),
        frame.height(),
        nisaba::PixelFormat::RGBA8888
    );

    if (!pixmap_ref) {
        return MediaError::AllocationFailed;
    }

    bool success = nisaba::image::save_jpeg_file(*pixmap_ref, path, quality);
    if (!success) {
        return MediaError::IoError;
    }

    return MediaResult<void>::ok();
}

MediaResult<std::vector<uint8_t>> ImageExporter::encode_png(
    const VideoFrame& frame,
    int compression_level
) {
    if (!frame.is_valid()) {
        return MediaError::InvalidData;
    }

    auto packed = get_packed_rgba(frame);
    if (!packed.data || packed.size == 0) {
        return MediaError::UnsupportedFormat;
    }

    auto pixmap_ref = nisaba::PixmapRef::from_bytes(
        packed.data,
        packed.size,
        frame.width(),
        frame.height(),
        nisaba::PixelFormat::RGBA8888
    );

    if (!pixmap_ref) {
        return MediaError::AllocationFailed;
    }

    auto encoded_res = nisaba::image::encode_png(*pixmap_ref, compression_level);
    if (!encoded_res.has_value()) {
        return MediaError::InvalidData;
    }

    return std::move(*encoded_res);
}

MediaResult<void> ImageExporter::save_raw_rgba(
    const uint8_t* rgba_data,
    uint32_t width,
    uint32_t height,
    size_t stride_bytes,
    const std::string& path,
    int quality_or_compression
) {
    if (!rgba_data || width == 0 || height == 0) {
        return MediaError::InvalidData;
    }

    size_t expected_stride = static_cast<size_t>(width) * 4;
    const uint8_t* eff_data = rgba_data;
    std::vector<uint8_t> contiguous;

    if (stride_bytes != expected_stride && stride_bytes != 0) {
        contiguous.resize(expected_stride * height);
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(contiguous.data() + y * expected_stride, rgba_data + y * stride_bytes, expected_stride);
        }
        eff_data = contiguous.data();
    }

    auto pixmap_ref = nisaba::PixmapRef::from_bytes(
        eff_data,
        expected_stride * height,
        width,
        height,
        nisaba::PixelFormat::RGBA8888
    );

    if (!pixmap_ref) {
        return MediaError::AllocationFailed;
    }

    bool success = nisaba::image::save_image_file(*pixmap_ref, path, quality_or_compression);
    if (!success) {
        return MediaError::IoError;
    }

    return MediaResult<void>::ok();
}

} // namespace atum

#pragma once

// Atum Media Subsystem (atum-engine)
// Sovereign, zero-dependency multimedia processing engine for vaxp.
// Standalone C++20 media architecture & native Nisaba 2D Graphics Engine integration.

#include "atum/core/media_types.hpp"
#include "atum/core/packet.hpp"
#include "atum/core/frame.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/bitstream/bit_writer.hpp"
#include "atum/bitstream/nal_unit.hpp"
#include "atum/io/io_stream.hpp"
#include "atum/io/memory_stream.hpp"
#include "atum/io/file_stream.hpp"
#include "atum/surface/pixel_format.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/surface/color_convert.hpp"
#include "atum/audio/audio_types.hpp"
#include "atum/audio/audio_frame.hpp"
#include "atum/audio/audio_resampler.hpp"
#include "atum/codec/codec_id.hpp"
#include "atum/codec/codec_parameters.hpp"
#include "atum/codec/codec_registry.hpp"
#include "atum/decode/decoder.hpp"
#include "atum/decode/pcm_decoder.hpp"
#include "atum/decode/qoi_video_decoder.hpp"
#include "atum/encode/encoder.hpp"
#include "atum/encode/pcm_encoder.hpp"
#include "atum/encode/qoi_video_encoder.hpp"
#include "atum/container/container.hpp"
#include "atum/container/wav_container.hpp"
#include "atum/container/ivf_container.hpp"
#include "atum/container/isobmff_demuxer.hpp"
#include "atum/hwaccel/hwaccel.hpp"
#include "atum/parallel/thread_pool.hpp"
#include "atum/parallel/hevc_wpp.hpp"
#include "atum/parallel/av1_tile_grid.hpp"
#include "atum/parallel/frame_graph.hpp"

// --- Nisaba 2D Engine Integration Bridge ---
#if __has_include("nisaba/canvas/pixmap.hpp") && __has_include("nisaba/canvas/canvas.hpp")
#include "nisaba/canvas/pixmap.hpp"
#include "nisaba/canvas/canvas.hpp"

namespace atum {

/// Converts a VideoFrame directly into a Nisaba Pixmap surface.
inline std::optional<nisaba::Pixmap> to_pixmap(const VideoFrame& frame) {
    if (!frame.is_valid()) return std::nullopt;

    auto pixmap = nisaba::Pixmap::allocate(frame.width(), frame.height());
    if (!pixmap) return std::nullopt;

    auto mut = pixmap->as_mut();

    if (frame.pixel_format() == PixelFormat::RGBA32) {
        for (uint32_t y = 0; y < frame.height(); ++y) {
            const uint8_t* src_row = frame.plane(0) + y * frame.stride(0);
            uint8_t* dst_row = reinterpret_cast<uint8_t*>(mut.row(y));
            std::memcpy(dst_row, src_row, frame.width() * 4);
        }
    } else if (frame.pixel_format() == PixelFormat::YUV420P) {
        ColorConverter::yuv420p_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            frame.plane(2), frame.stride(2),
            mut.data_mut(), mut.stride_bytes(),
            frame.width(), frame.height(),
            frame.color_space(), frame.color_range()
        );
    } else if (frame.pixel_format() == PixelFormat::NV12) {
        ColorConverter::nv12_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            mut.data_mut(), mut.stride_bytes(),
            frame.width(), frame.height(),
            frame.color_space(), frame.color_range()
        );
    }

    return pixmap;
}

/// Directly blits a VideoFrame into an existing Nisaba mutable Pixmap surface without heap allocation.
inline bool blit_to_pixmap(const VideoFrame& frame, nisaba::PixmapMut& target) {
    if (!frame.is_valid() || target.width() < frame.width() || target.height() < frame.height()) {
        return false;
    }

    if (frame.pixel_format() == PixelFormat::RGBA32) {
        for (uint32_t y = 0; y < frame.height(); ++y) {
            const uint8_t* src_row = frame.plane(0) + y * frame.stride(0);
            uint8_t* dst_row = reinterpret_cast<uint8_t*>(target.row(y));
            std::memcpy(dst_row, src_row, frame.width() * 4);
        }
    } else if (frame.pixel_format() == PixelFormat::YUV420P) {
        ColorConverter::yuv420p_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            frame.plane(2), frame.stride(2),
            target.data_mut(), target.stride_bytes(),
            frame.width(), frame.height(),
            frame.color_space(), frame.color_range()
        );
    } else if (frame.pixel_format() == PixelFormat::NV12) {
        ColorConverter::nv12_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            target.data_mut(), target.stride_bytes(),
            frame.width(), frame.height(),
            frame.color_space(), frame.color_range()
        );
    }
    return true;
}

/// Renders a decoded VideoFrame directly onto a Nisaba Canvas at the given coordinates.
inline void draw_video_frame(
    nisaba::Canvas& canvas,
    int32_t x,
    int32_t y,
    const VideoFrame& frame,
    const nisaba::PixmapPaint& paint = nisaba::PixmapPaint()
) {
    auto pixmap = to_pixmap(frame);
    if (pixmap.has_value()) {
        canvas.draw_pixmap(x, y, pixmap->as_ref(), paint);
    }
}

} // namespace atum
#endif

#include "atum/surface/video_frame.hpp"
#include <cstring>
#include <algorithm>

namespace atum {

static inline size_t align32(size_t val) noexcept {
    return (val + 31) & ~static_cast<size_t>(31);
}

VideoFrame::VideoFrame() noexcept
    : Frame(MediaType::Video) {}

VideoFrame::VideoFrame(const VideoFrame& other)
    : Frame(MediaType::Video)
    , width_(other.width_)
    , height_(other.height_)
    , format_(other.format_)
    , range_(other.range_)
    , space_(other.space_)
    , strides_(other.strides_)
    , is_owned_(other.is_owned_) {
    pts_ = other.pts_;
    dts_ = other.dts_;
    duration_ = other.duration_;
    timebase_ = other.timebase_;
    is_keyframe_ = other.is_keyframe_;
    is_hw_frame_ = other.is_hw_frame_;
    hw_surface_id_ = other.hw_surface_id_;
    dma_buf_fd_ = other.dma_buf_fd_;

    if (other.is_owned_) {
        owned_buffer_ = other.owned_buffer_;
        // Re-point plane pointers relative to new owned_buffer_
        const uint8_t* old_base = other.owned_buffer_.data();
        uint8_t* new_base = owned_buffer_.data();
        for (size_t i = 0; i < MAX_PLANES; ++i) {
            if (other.planes_[i]) {
                size_t offset = other.planes_[i] - old_base;
                planes_[i] = new_base + offset;
            } else {
                planes_[i] = nullptr;
            }
        }
    } else {
        planes_ = other.planes_;
    }
}

VideoFrame& VideoFrame::operator=(const VideoFrame& other) {
    if (this == &other) return *this;

    width_ = other.width_;
    height_ = other.height_;
    format_ = other.format_;
    range_ = other.range_;
    space_ = other.space_;
    hdr_meta_ = other.hdr_meta_;
    strides_ = other.strides_;
    is_owned_ = other.is_owned_;

    pts_ = other.pts_;
    dts_ = other.dts_;
    duration_ = other.duration_;
    timebase_ = other.timebase_;
    is_keyframe_ = other.is_keyframe_;
    is_hw_frame_ = other.is_hw_frame_;
    hw_surface_id_ = other.hw_surface_id_;
    dma_buf_fd_ = other.dma_buf_fd_;

    if (other.is_owned_) {
        owned_buffer_ = other.owned_buffer_;
        const uint8_t* old_base = other.owned_buffer_.data();
        uint8_t* new_base = owned_buffer_.data();
        for (size_t i = 0; i < MAX_PLANES; ++i) {
            if (other.planes_[i]) {
                size_t offset = other.planes_[i] - old_base;
                planes_[i] = new_base + offset;
            } else {
                planes_[i] = nullptr;
            }
        }
    } else {
        owned_buffer_.clear();
        planes_ = other.planes_;
    }
    return *this;
}

VideoFrame VideoFrame::allocate(uint32_t width, uint32_t height, PixelFormat format) {
    VideoFrame frame;
    frame.width_ = width;
    frame.height_ = height;
    frame.format_ = format;

    size_t total_size = 0;
    size_t num_planes = pixel_format_plane_count(format);

    std::array<size_t, MAX_PLANES> plane_sizes{};
    std::array<size_t, MAX_PLANES> plane_offsets{};

    switch (format) {
        case PixelFormat::YUV420P: {
            frame.strides_[0] = align32(width);
            frame.strides_[1] = align32((width + 1) / 2);
            frame.strides_[2] = align32((width + 1) / 2);

            plane_sizes[0] = frame.strides_[0] * height;
            plane_sizes[1] = frame.strides_[1] * ((height + 1) / 2);
            plane_sizes[2] = frame.strides_[2] * ((height + 1) / 2);

            plane_offsets[0] = 0;
            plane_offsets[1] = align32(plane_sizes[0]);
            plane_offsets[2] = plane_offsets[1] + align32(plane_sizes[1]);
            total_size = plane_offsets[2] + align32(plane_sizes[2]);
            break;
        }
        case PixelFormat::YUV420P10:
        case PixelFormat::YUV420P12: {
            frame.strides_[0] = align32(width * 2);
            frame.strides_[1] = align32(((width + 1) / 2) * 2);
            frame.strides_[2] = align32(((width + 1) / 2) * 2);

            plane_sizes[0] = frame.strides_[0] * height;
            plane_sizes[1] = frame.strides_[1] * ((height + 1) / 2);
            plane_sizes[2] = frame.strides_[2] * ((height + 1) / 2);

            plane_offsets[0] = 0;
            plane_offsets[1] = align32(plane_sizes[0]);
            plane_offsets[2] = plane_offsets[1] + align32(plane_sizes[1]);
            total_size = plane_offsets[2] + align32(plane_sizes[2]);
            break;
        }
        case PixelFormat::NV12:
        case PixelFormat::NV21: {
            frame.strides_[0] = align32(width);
            frame.strides_[1] = align32(width); // UV interleaved

            plane_sizes[0] = frame.strides_[0] * height;
            plane_sizes[1] = frame.strides_[1] * ((height + 1) / 2);

            plane_offsets[0] = 0;
            plane_offsets[1] = align32(plane_sizes[0]);
            total_size = plane_offsets[1] + align32(plane_sizes[1]);
            break;
        }
        case PixelFormat::P010:
        case PixelFormat::P012: {
            frame.strides_[0] = align32(width * 2);
            frame.strides_[1] = align32(width * 2); // UV interleaved 16-bit

            plane_sizes[0] = frame.strides_[0] * height;
            plane_sizes[1] = frame.strides_[1] * ((height + 1) / 2);

            plane_offsets[0] = 0;
            plane_offsets[1] = align32(plane_sizes[0]);
            total_size = plane_offsets[1] + align32(plane_sizes[1]);
            break;
        }
        case PixelFormat::YUV422P: {
            frame.strides_[0] = align32(width);
            frame.strides_[1] = align32((width + 1) / 2);
            frame.strides_[2] = align32((width + 1) / 2);

            plane_sizes[0] = frame.strides_[0] * height;
            plane_sizes[1] = frame.strides_[1] * height; // 4:2:2 full height
            plane_sizes[2] = frame.strides_[2] * height;

            plane_offsets[0] = 0;
            plane_offsets[1] = align32(plane_sizes[0]);
            plane_offsets[2] = plane_offsets[1] + align32(plane_sizes[1]);
            total_size = plane_offsets[2] + align32(plane_sizes[2]);
            break;
        }
        case PixelFormat::YUV422P10:
        case PixelFormat::YUV422P12: {
            frame.strides_[0] = align32(width * 2);
            frame.strides_[1] = align32(((width + 1) / 2) * 2);
            frame.strides_[2] = align32(((width + 1) / 2) * 2);

            plane_sizes[0] = frame.strides_[0] * height;
            plane_sizes[1] = frame.strides_[1] * height;
            plane_sizes[2] = frame.strides_[2] * height;

            plane_offsets[0] = 0;
            plane_offsets[1] = align32(plane_sizes[0]);
            plane_offsets[2] = plane_offsets[1] + align32(plane_sizes[1]);
            total_size = plane_offsets[2] + align32(plane_sizes[2]);
            break;
        }
        case PixelFormat::YUV444P: {
            frame.strides_[0] = align32(width);
            frame.strides_[1] = align32(width);
            frame.strides_[2] = align32(width);

            plane_sizes[0] = frame.strides_[0] * height;
            plane_sizes[1] = frame.strides_[1] * height;
            plane_sizes[2] = frame.strides_[2] * height;

            plane_offsets[0] = 0;
            plane_offsets[1] = align32(plane_sizes[0]);
            plane_offsets[2] = plane_offsets[1] + align32(plane_sizes[1]);
            total_size = plane_offsets[2] + align32(plane_sizes[2]);
            break;
        }
        case PixelFormat::YUV444P10:
        case PixelFormat::YUV444P12: {
            frame.strides_[0] = align32(width * 2);
            frame.strides_[1] = align32(width * 2);
            frame.strides_[2] = align32(width * 2);

            plane_sizes[0] = frame.strides_[0] * height;
            plane_sizes[1] = frame.strides_[1] * height;
            plane_sizes[2] = frame.strides_[2] * height;

            plane_offsets[0] = 0;
            plane_offsets[1] = align32(plane_sizes[0]);
            plane_offsets[2] = plane_offsets[1] + align32(plane_sizes[1]);
            total_size = plane_offsets[2] + align32(plane_sizes[2]);
            break;
        }
        case PixelFormat::RGBA32:
        case PixelFormat::BGRA32: {
            frame.strides_[0] = align32(width * 4);
            plane_sizes[0] = frame.strides_[0] * height;
            plane_offsets[0] = 0;
            total_size = align32(plane_sizes[0]);
            break;
        }
        case PixelFormat::RGBA64: {
            frame.strides_[0] = align32(width * 8);
            plane_sizes[0] = frame.strides_[0] * height;
            plane_offsets[0] = 0;
            total_size = align32(plane_sizes[0]);
            break;
        }
        case PixelFormat::RGB24:
        case PixelFormat::BGR24: {
            frame.strides_[0] = align32(width * 3);
            plane_sizes[0] = frame.strides_[0] * height;
            plane_offsets[0] = 0;
            total_size = align32(plane_sizes[0]);
            break;
        }
        case PixelFormat::GRAY8: {
            frame.strides_[0] = align32(width);
            plane_sizes[0] = frame.strides_[0] * height;
            plane_offsets[0] = 0;
            total_size = align32(plane_sizes[0]);
            break;
        }
        default:
            return frame;
    }

    // Allocate with 32-byte padding to guarantee 32-byte SIMD alignment for all planes
    frame.owned_buffer_.resize(total_size + 32, 0);
    frame.is_owned_ = true;

    const uintptr_t raw_ptr = reinterpret_cast<uintptr_t>(frame.owned_buffer_.data());
    const size_t align_pad = (32 - (raw_ptr % 32)) % 32;
    uint8_t* const base_aligned = frame.owned_buffer_.data() + align_pad;

    for (size_t i = 0; i < num_planes; ++i) {
        frame.planes_[i] = base_aligned + plane_offsets[i];
    }

    if (format == PixelFormat::YUV420P || format == PixelFormat::YUV422P || format == PixelFormat::YUV444P) {
        std::memset(frame.planes_[0], 16, plane_sizes[0]);
        if (num_planes > 1) std::memset(frame.planes_[1], 128, plane_sizes[1]);
        if (num_planes > 2) std::memset(frame.planes_[2], 128, plane_sizes[2]);
    } else if (format == PixelFormat::NV12 || format == PixelFormat::NV21) {
        std::memset(frame.planes_[0], 16, plane_sizes[0]);
        if (num_planes > 1) std::memset(frame.planes_[1], 128, plane_sizes[1]);
    }

    return frame;
}

VideoFrame VideoFrame::from_raw_planes(
    uint32_t width,
    uint32_t height,
    PixelFormat format,
    const std::array<uint8_t*, MAX_PLANES>& planes,
    const std::array<size_t, MAX_PLANES>& strides
) noexcept {
    VideoFrame frame;
    frame.width_ = width;
    frame.height_ = height;
    frame.format_ = format;
    frame.planes_ = planes;
    frame.strides_ = strides;
    frame.is_owned_ = false;
    return frame;
}

VideoFrame VideoFrame::from_raw_planes_const(
    uint32_t width,
    uint32_t height,
    PixelFormat format,
    const std::array<const uint8_t*, MAX_PLANES>& planes,
    const std::array<size_t, MAX_PLANES>& strides
) noexcept {
    VideoFrame frame;
    frame.width_ = width;
    frame.height_ = height;
    frame.format_ = format;
    for (size_t i = 0; i < MAX_PLANES; ++i) {
        frame.planes_[i] = const_cast<uint8_t*>(planes[i]);
    }
    frame.strides_ = strides;
    frame.is_owned_ = false;
    return frame;
}

uint32_t VideoFrame::plane_width(size_t index) const noexcept {
    if (index == 0) return width_;
    switch (format_) {
        case PixelFormat::YUV420P:
        case PixelFormat::YUV420P10:
        case PixelFormat::YUV420P12:
        case PixelFormat::YUV422P:
        case PixelFormat::YUV422P10:
        case PixelFormat::YUV422P12:
            return (width_ + 1) / 2;
        case PixelFormat::NV12:
        case PixelFormat::NV21:
        case PixelFormat::P010:
        case PixelFormat::P012:
            return width_; // UV pairs
        case PixelFormat::YUV444P:
        case PixelFormat::YUV444P10:
        case PixelFormat::YUV444P12:
            return width_;
        default:
            return width_;
    }
}

uint32_t VideoFrame::plane_height(size_t index) const noexcept {
    if (index == 0) return height_;
    switch (format_) {
        case PixelFormat::YUV420P:
        case PixelFormat::YUV420P10:
        case PixelFormat::YUV420P12:
        case PixelFormat::NV12:
        case PixelFormat::NV21:
        case PixelFormat::P010:
        case PixelFormat::P012:
            return (height_ + 1) / 2;
        case PixelFormat::YUV422P:
        case PixelFormat::YUV422P10:
        case PixelFormat::YUV422P12:
        case PixelFormat::YUV444P:
        case PixelFormat::YUV444P10:
        case PixelFormat::YUV444P12:
            return height_;
        default:
            return height_;
    }
}

void VideoFrame::ensure_owned() {
    if (is_owned_ || !is_valid()) return;
    auto cloned = clone();
    *this = std::move(cloned);
}

VideoFrame VideoFrame::clone() const {
    if (!is_valid()) return VideoFrame();
    auto new_frame = allocate(width_, height_, format_);
    new_frame.set_color_range(range_);
    new_frame.set_color_space(space_);
    new_frame.set_hdr_metadata(hdr_meta_);
    new_frame.set_pts(pts_);
    new_frame.set_dts(dts_);
    new_frame.set_duration(duration_);
    new_frame.set_timebase(timebase_);
    new_frame.set_keyframe(is_keyframe_);
    new_frame.set_hw_frame(is_hw_frame_);
    new_frame.set_hw_surface_id(hw_surface_id_);
    new_frame.set_dma_buf_fd(dma_buf_fd_);

    size_t num_planes = plane_count();
    size_t bps = bytes_per_sample();

    for (size_t p = 0; p < num_planes; ++p) {
        uint32_t h = plane_height(p);
        size_t copy_bytes = plane_width(p) * bps;
        if (format_ == PixelFormat::RGBA32 || format_ == PixelFormat::BGRA32) copy_bytes = width_ * 4;
        else if (format_ == PixelFormat::RGBA64) copy_bytes = width_ * 8;
        else if (format_ == PixelFormat::RGB24 || format_ == PixelFormat::BGR24) copy_bytes = width_ * 3;

        for (uint32_t y = 0; y < h; ++y) {
            std::memcpy(
                new_frame.plane_mut(p) + y * new_frame.stride(p),
                plane(p) + y * stride(p),
                copy_bytes
            );
        }
    }
    return new_frame;
}

} // namespace atum

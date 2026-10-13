#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include <array>
#include <memory>
#include "atum/core/frame.hpp"
#include "atum/surface/pixel_format.hpp"
#include "atum/color/hdr_metadata.hpp"

namespace atum {

/// Decoded uncompressed multi-planar video frame.
class VideoFrame : public Frame {
public:
    static constexpr size_t MAX_PLANES = 4;

    VideoFrame() noexcept;
    ~VideoFrame() override = default;

    VideoFrame(const VideoFrame& other);
    VideoFrame& operator=(const VideoFrame& other);

    VideoFrame(VideoFrame&& other) noexcept = default;
    VideoFrame& operator=(VideoFrame&& other) noexcept = default;

    /// Allocates an owned video surface with 32-byte SIMD row alignment.
    static VideoFrame allocate(uint32_t width, uint32_t height, PixelFormat format);

    /// Wraps external hardware/memory planes without copying (borrowed zero-copy surface).
    static VideoFrame from_raw_planes(
        uint32_t width,
        uint32_t height,
        PixelFormat format,
        const std::array<uint8_t*, MAX_PLANES>& planes,
        const std::array<size_t, MAX_PLANES>& strides
    ) noexcept;

    /// Read-only external plane wrapper.
    static VideoFrame from_raw_planes_const(
        uint32_t width,
        uint32_t height,
        PixelFormat format,
        const std::array<const uint8_t*, MAX_PLANES>& planes,
        const std::array<size_t, MAX_PLANES>& strides
    ) noexcept;

    [[nodiscard]] bool is_valid() const noexcept override {
        return width_ > 0 && height_ > 0 && format_ != PixelFormat::Unknown && planes_[0] != nullptr;
    }

    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] PixelFormat pixel_format() const noexcept { return format_; }
    [[nodiscard]] uint32_t bit_depth() const noexcept { return pixel_format_bit_depth(format_); }
    [[nodiscard]] size_t bytes_per_sample() const noexcept { return pixel_format_bytes_per_sample(format_); }
    [[nodiscard]] bool is_high_bit_depth() const noexcept { return pixel_format_is_high_bit_depth(format_); }

    [[nodiscard]] ColorRange color_range() const noexcept { return range_; }
    void set_color_range(ColorRange r) noexcept { range_ = r; }

    [[nodiscard]] ColorSpace color_space() const noexcept { return space_; }
    void set_color_space(ColorSpace s) noexcept { space_ = s; }

    [[nodiscard]] const HdrMetadata& hdr_metadata() const noexcept { return hdr_meta_; }
    void set_hdr_metadata(const HdrMetadata& meta) noexcept { hdr_meta_ = meta; }

    [[nodiscard]] size_t plane_count() const noexcept {
        return pixel_format_plane_count(format_);
    }

    [[nodiscard]] const uint8_t* plane(size_t index) const noexcept {
        return index < MAX_PLANES ? planes_[index] : nullptr;
    }

    [[nodiscard]] uint8_t* plane_mut(size_t index) noexcept {
        ensure_owned();
        return index < MAX_PLANES ? planes_[index] : nullptr;
    }

    /// Access typed 16-bit plane pointer for 10-bit and 12-bit formats.
    [[nodiscard]] const uint16_t* plane16(size_t index) const noexcept {
        return reinterpret_cast<const uint16_t*>(plane(index));
    }

    [[nodiscard]] uint16_t* plane16_mut(size_t index) noexcept {
        return reinterpret_cast<uint16_t*>(plane_mut(index));
    }

    [[nodiscard]] size_t stride(size_t index) const noexcept {
        return index < MAX_PLANES ? strides_[index] : 0;
    }

    [[nodiscard]] uint32_t plane_width(size_t index) const noexcept;
    [[nodiscard]] uint32_t plane_height(size_t index) const noexcept;

    void ensure_owned();
    [[nodiscard]] VideoFrame clone() const;

    // --- Hardware Acceleration & Zero-Copy Properties ---
    [[nodiscard]] bool is_hw_frame() const noexcept { return is_hw_frame_; }
    void set_hw_frame(bool hw) noexcept { is_hw_frame_ = hw; }

    [[nodiscard]] uint32_t hw_surface_id() const noexcept { return hw_surface_id_; }
    void set_hw_surface_id(uint32_t id) noexcept { hw_surface_id_ = id; if (id != 0xFFFFFFFF) is_hw_frame_ = true; }

    [[nodiscard]] int32_t dma_buf_fd() const noexcept { return dma_buf_fd_; }
    void set_dma_buf_fd(int32_t fd) noexcept { dma_buf_fd_ = fd; if (fd >= 0) is_hw_frame_ = true; }

    [[nodiscard]] uint64_t hw_opaque_handle() const noexcept { return hw_opaque_handle_; }
    void set_hw_opaque_handle(uint64_t handle) noexcept { hw_opaque_handle_ = handle; if (handle != 0) is_hw_frame_ = true; }

private:
    uint32_t width_{0};
    uint32_t height_{0};
    PixelFormat format_{PixelFormat::Unknown};
    ColorRange range_{ColorRange::Studio};
    ColorSpace space_{ColorSpace::BT709};
    HdrMetadata hdr_meta_{};

    std::array<uint8_t*, MAX_PLANES> planes_{};
    std::array<size_t, MAX_PLANES> strides_{};

    std::vector<uint8_t> owned_buffer_;
    bool is_owned_{false};

    bool is_hw_frame_{false};
    uint32_t hw_surface_id_{0xFFFFFFFF};
    int32_t dma_buf_fd_{-1};
    uint64_t hw_opaque_handle_{0};
};

} // namespace atum

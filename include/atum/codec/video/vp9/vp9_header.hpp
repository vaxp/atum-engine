#pragma once

#include <cstdint>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/codec/video/vp9/vp9_types.hpp"

namespace atum {

/// Parsed VP9 Uncompressed Frame Header.
struct Vp9UncompressedHeader {
    uint8_t frame_marker{2}; // Must be 2 (0b10)
    Vp9Profile profile{Vp9Profile::Profile0};
    bool show_existing_frame{false};
    uint8_t frame_to_show_map_idx{0};

    Vp9FrameType frame_type{Vp9FrameType::KeyFrame};
    bool show_frame{true};
    bool error_resilient_mode{false};
    bool intra_only{false};

    uint8_t bit_depth{8};
    Vp9ColorSpace color_space{Vp9ColorSpace::BT709};
    bool color_range_full{false};
    uint8_t subsampling_x{1};
    uint8_t subsampling_y{1};

    uint32_t width{0};
    uint32_t height{0};
    uint32_t render_width{0};
    uint32_t render_height{0};

    uint8_t refresh_frame_flags{0};
    uint8_t filter_level{0};
    uint8_t sharpness_level{0};
    uint8_t base_q_idx{0};

    [[nodiscard]] bool is_keyframe() const noexcept {
        return frame_type == Vp9FrameType::KeyFrame || intra_only;
    }

    /// Parses a VP9 uncompressed header from the bitstream.
    static MediaResult<Vp9UncompressedHeader> parse(BitReader& reader) noexcept;

    /// Parses directly from a raw frame byte span.
    static MediaResult<Vp9UncompressedHeader> parse_frame(std::span<const uint8_t> frame_bytes) noexcept;
};

} // namespace atum

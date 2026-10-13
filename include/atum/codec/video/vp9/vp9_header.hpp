#pragma once

#include <cstdint>
#include <array>
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

    uint8_t uncompressed_header_size{0};
    uint16_t compressed_header_size{0};

    std::array<uint8_t, 3> ref_frame_indices{0, 0, 0};
    std::array<uint8_t, 3> ref_frame_sign_bias{0, 0, 0};

    uint8_t tile_cols_log2{0};
    uint8_t tile_rows_log2{0};

    bool allow_high_precision_mv{false};
    uint8_t mcomp_filter_type{0}; // 0=eighttap, 1=smooth, 2=sharp, 3=switchable
    bool frame_parallel_decoding_mode{true};
    uint8_t reset_frame_context{0};
    bool refresh_frame_context{true};
    uint8_t frame_context_idx{0};

    bool segmentation_enabled{false};
    bool segmentation_temporal_update{false};
    bool segmentation_update_map{false};
    std::array<uint8_t, 7> mb_segment_tree_probs{0};
    std::array<uint8_t, 3> segment_pred_probs{0};

    [[nodiscard]] bool is_keyframe() const noexcept {
        return frame_type == Vp9FrameType::KeyFrame || intra_only;
    }

    /// Parses a VP9 uncompressed header from the bitstream.
    static MediaResult<Vp9UncompressedHeader> parse(BitReader& reader, uint32_t ref_w = 0, uint32_t ref_h = 0) noexcept;

    /// Parses directly from a raw frame byte span.
    static MediaResult<Vp9UncompressedHeader> parse_frame(std::span<const uint8_t> frame_bytes, uint32_t ref_w = 0, uint32_t ref_h = 0) noexcept;
};

} // namespace atum

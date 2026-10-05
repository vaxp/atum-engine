#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/codec/video/av1/av1_types.hpp"

namespace atum {

/// AV1 Open Bitstream Unit (OBU) Header.
struct Av1ObuHeader {
    bool obu_forbidden_bit{false}; // Must be false
    Av1ObuType obu_type{Av1ObuType::SequenceHeader};
    bool obu_extension_flag{false};
    bool obu_has_size_field{false};
    uint8_t temporal_id{0};
    uint8_t spatial_id{0};

    /// Parses OBU header from raw bytes. Returns [header, bytes_consumed].
    static MediaResult<std::pair<Av1ObuHeader, size_t>> parse(std::span<const uint8_t> data) noexcept;
};

/// AV1 Color Configuration (Section 6.4.2).
struct Av1ColorConfig {
    bool high_bitdepth{false};
    bool twelve_bit{false};
    uint8_t bit_depth{8};
    bool mono_chrome{false};

    bool color_description_present_flag{false};
    uint8_t color_primaries{2};         // 2 = Unspecified
    uint8_t transfer_characteristics{2}; // 2 = Unspecified
    uint8_t matrix_coefficients{2};      // 2 = Unspecified
    bool color_range_full{false};

    uint8_t subsampling_x{1};
    uint8_t subsampling_y{1};
    uint8_t chroma_sample_position{0};
    bool separate_uv_delta_q{false};

    static MediaResult<Av1ColorConfig> parse(BitReader& reader, Av1Profile profile) noexcept;
};

/// AV1 Sequence Header OBU (Section 6.4.1).
struct Av1SequenceHeader {
    Av1Profile seq_profile{Av1Profile::Main};
    bool still_picture{false};
    bool reduced_still_picture_header{false};

    bool timing_info_present_flag{false};
    uint32_t num_units_in_display_tick{0};
    uint32_t time_scale{0};

    uint8_t frame_width_bits_minus_1{0};
    uint8_t frame_height_bits_minus_1{0};
    uint32_t max_frame_width{0};
    uint32_t max_frame_height{0};

    bool use_128x128_superblock{false};
    bool enable_cdef{false};
    bool enable_restoration{false};

    Av1ColorConfig color_config;
    bool film_grain_params_present{false};

    [[nodiscard]] uint32_t width() const noexcept { return max_frame_width; }
    [[nodiscard]] uint32_t height() const noexcept { return max_frame_height; }

    [[nodiscard]] Rational frame_rate() const noexcept {
        if (timing_info_present_flag && num_units_in_display_tick > 0) {
            return {static_cast<int32_t>(time_scale), static_cast<int32_t>(num_units_in_display_tick)};
        }
        return {0, 1};
    }

    static MediaResult<Av1SequenceHeader> parse(BitReader& reader) noexcept;
    static MediaResult<Av1SequenceHeader> parse_payload(std::span<const uint8_t> payload) noexcept;
};

/// AV1 Frame Header OBU (Section 6.8.0).
struct Av1FrameHeader {
    bool show_existing_frame{false};
    uint8_t frame_to_show_map_idx{0};

    Av1FrameType frame_type{Av1FrameType::KeyFrame};
    bool show_frame{true};
    bool showable_frame{false};
    bool error_resilient_mode{false};
    bool disable_cdf_update{false};

    uint32_t frame_width{0};
    uint32_t frame_height{0};
    uint32_t render_width{0};
    uint32_t render_height{0};

    [[nodiscard]] bool is_keyframe() const noexcept {
        return frame_type == Av1FrameType::KeyFrame || frame_type == Av1FrameType::IntraOnlyFrame;
    }

    static MediaResult<Av1FrameHeader> parse(BitReader& reader, const Av1SequenceHeader& seq) noexcept;
    static MediaResult<Av1FrameHeader> parse_payload(std::span<const uint8_t> payload, const Av1SequenceHeader& seq) noexcept;
};

} // namespace atum

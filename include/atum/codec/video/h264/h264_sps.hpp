#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/codec/video/h264/h264_types.hpp"

namespace atum {

/// H.264 VUI (Video Usability Information) Parameters.
struct H264VuiParameters {
    bool aspect_ratio_info_present_flag{false};
    uint8_t aspect_ratio_idc{0};
    uint16_t sar_width{0};
    uint16_t sar_height{0};

    bool overscan_info_present_flag{false};
    bool overscan_appropriate_flag{false};

    bool video_signal_type_present_flag{false};
    uint8_t video_format{5}; // 5 = Unspecified
    bool video_full_range_flag{false};
    bool colour_description_present_flag{false};
    uint8_t colour_primaries{2};         // 2 = Unspecified
    uint8_t transfer_characteristics{2}; // 2 = Unspecified
    uint8_t matrix_coefficients{2};      // 2 = Unspecified

    bool chroma_loc_info_present_flag{false};
    uint32_t chroma_sample_loc_type_top_field{0};
    uint32_t chroma_sample_loc_type_bottom_field{0};

    bool timing_info_present_flag{false};
    uint32_t num_units_in_tick{0};
    uint32_t time_scale{0};
    bool fixed_frame_rate_flag{false};
};

/// H.264 Sequence Parameter Set (SPS).
struct H264Sps {
    uint8_t profile_idc{0};
    bool constraint_set0_flag{false};
    bool constraint_set1_flag{false};
    bool constraint_set2_flag{false};
    bool constraint_set3_flag{false};
    bool constraint_set4_flag{false};
    bool constraint_set5_flag{false};
    uint8_t level_idc{0};
    uint32_t seq_parameter_set_id{0};

    // High profile extensions
    uint32_t chroma_format_idc{1}; // 1 = 4:2:0 default
    bool separate_colour_plane_flag{false};
    uint32_t bit_depth_luma_minus8{0};
    uint32_t bit_depth_chroma_minus8{0};
    bool qpprime_y_zero_transform_bypass_flag{false};
    bool seq_scaling_matrix_present_flag{false};

    uint32_t log2_max_frame_num_minus4{0};
    uint32_t pic_order_cnt_type{0};
    uint32_t log2_max_pic_order_cnt_lsb_minus4{0};
    bool delta_pic_order_always_zero_flag{false};
    int32_t offset_for_non_ref_pic{0};
    int32_t offset_for_top_to_bottom_field{0};
    uint32_t num_ref_frames_in_pic_order_cnt_cycle{0};
    std::vector<int32_t> offset_for_ref_frame;

    uint32_t max_num_ref_frames{0};
    bool gaps_in_frame_num_value_allowed_flag{false};

    uint32_t pic_width_in_mbs_minus1{0};
    uint32_t pic_height_in_map_units_minus1{0};
    bool frame_mbs_only_flag{true};
    bool mb_adaptive_frame_field_flag{false};
    bool direct_8x8_inference_flag{false};

    bool frame_cropping_flag{false};
    uint32_t frame_crop_left_offset{0};
    uint32_t frame_crop_right_offset{0};
    uint32_t frame_crop_top_offset{0};
    uint32_t frame_crop_bottom_offset{0};

    bool vui_parameters_present_flag{false};
    H264VuiParameters vui;

    [[nodiscard]] uint32_t raw_width() const noexcept {
        return (pic_width_in_mbs_minus1 + 1) * 16;
    }

    [[nodiscard]] uint32_t raw_height() const noexcept {
        return (pic_height_in_map_units_minus1 + 1) * 16 * (frame_mbs_only_flag ? 1 : 2);
    }

    [[nodiscard]] uint32_t width() const noexcept {
        uint32_t w = raw_width();
        if (!frame_cropping_flag) return w;
        uint32_t crop_x = (chroma_format_idc == 0) ? 1 : ((chroma_format_idc == 1 || chroma_format_idc == 2) ? 2 : 1);
        uint32_t crop_total = (frame_crop_left_offset + frame_crop_right_offset) * crop_x;
        return (crop_total < w) ? (w - crop_total) : w;
    }

    [[nodiscard]] uint32_t height() const noexcept {
        uint32_t h = raw_height();
        if (!frame_cropping_flag) return h;
        uint32_t crop_sub_y = (chroma_format_idc == 1) ? 2 : 1;
        uint32_t crop_y = crop_sub_y * (2 - (frame_mbs_only_flag ? 1 : 0));
        uint32_t crop_total = (frame_crop_top_offset + frame_crop_bottom_offset) * crop_y;
        return (crop_total < h) ? (h - crop_total) : h;
    }

    [[nodiscard]] Rational frame_rate() const noexcept {
        if (vui_parameters_present_flag && vui.timing_info_present_flag && vui.num_units_in_tick > 0) {
            return {static_cast<int32_t>(vui.time_scale), static_cast<int32_t>(vui.num_units_in_tick * 2)};
        }
        return {0, 1};
    }

    /// Parses an SPS from an RBSP bitstream reader.
    static MediaResult<H264Sps> parse(BitReader& reader) noexcept;

    /// Parses an SPS directly from RBSP byte span.
    static MediaResult<H264Sps> parse_rbsp(std::span<const uint8_t> rbsp_bytes) noexcept;
};

} // namespace atum

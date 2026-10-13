#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/codec/video/hevc/hevc_types.hpp"

namespace atum {

/// HEVC Profile, Tier, and Level.
struct HevcProfileTierLevel {
    uint8_t general_profile_space{0};
    bool general_tier_flag{false};
    uint8_t general_profile_idc{0};
    uint32_t general_profile_compatibility_flags{0};
    bool general_progressive_source_flag{false};
    bool general_interlaced_source_flag{false};
    bool general_non_packed_constraint_flag{false};
    bool general_frame_only_constraint_flag{false};
    uint8_t general_level_idc{0};

    static MediaResult<HevcProfileTierLevel> parse(BitReader& reader, uint8_t max_sub_layers_minus1) noexcept;
};

/// HEVC Video Parameter Set (VPS).
struct HevcVps {
    uint8_t vps_video_parameter_set_id{0};
    bool vps_base_layer_internal_flag{true};
    bool vps_base_layer_available_flag{true};
    uint8_t vps_max_layers_minus1{0};
    uint8_t vps_max_sub_layers_minus1{0};
    bool vps_temporal_id_nesting_flag{false};
    HevcProfileTierLevel ptl;

    static MediaResult<HevcVps> parse(BitReader& reader) noexcept;
    static MediaResult<HevcVps> parse_rbsp(std::span<const uint8_t> rbsp) noexcept;
};

/// HEVC Conformance Cropping Window.
struct HevcConformanceWindow {
    uint32_t conf_win_left_offset{0};
    uint32_t conf_win_right_offset{0};
    uint32_t conf_win_top_offset{0};
    uint32_t conf_win_bottom_offset{0};
};

/// HEVC VUI Parameters.
struct HevcVui {
    bool aspect_ratio_info_present_flag{false};
    uint8_t aspect_ratio_idc{0};
    uint16_t sar_width{0};
    uint16_t sar_height{0};

    bool video_signal_type_present_flag{false};
    uint8_t video_format{5};
    bool video_full_range_flag{false};
    bool colour_description_present_flag{false};
    uint8_t colour_primaries{2};
    uint8_t transfer_characteristics{2};
    uint8_t matrix_coeffs{2};

    bool vui_timing_info_present_flag{false};
    uint32_t vui_num_units_in_tick{0};
    uint32_t vui_time_scale{0};
};

/// HEVC Sequence Parameter Set (SPS).
struct HevcSps {
    uint8_t sps_video_parameter_set_id{0};
    uint8_t sps_max_sub_layers_minus1{0};
    bool sps_temporal_id_nesting_flag{false};
    HevcProfileTierLevel ptl;
    uint32_t sps_seq_parameter_set_id{0};

    uint32_t chroma_format_idc{1}; // 1 = 4:2:0 default
    bool separate_colour_plane_flag{false};
    uint32_t pic_width_in_luma_samples{0};
    uint32_t pic_height_in_luma_samples{0};

    bool conformance_window_flag{false};
    HevcConformanceWindow conf_win;

    uint32_t bit_depth_luma_minus8{0};
    uint32_t bit_depth_chroma_minus8{0};
    uint32_t log2_max_pic_order_cnt_lsb_minus4{0};

    bool sps_sub_layer_ordering_info_present_flag{false};
    uint32_t log2_min_luma_coding_block_size_minus3{0};
    uint32_t log2_diff_max_min_luma_coding_block_size{0};
    uint32_t log2_min_transform_block_size_minus2{0};
    uint32_t log2_diff_max_min_transform_block_size{0};
    uint32_t max_transform_hierarchy_depth_inter{0};
    uint32_t max_transform_hierarchy_depth_intra{0};
    uint32_t sps_max_dec_pic_buffering_minus1{0};

    bool scaling_list_enabled_flag{false};
    bool amp_enabled_flag{false};
    bool sample_adaptive_offset_enabled_flag{false};
    bool pcm_enabled_flag{false};
    uint32_t num_short_term_ref_pic_sets{0};
    bool long_term_ref_pics_present_flag{false};
    uint32_t num_long_term_ref_pics_sps{0};
    bool sps_temporal_mvp_enabled_flag{false};
    bool strong_intra_smoothing_enabled_flag{false};

    bool vui_parameters_present_flag{false};
    HevcVui vui;

    [[nodiscard]] uint32_t raw_width() const noexcept {
        return pic_width_in_luma_samples;
    }

    [[nodiscard]] uint32_t raw_height() const noexcept {
        return pic_height_in_luma_samples;
    }

    [[nodiscard]] uint32_t width() const noexcept {
        if (!conformance_window_flag) return pic_width_in_luma_samples;
        uint32_t sub_w = (chroma_format_idc == 1 || chroma_format_idc == 2) ? 2 : 1;
        uint32_t crop = (conf_win.conf_win_left_offset + conf_win.conf_win_right_offset) * sub_w;
        return (crop < pic_width_in_luma_samples) ? (pic_width_in_luma_samples - crop) : pic_width_in_luma_samples;
    }

    [[nodiscard]] uint32_t height() const noexcept {
        if (!conformance_window_flag) return pic_height_in_luma_samples;
        uint32_t sub_h = (chroma_format_idc == 1) ? 2 : 1;
        uint32_t crop = (conf_win.conf_win_top_offset + conf_win.conf_win_bottom_offset) * sub_h;
        return (crop < pic_height_in_luma_samples) ? (pic_height_in_luma_samples - crop) : pic_height_in_luma_samples;
    }

    [[nodiscard]] Rational frame_rate() const noexcept {
        if (vui_parameters_present_flag && vui.vui_timing_info_present_flag && vui.vui_num_units_in_tick > 0) {
            return {static_cast<int32_t>(vui.vui_time_scale), static_cast<int32_t>(vui.vui_num_units_in_tick)};
        }
        return {0, 1};
    }

    static MediaResult<HevcSps> parse(BitReader& reader) noexcept;
    static MediaResult<HevcSps> parse_rbsp(std::span<const uint8_t> rbsp) noexcept;
};

/// HEVC Picture Parameter Set (PPS).
struct HevcPps {
    uint32_t pps_pic_parameter_set_id{0};
    uint32_t pps_seq_parameter_set_id{0};
    bool dependent_slice_segments_enabled_flag{false};
    bool output_flag_present_flag{false};
    uint8_t num_extra_slice_header_bits{0};
    bool sign_data_hiding_enabled_flag{false};
    bool cabac_init_present_flag{false};
    uint32_t num_ref_idx_l0_default_active_minus1{0};
    uint32_t num_ref_idx_l1_default_active_minus1{0};
    int32_t init_qp_minus26{0};
    bool constrained_intra_pred_flag{false};
    bool transform_skip_enabled_flag{false};
    bool cu_qp_delta_enabled_flag{false};
    uint32_t diff_cu_qp_delta_depth{0};
    int32_t pps_cb_qp_offset{0};
    int32_t pps_cr_qp_offset{0};
    bool pps_slice_chroma_qp_offsets_present_flag{false};
    bool weighted_pred_flag{false};
    bool weighted_bipred_flag{false};
    bool transquant_bypass_enabled_flag{false};
    bool tiles_enabled_flag{false};
    bool entropy_coding_sync_enabled_flag{false};

    uint32_t num_tile_columns_minus1{0};
    uint32_t num_tile_rows_minus1{0};
    bool uniform_spacing_flag{true};
    bool loop_filter_across_tiles_enabled_flag{true};
    bool pps_loop_filter_across_slices_enabled_flag{false};
    bool deblocking_filter_control_present_flag{false};
    bool deblocking_filter_override_enabled_flag{false};
    bool pps_deblocking_filter_disabled_flag{false};
    int32_t pps_beta_offset_div2{0};
    int32_t pps_tc_offset_div2{0};
    bool pps_scaling_list_data_present_flag{false};
    bool lists_modification_present_flag{false};
    uint32_t log2_parallel_merge_level_minus2{0};
    bool slice_segment_header_extension_present_flag{false};

    static MediaResult<HevcPps> parse(BitReader& reader) noexcept;
    static MediaResult<HevcPps> parse_rbsp(std::span<const uint8_t> rbsp) noexcept;
};

} // namespace atum

#include "atum/codec/video/h264/h264_sps.hpp"

namespace atum {

MediaResult<H264Sps> H264Sps::parse_rbsp(std::span<const uint8_t> rbsp_bytes) noexcept {
    BitReader reader(rbsp_bytes);
    return parse(reader);
}

MediaResult<H264Sps> H264Sps::parse(BitReader& reader) noexcept {
    H264Sps sps{};

    auto p_idc = reader.read_bits(8);
    if (!p_idc) return p_idc.error();
    sps.profile_idc = static_cast<uint8_t>(*p_idc);

    auto cs0 = reader.read_bit(); if (!cs0) return cs0.error(); sps.constraint_set0_flag = (*cs0 != 0);
    auto cs1 = reader.read_bit(); if (!cs1) return cs1.error(); sps.constraint_set1_flag = (*cs1 != 0);
    auto cs2 = reader.read_bit(); if (!cs2) return cs2.error(); sps.constraint_set2_flag = (*cs2 != 0);
    auto cs3 = reader.read_bit(); if (!cs3) return cs3.error(); sps.constraint_set3_flag = (*cs3 != 0);
    auto cs4 = reader.read_bit(); if (!cs4) return cs4.error(); sps.constraint_set4_flag = (*cs4 != 0);
    auto cs5 = reader.read_bit(); if (!cs5) return cs5.error(); sps.constraint_set5_flag = (*cs5 != 0);

    auto skip_reserved = reader.skip_bits(2);
    if (!skip_reserved) return skip_reserved.error();

    auto lvl = reader.read_bits(8);
    if (!lvl) return lvl.error();
    sps.level_idc = static_cast<uint8_t>(*lvl);

    auto sps_id = reader.read_ue();
    if (!sps_id) return sps_id.error();
    sps.seq_parameter_set_id = *sps_id;

    if (sps.profile_idc == 100 || sps.profile_idc == 110 || sps.profile_idc == 122 ||
        sps.profile_idc == 244 || sps.profile_idc == 44  || sps.profile_idc == 83  ||
        sps.profile_idc == 86  || sps.profile_idc == 118 || sps.profile_idc == 128 ||
        sps.profile_idc == 138 || sps.profile_idc == 139 || sps.profile_idc == 134 ||
        sps.profile_idc == 135) {

        auto chroma = reader.read_ue();
        if (!chroma) return chroma.error();
        sps.chroma_format_idc = *chroma;

        if (sps.chroma_format_idc == 3) {
            auto sep = reader.read_bit();
            if (!sep) return sep.error();
            sps.separate_colour_plane_flag = (*sep != 0);
        }

        auto bit_depth_luma = reader.read_ue();
        if (!bit_depth_luma) return bit_depth_luma.error();
        sps.bit_depth_luma_minus8 = *bit_depth_luma;

        auto bit_depth_chroma = reader.read_ue();
        if (!bit_depth_chroma) return bit_depth_chroma.error();
        sps.bit_depth_chroma_minus8 = *bit_depth_chroma;

        auto qp_bypass = reader.read_bit();
        if (!qp_bypass) return qp_bypass.error();
        sps.qpprime_y_zero_transform_bypass_flag = (*qp_bypass != 0);

        auto scaling_matrix = reader.read_bit();
        if (!scaling_matrix) return scaling_matrix.error();
        sps.seq_scaling_matrix_present_flag = (*scaling_matrix != 0);

        if (sps.seq_scaling_matrix_present_flag) {
            size_t count = (sps.chroma_format_idc != 3) ? 8 : 12;
            for (size_t i = 0; i < count; ++i) {
                auto list_present = reader.read_bit();
                if (!list_present) return list_present.error();
                if (*list_present != 0) {
                    size_t size = (i < 6) ? 16 : 64;
                    int32_t last_scale = 8;
                    int32_t next_scale = 8;
                    for (size_t j = 0; j < size; ++j) {
                        if (next_scale != 0) {
                            auto delta = reader.read_se();
                            if (!delta) return delta.error();
                            next_scale = (last_scale + *delta + 256) % 256;
                        }
                        last_scale = (next_scale == 0) ? last_scale : next_scale;
                    }
                }
            }
        }
    }

    auto log2_max_fn = reader.read_ue();
    if (!log2_max_fn) return log2_max_fn.error();
    sps.log2_max_frame_num_minus4 = *log2_max_fn;

    auto poc_type = reader.read_ue();
    if (!poc_type) return poc_type.error();
    sps.pic_order_cnt_type = *poc_type;

    if (sps.pic_order_cnt_type == 0) {
        auto log2_max_poc = reader.read_ue();
        if (!log2_max_poc) return log2_max_poc.error();
        sps.log2_max_pic_order_cnt_lsb_minus4 = *log2_max_poc;
    } else if (sps.pic_order_cnt_type == 1) {
        auto d_poc_zero = reader.read_bit();
        if (!d_poc_zero) return d_poc_zero.error();
        sps.delta_pic_order_always_zero_flag = (*d_poc_zero != 0);

        auto off_non_ref = reader.read_se();
        if (!off_non_ref) return off_non_ref.error();
        sps.offset_for_non_ref_pic = *off_non_ref;

        auto off_top_bot = reader.read_se();
        if (!off_top_bot) return off_top_bot.error();
        sps.offset_for_top_to_bottom_field = *off_top_bot;

        auto num_ref_frames_cycle = reader.read_ue();
        if (!num_ref_frames_cycle) return num_ref_frames_cycle.error();
        sps.num_ref_frames_in_pic_order_cnt_cycle = *num_ref_frames_cycle;

        sps.offset_for_ref_frame.resize(sps.num_ref_frames_in_pic_order_cnt_cycle);
        for (uint32_t i = 0; i < sps.num_ref_frames_in_pic_order_cnt_cycle; ++i) {
            auto off_ref = reader.read_se();
            if (!off_ref) return off_ref.error();
            sps.offset_for_ref_frame[i] = *off_ref;
        }
    }

    auto max_ref = reader.read_ue();
    if (!max_ref) return max_ref.error();
    sps.max_num_ref_frames = *max_ref;

    auto gaps = reader.read_bit();
    if (!gaps) return gaps.error();
    sps.gaps_in_frame_num_value_allowed_flag = (*gaps != 0);

    auto pic_w = reader.read_ue();
    if (!pic_w) return pic_w.error();
    sps.pic_width_in_mbs_minus1 = *pic_w;

    auto pic_h = reader.read_ue();
    if (!pic_h) return pic_h.error();
    sps.pic_height_in_map_units_minus1 = *pic_h;

    auto frame_mbs_only = reader.read_bit();
    if (!frame_mbs_only) return frame_mbs_only.error();
    sps.frame_mbs_only_flag = (*frame_mbs_only != 0);

    if (!sps.frame_mbs_only_flag) {
        auto mb_adapt = reader.read_bit();
        if (!mb_adapt) return mb_adapt.error();
        sps.mb_adaptive_frame_field_flag = (*mb_adapt != 0);
    }

    auto direct_8x8 = reader.read_bit();
    if (!direct_8x8) return direct_8x8.error();
    sps.direct_8x8_inference_flag = (*direct_8x8 != 0);

    auto crop = reader.read_bit();
    if (!crop) return crop.error();
    sps.frame_cropping_flag = (*crop != 0);

    if (sps.frame_cropping_flag) {
        auto c_left = reader.read_ue(); if (!c_left) return c_left.error(); sps.frame_crop_left_offset = *c_left;
        auto c_right = reader.read_ue(); if (!c_right) return c_right.error(); sps.frame_crop_right_offset = *c_right;
        auto c_top = reader.read_ue(); if (!c_top) return c_top.error(); sps.frame_crop_top_offset = *c_top;
        auto c_bottom = reader.read_ue(); if (!c_bottom) return c_bottom.error(); sps.frame_crop_bottom_offset = *c_bottom;
    }

    auto vui_present = reader.read_bit();
    if (!vui_present) return vui_present.error();
    sps.vui_parameters_present_flag = (*vui_present != 0);

    if (sps.vui_parameters_present_flag) {
        auto ar_present = reader.read_bit();
        if (!ar_present) return ar_present.error();
        sps.vui.aspect_ratio_info_present_flag = (*ar_present != 0);
        if (sps.vui.aspect_ratio_info_present_flag) {
            auto ar_idc = reader.read_bits(8);
            if (!ar_idc) return ar_idc.error();
            sps.vui.aspect_ratio_idc = static_cast<uint8_t>(*ar_idc);
            if (sps.vui.aspect_ratio_idc == 255 /* Extended_SAR */) {
                auto sar_w = reader.read_bits(16); if (!sar_w) return sar_w.error(); sps.vui.sar_width = static_cast<uint16_t>(*sar_w);
                auto sar_h = reader.read_bits(16); if (!sar_h) return sar_h.error(); sps.vui.sar_height = static_cast<uint16_t>(*sar_h);
            }
        }

        auto overscan = reader.read_bit();
        if (!overscan) return overscan.error();
        sps.vui.overscan_info_present_flag = (*overscan != 0);
        if (sps.vui.overscan_info_present_flag) {
            auto overscan_app = reader.read_bit();
            if (!overscan_app) return overscan_app.error();
            sps.vui.overscan_appropriate_flag = (*overscan_app != 0);
        }

        auto vid_signal = reader.read_bit();
        if (!vid_signal) return vid_signal.error();
        sps.vui.video_signal_type_present_flag = (*vid_signal != 0);
        if (sps.vui.video_signal_type_present_flag) {
            auto v_fmt = reader.read_bits(3); if (!v_fmt) return v_fmt.error(); sps.vui.video_format = static_cast<uint8_t>(*v_fmt);
            auto full_rng = reader.read_bit(); if (!full_rng) return full_rng.error(); sps.vui.video_full_range_flag = (*full_rng != 0);
            auto col_desc = reader.read_bit(); if (!col_desc) return col_desc.error(); sps.vui.colour_description_present_flag = (*col_desc != 0);
            if (sps.vui.colour_description_present_flag) {
                auto prim = reader.read_bits(8); if (!prim) return prim.error(); sps.vui.colour_primaries = static_cast<uint8_t>(*prim);
                auto trans = reader.read_bits(8); if (!trans) return trans.error(); sps.vui.transfer_characteristics = static_cast<uint8_t>(*trans);
                auto mat = reader.read_bits(8); if (!mat) return mat.error(); sps.vui.matrix_coefficients = static_cast<uint8_t>(*mat);
            }
        }

        auto chroma_loc = reader.read_bit();
        if (!chroma_loc) return chroma_loc.error();
        sps.vui.chroma_loc_info_present_flag = (*chroma_loc != 0);
        if (sps.vui.chroma_loc_info_present_flag) {
            auto loc_top = reader.read_ue(); if (!loc_top) return loc_top.error(); sps.vui.chroma_sample_loc_type_top_field = *loc_top;
            auto loc_bot = reader.read_ue(); if (!loc_bot) return loc_bot.error(); sps.vui.chroma_sample_loc_type_bottom_field = *loc_bot;
        }

        auto timing = reader.read_bit();
        if (!timing) return timing.error();
        sps.vui.timing_info_present_flag = (*timing != 0);
        if (sps.vui.timing_info_present_flag) {
            auto units = reader.read_bits(32); if (!units) return units.error(); sps.vui.num_units_in_tick = *units;
            auto scale = reader.read_bits(32); if (!scale) return scale.error(); sps.vui.time_scale = *scale;
            auto fixed_fps = reader.read_bit(); if (!fixed_fps) return fixed_fps.error(); sps.vui.fixed_frame_rate_flag = (*fixed_fps != 0);
        }
    }

    return sps;
}

} // namespace atum

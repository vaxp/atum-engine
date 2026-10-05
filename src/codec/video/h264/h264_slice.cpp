#include "atum/codec/video/h264/h264_slice.hpp"

namespace atum {

MediaResult<H264SliceHeader> H264SliceHeader::parse(
    BitReader& reader,
    H264NalType nal_type,
    const H264Sps& sps,
    const H264Pps& pps) noexcept {

    H264SliceHeader slice{};

    auto first_mb = reader.read_ue();
    if (!first_mb) return first_mb.error();
    slice.first_mb_in_slice = *first_mb;

    auto s_type = reader.read_ue();
    if (!s_type) return s_type.error();
    slice.raw_slice_type = *s_type;
    slice.slice_type = normalize_h264_slice_type(slice.raw_slice_type);

    auto pps_id = reader.read_ue();
    if (!pps_id) return pps_id.error();
    slice.pic_parameter_set_id = *pps_id;

    if (sps.separate_colour_plane_flag) {
        auto plane = reader.read_bits(2);
        if (!plane) return plane.error();
        slice.colour_plane_id = static_cast<uint8_t>(*plane);
    }

    size_t fn_bits = sps.log2_max_frame_num_minus4 + 4;
    auto f_num = reader.read_bits(fn_bits);
    if (!f_num) return f_num.error();
    slice.frame_num = *f_num;

    if (!sps.frame_mbs_only_flag) {
        auto field_pic = reader.read_bit();
        if (!field_pic) return field_pic.error();
        slice.field_pic_flag = (*field_pic != 0);

        if (slice.field_pic_flag) {
            auto bot_field = reader.read_bit();
            if (!bot_field) return bot_field.error();
            slice.bottom_field_flag = (*bot_field != 0);
        }
    }

    if (nal_type == H264NalType::SliceIdr) {
        auto idr_id = reader.read_ue();
        if (!idr_id) return idr_id.error();
        slice.idr_pic_id = *idr_id;
    }

    if (sps.pic_order_cnt_type == 0) {
        size_t poc_bits = sps.log2_max_pic_order_cnt_lsb_minus4 + 4;
        auto poc_lsb = reader.read_bits(poc_bits);
        if (!poc_lsb) return poc_lsb.error();
        slice.pic_order_cnt_lsb = *poc_lsb;

        if (pps.bottom_field_pic_order_in_frame_present_flag && !slice.field_pic_flag) {
            auto delta_poc_bot = reader.read_se();
            if (!delta_poc_bot) return delta_poc_bot.error();
            slice.delta_pic_order_cnt_bottom = *delta_poc_bot;
        }
    } else if (sps.pic_order_cnt_type == 1 && !sps.delta_pic_order_always_zero_flag) {
        auto delta_poc0 = reader.read_se();
        if (!delta_poc0) return delta_poc0.error();
        slice.delta_pic_order_cnt[0] = *delta_poc0;

        if (pps.bottom_field_pic_order_in_frame_present_flag && !slice.field_pic_flag) {
            auto delta_poc1 = reader.read_se();
            if (!delta_poc1) return delta_poc1.error();
            slice.delta_pic_order_cnt[1] = *delta_poc1;
        }
    }

    if (pps.redundant_pic_cnt_present_flag) {
        auto red_pic = reader.read_ue();
        if (!red_pic) return red_pic.error();
        slice.redundant_pic_cnt = *red_pic;
    }

    if (slice.slice_type == H264SliceType::B) {
        auto direct_mv = reader.read_bit();
        if (!direct_mv) return direct_mv.error();
        slice.direct_spatial_mv_pred_flag = (*direct_mv != 0);
    }

    if (slice.slice_type == H264SliceType::P ||
        slice.slice_type == H264SliceType::SP ||
        slice.slice_type == H264SliceType::B) {

        auto ref_override = reader.read_bit();
        if (!ref_override) return ref_override.error();
        slice.num_ref_idx_active_override_flag = (*ref_override != 0);

        if (slice.num_ref_idx_active_override_flag) {
            auto ref_l0 = reader.read_ue();
            if (!ref_l0) return ref_l0.error();
            slice.num_ref_idx_l0_active_minus1 = *ref_l0;

            if (slice.slice_type == H264SliceType::B) {
                auto ref_l1 = reader.read_ue();
                if (!ref_l1) return ref_l1.error();
                slice.num_ref_idx_l1_active_minus1 = *ref_l1;
            }
        }
    }

    // Ref pic list modification
    if (slice.slice_type != H264SliceType::I && slice.slice_type != H264SliceType::SI) {
        auto mod_l0 = reader.read_bit();
        if (!mod_l0) return mod_l0.error();
        if (*mod_l0 != 0) {
            while (true) {
                auto op = reader.read_ue();
                if (!op) return op.error();
                if (*op == 3) break;
                auto delta = reader.read_ue();
                if (!delta) return delta.error();
            }
        }
        if (slice.slice_type == H264SliceType::B) {
            auto mod_l1 = reader.read_bit();
            if (!mod_l1) return mod_l1.error();
            if (*mod_l1 != 0) {
                while (true) {
                    auto op = reader.read_ue();
                    if (!op) return op.error();
                    if (*op == 3) break;
                    auto delta = reader.read_ue();
                    if (!delta) return delta.error();
                }
            }
        }
    }

    // Dec ref pic marking
    if (nal_type == H264NalType::SliceIdr) {
        auto no_output = reader.read_bit();
        if (!no_output) return no_output.error();
        auto long_term = reader.read_bit();
        if (!long_term) return long_term.error();
    } else {
        auto adaptive = reader.read_bit();
        if (!adaptive) return adaptive.error();
        if (*adaptive != 0) {
            while (true) {
                auto op = reader.read_ue();
                if (!op) return op.error();
                if (*op == 0) break;
                if (*op == 1 || *op == 3) {
                    auto diff = reader.read_ue(); if (!diff) return diff.error();
                }
                if (*op == 2) {
                    auto ltf = reader.read_ue(); if (!ltf) return ltf.error();
                }
                if (*op == 3 || *op == 6) {
                    auto max_lt = reader.read_ue(); if (!max_lt) return max_lt.error();
                }
                if (*op == 4) {
                    auto max_num = reader.read_ue(); if (!max_num) return max_num.error();
                }
            }
        }
    }

    if (pps.entropy_coding_mode_flag &&
        slice.slice_type != H264SliceType::I &&
        slice.slice_type != H264SliceType::SI) {
        auto c_init = reader.read_ue();
        if (!c_init) return c_init.error();
        slice.cabac_init_idc = *c_init;
    }

    auto qp_delta = reader.read_se();
    if (!qp_delta) return qp_delta.error();
    slice.slice_qp_delta = *qp_delta;

    return slice;
}

} // namespace atum

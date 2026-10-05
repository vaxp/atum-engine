#include "atum/codec/video/h264/h264_pps.hpp"

namespace atum {

MediaResult<H264Pps> H264Pps::parse_rbsp(std::span<const uint8_t> rbsp_bytes) noexcept {
    BitReader reader(rbsp_bytes);
    return parse(reader);
}

MediaResult<H264Pps> H264Pps::parse(BitReader& reader) noexcept {
    H264Pps pps{};

    auto pps_id = reader.read_ue();
    if (!pps_id) return pps_id.error();
    pps.pic_parameter_set_id = *pps_id;

    auto sps_id = reader.read_ue();
    if (!sps_id) return sps_id.error();
    pps.seq_parameter_set_id = *sps_id;

    auto entropy = reader.read_bit();
    if (!entropy) return entropy.error();
    pps.entropy_coding_mode_flag = (*entropy != 0);

    auto bot_field = reader.read_bit();
    if (!bot_field) return bot_field.error();
    pps.bottom_field_pic_order_in_frame_present_flag = (*bot_field != 0);

    auto num_slice_groups = reader.read_ue();
    if (!num_slice_groups) return num_slice_groups.error();
    pps.num_slice_groups_minus1 = *num_slice_groups;

    if (pps.num_slice_groups_minus1 > 0) {
        auto map_type = reader.read_ue();
        if (!map_type) return map_type.error();
        uint32_t smt = *map_type;

        if (smt == 0) {
            for (uint32_t i = 0; i <= pps.num_slice_groups_minus1; ++i) {
                auto run_len = reader.read_ue();
                if (!run_len) return run_len.error();
            }
        } else if (smt == 2) {
            for (uint32_t i = 0; i < pps.num_slice_groups_minus1; ++i) {
                auto tl = reader.read_ue(); if (!tl) return tl.error();
                auto br = reader.read_ue(); if (!br) return br.error();
            }
        } else if (smt == 3 || smt == 4 || smt == 5) {
            auto dir = reader.read_bit(); if (!dir) return dir.error();
            auto rate = reader.read_ue(); if (!rate) return rate.error();
        } else if (smt == 6) {
            auto pic_size = reader.read_ue();
            if (!pic_size) return pic_size.error();
            uint32_t count = *pic_size + 1;
            uint32_t bits = 1;
            while ((1u << bits) <= pps.num_slice_groups_minus1) bits++;
            for (uint32_t i = 0; i < count; ++i) {
                auto grp = reader.read_bits(bits);
                if (!grp) return grp.error();
            }
        }
    }

    auto ref_l0 = reader.read_ue();
    if (!ref_l0) return ref_l0.error();
    pps.num_ref_idx_l0_default_active_minus1 = *ref_l0;

    auto ref_l1 = reader.read_ue();
    if (!ref_l1) return ref_l1.error();
    pps.num_ref_idx_l1_default_active_minus1 = *ref_l1;

    auto weighted_pred = reader.read_bit();
    if (!weighted_pred) return weighted_pred.error();
    pps.weighted_pred_flag = (*weighted_pred != 0);

    auto weighted_bipred = reader.read_bits(2);
    if (!weighted_bipred) return weighted_bipred.error();
    pps.weighted_bipred_idc = static_cast<uint8_t>(*weighted_bipred);

    auto init_qp = reader.read_se();
    if (!init_qp) return init_qp.error();
    pps.pic_init_qp_minus26 = *init_qp;

    auto init_qs = reader.read_se();
    if (!init_qs) return init_qs.error();
    pps.pic_init_qs_minus26 = *init_qs;

    auto chroma_qp = reader.read_se();
    if (!chroma_qp) return chroma_qp.error();
    pps.chroma_qp_index_offset = *chroma_qp;

    auto deblock = reader.read_bit();
    if (!deblock) return deblock.error();
    pps.deblocking_filter_control_present_flag = (*deblock != 0);

    auto constrained_intra = reader.read_bit();
    if (!constrained_intra) return constrained_intra.error();
    pps.constrained_intra_pred_flag = (*constrained_intra != 0);

    auto redundant_pic = reader.read_bit();
    if (!redundant_pic) return redundant_pic.error();
    pps.redundant_pic_cnt_present_flag = (*redundant_pic != 0);

    if (reader.has_bits(1)) {
        auto t8x8 = reader.read_bit();
        if (t8x8) {
            pps.transform_8x8_mode_flag = (*t8x8 != 0);
            if (reader.has_bits(1)) {
                auto p_scaling = reader.read_bit();
                if (p_scaling) {
                    pps.pic_scaling_matrix_present_flag = (*p_scaling != 0);
                    if (pps.pic_scaling_matrix_present_flag) {
                        size_t count = 6 + (pps.transform_8x8_mode_flag ? 2 : 0);
                        for (size_t i = 0; i < count; ++i) {
                            auto present = reader.read_bit();
                            if (!present) return present.error();
                            if (*present != 0) {
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
                    if (reader.has_bits(1)) {
                        auto sec_chroma = reader.read_se();
                        if (sec_chroma) {
                            pps.second_chroma_qp_index_offset = *sec_chroma;
                        }
                    }
                }
            }
        }
    }

    return pps;
}

} // namespace atum

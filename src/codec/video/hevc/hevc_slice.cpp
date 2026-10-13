#include "atum/codec/video/hevc/hevc_slice.hpp"
#include <vector>
#include <cmath>

namespace atum {

static inline uint32_t rbsp_offset_to_ebsp_offset(std::span<const uint8_t> ebsp_payload, size_t rbsp_byte_offset) {
    size_t ebsp_idx = 0;
    size_t rbsp_idx = 0;
    while (ebsp_idx < ebsp_payload.size() && rbsp_idx < rbsp_byte_offset) {
        if (ebsp_idx + 2 < ebsp_payload.size() &&
            ebsp_payload[ebsp_idx] == 0 &&
            ebsp_payload[ebsp_idx + 1] == 0 &&
            ebsp_payload[ebsp_idx + 2] == 3) {
            ebsp_idx += 3;
            rbsp_idx += 2;
        } else {
            ebsp_idx++;
            rbsp_idx++;
        }
    }
    return static_cast<uint32_t>(ebsp_idx);
}

MediaResult<HevcSliceHeader> HevcSliceHeader::parse(
    BitReader& reader,
    HevcNalType nal_type,
    const HevcSps& sps,
    const HevcPps& pps,
    std::span<const uint8_t> raw_nal) noexcept {

    HevcSliceHeader slice{};
    slice.nal_unit_type = nal_type;

    auto first_flag = reader.read_bit(); if (!first_flag) return first_flag.error();
    slice.first_slice_segment_in_pic_flag = (*first_flag != 0);

    if (hevc_is_irap_nal(nal_type)) {
        auto no_out = reader.read_bit(); if (!no_out) return no_out.error();
        slice.no_output_of_prior_pics_flag = (*no_out != 0);
    }

    auto pps_id = reader.read_ue(); if (!pps_id) return pps_id.error();
    slice.slice_pic_parameter_set_id = *pps_id;

    uint32_t min_cb_size = 1u << (sps.log2_min_luma_coding_block_size_minus3 + 3);
    uint32_t ctb_size = min_cb_size << sps.log2_diff_max_min_luma_coding_block_size;
    uint32_t pic_width_in_ctbs = (sps.pic_width_in_luma_samples + ctb_size - 1) / ctb_size;
    uint32_t pic_height_in_ctbs = (sps.pic_height_in_luma_samples + ctb_size - 1) / ctb_size;
    uint32_t pic_size_in_ctbs = pic_width_in_ctbs * pic_height_in_ctbs;

    if (!slice.first_slice_segment_in_pic_flag) {
        if (pps.dependent_slice_segments_enabled_flag) {
            auto dep = reader.read_bit(); if (!dep) return dep.error();
            slice.dependent_slice_segment_flag = (*dep != 0);
        }

        uint32_t bits = 1;
        while ((1u << bits) < pic_size_in_ctbs) bits++;
        auto addr = reader.read_bits(bits); if (!addr) return addr.error();
        slice.slice_segment_address = *addr;
    }

    if (!slice.dependent_slice_segment_flag) {
        if (pps.num_extra_slice_header_bits > 0) {
            auto sk = reader.skip_bits(pps.num_extra_slice_header_bits);
            if (!sk) return sk.error();
        }

        auto st = reader.read_ue(); if (!st) return st.error();
        slice.raw_slice_type = *st;
        slice.slice_type = static_cast<HevcSliceType>(slice.raw_slice_type % 3);

        if (pps.output_flag_present_flag) {
            auto pic_out = reader.read_bit(); if (!pic_out) return pic_out.error();
            slice.pic_output_flag = (*pic_out != 0);
        }

        if (sps.separate_colour_plane_flag) {
            auto plane = reader.read_bits(2); if (!plane) return plane.error();
            slice.colour_plane_id = static_cast<uint8_t>(*plane);
        }

        uint32_t num_pic_total_curr = 0;

        if (nal_type != HevcNalType::IDR_W_RADL && nal_type != HevcNalType::IDR_N_LP) {
            size_t poc_bits = sps.log2_max_pic_order_cnt_lsb_minus4 + 4;
            auto poc = reader.read_bits(poc_bits); if (!poc) return poc.error();
            slice.slice_pic_order_cnt_lsb = *poc;

            auto st_rps_sps = reader.read_bit(); if (!st_rps_sps) return st_rps_sps.error();
            if (*st_rps_sps == 0) {
                // Short-term ref pic set directly in slice header
                bool inter_rps_pred = false;
                if (sps.num_short_term_ref_pic_sets > 0) {
                    auto inter = reader.read_bit(); if (!inter) return inter.error();
                    inter_rps_pred = (*inter != 0);
                }
                if (inter_rps_pred) {
                    auto delta_sign = reader.read_bit(); if (!delta_sign) return delta_sign.error();
                    auto abs_delta = reader.read_ue(); if (!abs_delta) return abs_delta.error();
                } else {
                    auto num_neg = reader.read_ue(); if (!num_neg) return num_neg.error();
                    auto num_pos = reader.read_ue(); if (!num_pos) return num_pos.error();
                    int32_t curr_poc = static_cast<int32_t>(slice.slice_pic_order_cnt_lsb);
                    int32_t d_poc = 0;
                    for (uint32_t i = 0; i < *num_neg; ++i) {
                        auto d_m1 = reader.read_ue(); if (!d_m1) return d_m1.error();
                        auto used = reader.read_bit(); if (!used) return used.error();
                        d_poc -= static_cast<int32_t>(*d_m1 + 1);
                        if (*used != 0) {
                            slice.rps_curr_before.push_back(curr_poc + d_poc);
                            num_pic_total_curr++;
                        } else {
                            slice.rps_foll.push_back(curr_poc + d_poc);
                        }
                    }
                    d_poc = 0;
                    for (uint32_t i = 0; i < *num_pos; ++i) {
                        auto d_m1 = reader.read_ue(); if (!d_m1) return d_m1.error();
                        auto used = reader.read_bit(); if (!used) return used.error();
                        d_poc += static_cast<int32_t>(*d_m1 + 1);
                        if (*used != 0) {
                            slice.rps_curr_after.push_back(curr_poc + d_poc);
                            num_pic_total_curr++;
                        } else {
                            slice.rps_foll.push_back(curr_poc + d_poc);
                        }
                    }
                }
            } else if (sps.num_short_term_ref_pic_sets > 1) {
                uint32_t idx_bits = 1;
                while ((1u << idx_bits) < sps.num_short_term_ref_pic_sets) idx_bits++;
                auto idx = reader.read_bits(idx_bits); if (!idx) return idx.error();
                num_pic_total_curr = 1; // At least one reference
            } else {
                num_pic_total_curr = 1;
            }

            if (sps.long_term_ref_pics_present_flag) {
                uint32_t num_lt_sps = 0;
                if (sps.num_long_term_ref_pics_sps > 0) {
                    auto lts = reader.read_ue(); if (!lts) return lts.error();
                    num_lt_sps = *lts;
                }
                auto num_lt_pics = reader.read_ue(); if (!num_lt_pics) return num_lt_pics.error();
                for (uint32_t i = 0; i < num_lt_sps + *num_lt_pics; ++i) {
                    if (i < num_lt_sps) {
                        if (sps.num_long_term_ref_pics_sps > 1) {
                            uint32_t l_bits = 1;
                            while ((1u << l_bits) < sps.num_long_term_ref_pics_sps) l_bits++;
                            reader.read_bits(l_bits);
                        }
                    } else {
                        reader.read_bits(poc_bits);
                        reader.read_bit();
                    }
                    auto msb = reader.read_bit();
                    if (msb && *msb != 0) reader.read_ue();
                }
            }

            if (sps.sps_temporal_mvp_enabled_flag) {
                auto tmvp = reader.read_bit(); if (!tmvp) return tmvp.error();
                slice.slice_temporal_mvp_enabled_flag = (*tmvp != 0);
            }
        }

        if (sps.sample_adaptive_offset_enabled_flag) {
            auto sao_l = reader.read_bit(); if (!sao_l) return sao_l.error();
            slice.slice_sao_luma_flag = (*sao_l != 0);
            if (!sps.separate_colour_plane_flag && sps.chroma_format_idc != 0) {
                auto sao_c = reader.read_bit(); if (!sao_c) return sao_c.error();
                slice.slice_sao_chroma_flag = (*sao_c != 0);
            }
        }

        if (slice.slice_type == HevcSliceType::P || slice.slice_type == HevcSliceType::B) {
            uint32_t num_ref_idx_l0 = pps.num_ref_idx_l0_default_active_minus1 + 1;
            uint32_t num_ref_idx_l1 = pps.num_ref_idx_l1_default_active_minus1 + 1;

            auto override_flag = reader.read_bit(); if (!override_flag) return override_flag.error();
            if (*override_flag != 0) {
                auto l0 = reader.read_ue(); if (!l0) return l0.error();
                num_ref_idx_l0 = *l0 + 1;
                if (slice.slice_type == HevcSliceType::B) {
                    auto l1 = reader.read_ue(); if (!l1) return l1.error();
                    num_ref_idx_l1 = *l1 + 1;
                }
            }

            slice.num_ref_idx_l0_active_minus1 = static_cast<uint8_t>(num_ref_idx_l0 - 1);
            slice.num_ref_idx_l1_active_minus1 = (slice.slice_type == HevcSliceType::B) ? static_cast<uint8_t>(num_ref_idx_l1 - 1) : 0;

            if (pps.lists_modification_present_flag && num_pic_total_curr > 1) {
                auto mod_l0 = reader.read_bit(); if (!mod_l0) return mod_l0.error();
                if (*mod_l0 != 0) {
                    uint32_t bits = 1;
                    while ((1u << bits) < num_pic_total_curr) bits++;
                    for (uint32_t i = 0; i < num_ref_idx_l0; ++i) reader.read_bits(bits);
                }
                if (slice.slice_type == HevcSliceType::B) {
                    auto mod_l1 = reader.read_bit(); if (!mod_l1) return mod_l1.error();
                    if (*mod_l1 != 0) {
                        uint32_t bits = 1;
                        while ((1u << bits) < num_pic_total_curr) bits++;
                        for (uint32_t i = 0; i < num_ref_idx_l1; ++i) reader.read_bits(bits);
                    }
                }
            }

            if (slice.slice_type == HevcSliceType::B) {
                auto mvd_l1 = reader.read_bit(); if (!mvd_l1) return mvd_l1.error();
                slice.mvd_l1_zero_flag = (*mvd_l1 != 0);
            }

            if (pps.cabac_init_present_flag) {
                auto cabac = reader.read_bit(); if (!cabac) return cabac.error();
                slice.cabac_init_flag = (*cabac != 0);
            }

            if (slice.slice_temporal_mvp_enabled_flag) {
                uint32_t collocated_ref_limit = 0;
                if (slice.slice_type == HevcSliceType::B) {
                    auto col_from_l0 = reader.read_bit(); if (!col_from_l0) return col_from_l0.error();
                    slice.collocated_from_l0_flag = (*col_from_l0 != 0);
                    collocated_ref_limit = (*col_from_l0 != 0) ? num_ref_idx_l0 : num_ref_idx_l1;
                } else {
                    slice.collocated_from_l0_flag = true;
                    collocated_ref_limit = num_ref_idx_l0;
                }
                slice.collocated_ref_idx = 0;
                if (collocated_ref_limit > 1) {
                    auto col_idx = reader.read_ue();
                    if (!col_idx) return col_idx.error();
                    slice.collocated_ref_idx = static_cast<uint8_t>(*col_idx);
                }
            } else {
                slice.collocated_ref_idx = 0xFF;
            }

            if ((pps.weighted_pred_flag && slice.slice_type == HevcSliceType::P) ||
                (pps.weighted_bipred_flag && slice.slice_type == HevcSliceType::B)) {
                // pred_weight_table
                auto luma_log2 = reader.read_ue(); if (!luma_log2) return luma_log2.error();
                if (sps.chroma_format_idc != 0) {
                    reader.read_se();
                }
                std::vector<uint8_t> l0_luma_flag(num_ref_idx_l0, 0);
                std::vector<uint8_t> l0_chroma_flag(num_ref_idx_l0, 0);
                for (uint32_t i = 0; i < num_ref_idx_l0; ++i) {
                    auto f = reader.read_bit(); if (!f) return f.error();
                    l0_luma_flag[i] = static_cast<uint8_t>(*f);
                }
                if (sps.chroma_format_idc != 0) {
                    for (uint32_t i = 0; i < num_ref_idx_l0; ++i) {
                        auto f = reader.read_bit(); if (!f) return f.error();
                        l0_chroma_flag[i] = static_cast<uint8_t>(*f);
                    }
                }
                for (uint32_t i = 0; i < num_ref_idx_l0; ++i) {
                    if (l0_luma_flag[i]) { reader.read_se(); reader.read_se(); }
                    if (l0_chroma_flag[i]) {
                        reader.read_se(); reader.read_se();
                        reader.read_se(); reader.read_se();
                    }
                }
                if (slice.slice_type == HevcSliceType::B) {
                    std::vector<uint8_t> l1_luma_flag(num_ref_idx_l1, 0);
                    std::vector<uint8_t> l1_chroma_flag(num_ref_idx_l1, 0);
                    for (uint32_t i = 0; i < num_ref_idx_l1; ++i) {
                        auto f = reader.read_bit(); if (!f) return f.error();
                        l1_luma_flag[i] = static_cast<uint8_t>(*f);
                    }
                    if (sps.chroma_format_idc != 0) {
                        for (uint32_t i = 0; i < num_ref_idx_l1; ++i) {
                            auto f = reader.read_bit(); if (!f) return f.error();
                            l1_chroma_flag[i] = static_cast<uint8_t>(*f);
                        }
                    }
                    for (uint32_t i = 0; i < num_ref_idx_l1; ++i) {
                        if (l1_luma_flag[i]) { reader.read_se(); reader.read_se(); }
                        if (l1_chroma_flag[i]) {
                            reader.read_se(); reader.read_se();
                            reader.read_se(); reader.read_se();
                        }
                    }
                }
            }

            auto five_minus = reader.read_ue(); if (!five_minus) return five_minus.error();
            slice.five_minus_max_num_merge_cand = static_cast<uint8_t>(*five_minus);
        }

        // QP delta
        auto qp = reader.read_se(); if (!qp) return qp.error();
        slice.slice_qp_delta = *qp;

        if (pps.pps_slice_chroma_qp_offsets_present_flag) {
            reader.read_se(); reader.read_se();
        }

        if (pps.deblocking_filter_override_enabled_flag) {
            auto deb_ov = reader.read_bit();
            if (deb_ov && *deb_ov != 0) {
                auto deb_dis = reader.read_bit();
                if (deb_dis) slice.slice_deblocking_filter_disabled_flag = (*deb_dis != 0);
                if (deb_dis && *deb_dis == 0) {
                    reader.read_se(); reader.read_se();
                }
            } else {
                slice.slice_deblocking_filter_disabled_flag = pps.pps_deblocking_filter_disabled_flag;
            }
        } else {
            slice.slice_deblocking_filter_disabled_flag = pps.pps_deblocking_filter_disabled_flag;
        }

        if (pps.pps_loop_filter_across_slices_enabled_flag) {
            auto l_flag = reader.read_bit();
            if (l_flag) slice.slice_loop_filter_across_slices_enabled_flag = (*l_flag != 0);
        } else {
            slice.slice_loop_filter_across_slices_enabled_flag = false;
        }
    }

    if (pps.tiles_enabled_flag || pps.entropy_coding_sync_enabled_flag) {
        auto num_entry = reader.read_ue();
        if (num_entry && *num_entry > 0) {
            auto offset_len = reader.read_ue();
            if (offset_len) {
                for (uint32_t i = 0; i < *num_entry; ++i) {
                    reader.skip_bits(*offset_len + 1);
                }
            }
        }
    }

    if (pps.slice_segment_header_extension_present_flag) {
        auto ext_len = reader.read_ue();
        if (ext_len && *ext_len > 0) {
            for (uint32_t i = 0; i < *ext_len; ++i) reader.skip_bits(8);
        }
    }

    // Byte alignment: 1-bit stop bit, then align to byte
    reader.read_bit();
    while (reader.bits_read() % 8 != 0 && reader.has_bits(1)) {
        reader.read_bit();
    }

    size_t rbsp_byte_offset = (reader.bits_read() + 7) / 8;
    if (raw_nal.size() >= 2) {
        slice.data_byte_offset = 2 + rbsp_offset_to_ebsp_offset(raw_nal.subspan(2), rbsp_byte_offset);
    } else {
        slice.data_byte_offset = 2 + static_cast<uint32_t>(rbsp_byte_offset);
    }

    return slice;
}

} // namespace atum

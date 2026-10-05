#include "atum/codec/video/hevc/hevc_parameter_sets.hpp"

namespace atum {

MediaResult<HevcProfileTierLevel> HevcProfileTierLevel::parse(BitReader& reader, uint8_t max_sub_layers_minus1) noexcept {
    HevcProfileTierLevel ptl{};

    auto space = reader.read_bits(2); if (!space) return space.error(); ptl.general_profile_space = static_cast<uint8_t>(*space);
    auto tier = reader.read_bit(); if (!tier) return tier.error(); ptl.general_tier_flag = (*tier != 0);
    auto prof = reader.read_bits(5); if (!prof) return prof.error(); ptl.general_profile_idc = static_cast<uint8_t>(*prof);
    auto compat = reader.read_bits(32); if (!compat) return compat.error(); ptl.general_profile_compatibility_flags = *compat;

    auto prog = reader.read_bit(); if (!prog) return prog.error(); ptl.general_progressive_source_flag = (*prog != 0);
    auto inter = reader.read_bit(); if (!inter) return inter.error(); ptl.general_interlaced_source_flag = (*inter != 0);
    auto non_pack = reader.read_bit(); if (!non_pack) return non_pack.error(); ptl.general_non_packed_constraint_flag = (*non_pack != 0);
    auto frame_only = reader.read_bit(); if (!frame_only) return frame_only.error(); ptl.general_frame_only_constraint_flag = (*frame_only != 0);

    // Skip 44 bits reserved
    auto skip32 = reader.skip_bits(32); if (!skip32) return skip32.error();
    auto skip12 = reader.skip_bits(12); if (!skip12) return skip12.error();

    auto lvl = reader.read_bits(8); if (!lvl) return lvl.error(); ptl.general_level_idc = static_cast<uint8_t>(*lvl);

    if (max_sub_layers_minus1 > 0) {
        std::vector<bool> sub_profile(max_sub_layers_minus1);
        std::vector<bool> sub_level(max_sub_layers_minus1);
        for (uint8_t i = 0; i < max_sub_layers_minus1; ++i) {
            auto sp = reader.read_bit(); if (!sp) return sp.error(); sub_profile[i] = (*sp != 0);
            auto sl = reader.read_bit(); if (!sl) return sl.error(); sub_level[i] = (*sl != 0);
        }
        if (max_sub_layers_minus1 < 8) {
            auto skip_res = reader.skip_bits(2 * (8 - max_sub_layers_minus1));
            if (!skip_res) return skip_res.error();
        }
        for (uint8_t i = 0; i < max_sub_layers_minus1; ++i) {
            if (sub_profile[i]) {
                auto sk = reader.skip_bits(32 + 32 + 24);
                if (!sk) return sk.error();
            }
            if (sub_level[i]) {
                auto sk = reader.skip_bits(8);
                if (!sk) return sk.error();
            }
        }
    }

    return ptl;
}

MediaResult<HevcVps> HevcVps::parse_rbsp(std::span<const uint8_t> rbsp) noexcept {
    BitReader reader(rbsp);
    return parse(reader);
}

MediaResult<HevcVps> HevcVps::parse(BitReader& reader) noexcept {
    HevcVps vps{};

    auto id = reader.read_bits(4); if (!id) return id.error(); vps.vps_video_parameter_set_id = static_cast<uint8_t>(*id);
    auto base_int = reader.read_bit(); if (!base_int) return base_int.error(); vps.vps_base_layer_internal_flag = (*base_int != 0);
    auto base_avail = reader.read_bit(); if (!base_avail) return base_avail.error(); vps.vps_base_layer_available_flag = (*base_avail != 0);
    auto max_layers = reader.read_bits(6); if (!max_layers) return max_layers.error(); vps.vps_max_layers_minus1 = static_cast<uint8_t>(*max_layers);
    auto max_sub = reader.read_bits(3); if (!max_sub) return max_sub.error(); vps.vps_max_sub_layers_minus1 = static_cast<uint8_t>(*max_sub);
    auto nesting = reader.read_bit(); if (!nesting) return nesting.error(); vps.vps_temporal_id_nesting_flag = (*nesting != 0);

    auto skip_res = reader.skip_bits(16); if (!skip_res) return skip_res.error();

    auto ptl_res = HevcProfileTierLevel::parse(reader, vps.vps_max_sub_layers_minus1);
    if (!ptl_res) return ptl_res.error();
    vps.ptl = *ptl_res;

    return vps;
}

MediaResult<HevcSps> HevcSps::parse_rbsp(std::span<const uint8_t> rbsp) noexcept {
    BitReader reader(rbsp);
    return parse(reader);
}

MediaResult<HevcSps> HevcSps::parse(BitReader& reader) noexcept {
    HevcSps sps{};

    auto vps_id = reader.read_bits(4); if (!vps_id) return vps_id.error(); sps.sps_video_parameter_set_id = static_cast<uint8_t>(*vps_id);
    auto max_sub = reader.read_bits(3); if (!max_sub) return max_sub.error(); sps.sps_max_sub_layers_minus1 = static_cast<uint8_t>(*max_sub);
    auto nesting = reader.read_bit(); if (!nesting) return nesting.error(); sps.sps_temporal_id_nesting_flag = (*nesting != 0);

    auto ptl_res = HevcProfileTierLevel::parse(reader, sps.sps_max_sub_layers_minus1);
    if (!ptl_res) return ptl_res.error();
    sps.ptl = *ptl_res;

    auto sps_id = reader.read_ue(); if (!sps_id) return sps_id.error(); sps.sps_seq_parameter_set_id = *sps_id;
    auto chroma = reader.read_ue(); if (!chroma) return chroma.error(); sps.chroma_format_idc = *chroma;

    if (sps.chroma_format_idc == 3) {
        auto sep = reader.read_bit(); if (!sep) return sep.error(); sps.separate_colour_plane_flag = (*sep != 0);
    }

    auto pic_w = reader.read_ue(); if (!pic_w) return pic_w.error(); sps.pic_width_in_luma_samples = *pic_w;
    auto pic_h = reader.read_ue(); if (!pic_h) return pic_h.error(); sps.pic_height_in_luma_samples = *pic_h;

    auto conf_flag = reader.read_bit(); if (!conf_flag) return conf_flag.error(); sps.conformance_window_flag = (*conf_flag != 0);
    if (sps.conformance_window_flag) {
        auto l = reader.read_ue(); if (!l) return l.error(); sps.conf_win.conf_win_left_offset = *l;
        auto r = reader.read_ue(); if (!r) return r.error(); sps.conf_win.conf_win_right_offset = *r;
        auto t = reader.read_ue(); if (!t) return t.error(); sps.conf_win.conf_win_top_offset = *t;
        auto b = reader.read_ue(); if (!b) return b.error(); sps.conf_win.conf_win_bottom_offset = *b;
    }

    auto bdl = reader.read_ue(); if (!bdl) return bdl.error(); sps.bit_depth_luma_minus8 = *bdl;
    auto bdc = reader.read_ue(); if (!bdc) return bdc.error(); sps.bit_depth_chroma_minus8 = *bdc;
    auto max_poc = reader.read_ue(); if (!max_poc) return max_poc.error(); sps.log2_max_pic_order_cnt_lsb_minus4 = *max_poc;

    auto sub_ord = reader.read_bit(); if (!sub_ord) return sub_ord.error(); sps.sps_sub_layer_ordering_info_present_flag = (*sub_ord != 0);
    uint32_t start_i = sps.sps_sub_layer_ordering_info_present_flag ? 0 : sps.sps_max_sub_layers_minus1;
    for (uint32_t i = start_i; i <= sps.sps_max_sub_layers_minus1; ++i) {
        auto dpb = reader.read_ue(); if (!dpb) return dpb.error();
        auto reord = reader.read_ue(); if (!reord) return reord.error();
        auto lat = reader.read_ue(); if (!lat) return lat.error();
    }

    auto min_cb = reader.read_ue(); if (!min_cb) return min_cb.error(); sps.log2_min_luma_coding_block_size_minus3 = *min_cb;
    auto diff_cb = reader.read_ue(); if (!diff_cb) return diff_cb.error(); sps.log2_diff_max_min_luma_coding_block_size = *diff_cb;
    auto min_tb = reader.read_ue(); if (!min_tb) return min_tb.error(); (void)min_tb;
    auto diff_tb = reader.read_ue(); if (!diff_tb) return diff_tb.error(); (void)diff_tb;
    auto max_depth_inter = reader.read_ue(); if (!max_depth_inter) return max_depth_inter.error(); (void)max_depth_inter;
    auto max_depth_intra = reader.read_ue(); if (!max_depth_intra) return max_depth_intra.error(); (void)max_depth_intra;

    auto scaling_en = reader.read_bit(); if (!scaling_en) return scaling_en.error();
    if (*scaling_en != 0) {
        auto scaling_present = reader.read_bit(); if (!scaling_present) return scaling_present.error();
        if (*scaling_present != 0) {
            for (size_t size_id = 0; size_id < 4; ++size_id) {
                size_t step = (size_id == 3) ? 3 : 1;
                for (size_t mat_id = 0; mat_id < 6; mat_id += step) {
                    auto pred_flag = reader.read_bit(); if (!pred_flag) return pred_flag.error();
                    if (*pred_flag == 0) {
                        auto pred_idx = reader.read_ue(); if (!pred_idx) return pred_idx.error();
                    } else {
                        size_t coef_num = std::min(64, 1 << (4 + (static_cast<int>(size_id) << 1)));
                        if (size_id > 1) {
                            auto dc = reader.read_se(); if (!dc) return dc.error();
                        }
                        for (size_t c = 0; c < coef_num; ++c) {
                            auto delta = reader.read_se(); if (!delta) return delta.error();
                        }
                    }
                }
            }
        }
    }

    auto amp = reader.read_bit(); if (!amp) return amp.error();
    auto sao = reader.read_bit(); if (!sao) return sao.error();
    auto pcm = reader.read_bit(); if (!pcm) return pcm.error();
    if (*pcm != 0) {
        auto sk = reader.skip_bits(8); if (!sk) return sk.error();
        auto min_pcm = reader.read_ue(); if (!min_pcm) return min_pcm.error();
        auto diff_pcm = reader.read_ue(); if (!diff_pcm) return diff_pcm.error();
        auto loop_dis = reader.read_bit(); if (!loop_dis) return loop_dis.error();
    }

    auto num_st = reader.read_ue(); if (!num_st) return num_st.error();
    // Parse short-term reference picture sets
    for (uint32_t st_idx = 0; st_idx < *num_st; ++st_idx) {
        bool inter_rps = false;
        if (st_idx != 0) {
            auto irps = reader.read_bit(); if (!irps) return irps.error();
            inter_rps = (*irps != 0);
        }
        if (inter_rps) {
            auto delta_sign = reader.read_bit(); if (!delta_sign) return delta_sign.error();
            auto delta_abs = reader.read_ue(); if (!delta_abs) return delta_abs.error();
            // Simplified skip: in typical streams, we skip through bits
            break;
        } else {
            auto num_neg = reader.read_ue(); if (!num_neg) return num_neg.error();
            auto num_pos = reader.read_ue(); if (!num_pos) return num_pos.error();
            for (uint32_t i = 0; i < *num_neg; ++i) {
                auto d = reader.read_ue(); if (!d) return d.error();
                auto u = reader.read_bit(); if (!u) return u.error();
            }
            for (uint32_t i = 0; i < *num_pos; ++i) {
                auto d = reader.read_ue(); if (!d) return d.error();
                auto u = reader.read_bit(); if (!u) return u.error();
            }
        }
    }

    if (reader.has_bits(1)) {
        auto lt_present = reader.read_bit(); if (!lt_present) return lt_present.error();
        if (*lt_present != 0) {
            auto num_lt = reader.read_ue(); if (!num_lt) return num_lt.error();
            for (uint32_t i = 0; i < *num_lt; ++i) {
                auto sk = reader.skip_bits(sps.log2_max_pic_order_cnt_lsb_minus4 + 4 + 1);
                if (!sk) return sk.error();
            }
        }
    }

    if (reader.has_bits(2)) {
        auto tmvp = reader.read_bit(); if (!tmvp) return tmvp.error();
        auto intra_smooth = reader.read_bit(); if (!intra_smooth) return intra_smooth.error();
    }

    if (reader.has_bits(1)) {
        auto vui_flag = reader.read_bit(); if (!vui_flag) return vui_flag.error();
        sps.vui_parameters_present_flag = (*vui_flag != 0);

        if (sps.vui_parameters_present_flag) {
            auto ar_present = reader.read_bit(); if (!ar_present) return ar_present.error();
            sps.vui.aspect_ratio_info_present_flag = (*ar_present != 0);
            if (sps.vui.aspect_ratio_info_present_flag) {
                auto ar_idc = reader.read_bits(8); if (!ar_idc) return ar_idc.error();
                sps.vui.aspect_ratio_idc = static_cast<uint8_t>(*ar_idc);
                if (sps.vui.aspect_ratio_idc == 255) {
                    auto sar_w = reader.read_bits(16); if (!sar_w) return sar_w.error(); sps.vui.sar_width = static_cast<uint16_t>(*sar_w);
                    auto sar_h = reader.read_bits(16); if (!sar_h) return sar_h.error(); sps.vui.sar_height = static_cast<uint16_t>(*sar_h);
                }
            }

            auto overscan = reader.read_bit(); if (!overscan) return overscan.error();
            if (*overscan != 0) {
                auto o_app = reader.read_bit(); if (!o_app) return o_app.error();
            }

            auto vid_sig = reader.read_bit(); if (!vid_sig) return vid_sig.error();
            sps.vui.video_signal_type_present_flag = (*vid_sig != 0);
            if (sps.vui.video_signal_type_present_flag) {
                auto v_fmt = reader.read_bits(3); if (!v_fmt) return v_fmt.error(); sps.vui.video_format = static_cast<uint8_t>(*v_fmt);
                auto full_rng = reader.read_bit(); if (!full_rng) return full_rng.error(); sps.vui.video_full_range_flag = (*full_rng != 0);
                auto col_desc = reader.read_bit(); if (!col_desc) return col_desc.error(); sps.vui.colour_description_present_flag = (*col_desc != 0);
                if (sps.vui.colour_description_present_flag) {
                    auto prim = reader.read_bits(8); if (!prim) return prim.error(); sps.vui.colour_primaries = static_cast<uint8_t>(*prim);
                    auto trans = reader.read_bits(8); if (!trans) return trans.error(); sps.vui.transfer_characteristics = static_cast<uint8_t>(*trans);
                    auto mat = reader.read_bits(8); if (!mat) return mat.error(); sps.vui.matrix_coeffs = static_cast<uint8_t>(*mat);
                }
            }

            auto chroma_loc = reader.read_bit(); if (!chroma_loc) return chroma_loc.error();
            if (*chroma_loc != 0) {
                auto c1 = reader.read_ue(); if (!c1) return c1.error();
                auto c2 = reader.read_ue(); if (!c2) return c2.error();
            }

            auto neut = reader.read_bit(); if (!neut) return neut.error();
            auto field = reader.read_bit(); if (!field) return field.error();
            auto frame_field = reader.read_bit(); if (!frame_field) return frame_field.error();

            auto timing = reader.read_bit(); if (!timing) return timing.error();
            sps.vui.vui_timing_info_present_flag = (*timing != 0);
            if (sps.vui.vui_timing_info_present_flag) {
                auto units = reader.read_bits(32); if (!units) return units.error(); sps.vui.vui_num_units_in_tick = *units;
                auto scale = reader.read_bits(32); if (!scale) return scale.error(); sps.vui.vui_time_scale = *scale;
            }
        }
    }

    return sps;
}

MediaResult<HevcPps> HevcPps::parse_rbsp(std::span<const uint8_t> rbsp) noexcept {
    BitReader reader(rbsp);
    return parse(reader);
}

MediaResult<HevcPps> HevcPps::parse(BitReader& reader) noexcept {
    HevcPps pps{};

    auto pps_id = reader.read_ue(); if (!pps_id) return pps_id.error(); pps.pps_pic_parameter_set_id = *pps_id;
    auto sps_id = reader.read_ue(); if (!sps_id) return sps_id.error(); pps.pps_seq_parameter_set_id = *sps_id;

    auto dep_slice = reader.read_bit(); if (!dep_slice) return dep_slice.error(); pps.dependent_slice_segments_enabled_flag = (*dep_slice != 0);
    auto out_flag = reader.read_bit(); if (!out_flag) return out_flag.error(); pps.output_flag_present_flag = (*out_flag != 0);
    auto extra_bits = reader.read_bits(3); if (!extra_bits) return extra_bits.error(); pps.num_extra_slice_header_bits = static_cast<uint8_t>(*extra_bits);

    auto sdh = reader.read_bit(); if (!sdh) return sdh.error(); pps.sign_data_hiding_enabled_flag = (*sdh != 0);
    auto cabac_init = reader.read_bit(); if (!cabac_init) return cabac_init.error(); pps.cabac_init_present_flag = (*cabac_init != 0);

    auto ref_l0 = reader.read_ue(); if (!ref_l0) return ref_l0.error(); pps.num_ref_idx_l0_default_active_minus1 = *ref_l0;
    auto ref_l1 = reader.read_ue(); if (!ref_l1) return ref_l1.error(); pps.num_ref_idx_l1_default_active_minus1 = *ref_l1;

    auto qp = reader.read_se(); if (!qp) return qp.error(); pps.init_qp_minus26 = *qp;

    auto constr = reader.read_bit(); if (!constr) return constr.error(); pps.constrained_intra_pred_flag = (*constr != 0);
    auto trans_skip = reader.read_bit(); if (!trans_skip) return trans_skip.error(); pps.transform_skip_enabled_flag = (*trans_skip != 0);

    auto cu_qp = reader.read_bit(); if (!cu_qp) return cu_qp.error(); pps.cu_qp_delta_enabled_flag = (*cu_qp != 0);
    if (pps.cu_qp_delta_enabled_flag) {
        auto depth = reader.read_ue(); if (!depth) return depth.error();
    }

    auto cb_qp = reader.read_se(); if (!cb_qp) return cb_qp.error(); pps.pps_cb_qp_offset = *cb_qp;
    auto cr_qp = reader.read_se(); if (!cr_qp) return cr_qp.error(); pps.pps_cr_qp_offset = *cr_qp;
    auto chroma_offsets = reader.read_bit(); if (!chroma_offsets) return chroma_offsets.error(); pps.pps_slice_chroma_qp_offsets_present_flag = (*chroma_offsets != 0);

    auto wp = reader.read_bit(); if (!wp) return wp.error(); pps.weighted_pred_flag = (*wp != 0);
    auto wbp = reader.read_bit(); if (!wbp) return wbp.error(); pps.weighted_bipred_flag = (*wbp != 0);
    auto tq_by = reader.read_bit(); if (!tq_by) return tq_by.error(); pps.transquant_bypass_enabled_flag = (*tq_by != 0);
    auto tiles = reader.read_bit(); if (!tiles) return tiles.error(); pps.tiles_enabled_flag = (*tiles != 0);
    auto wpp = reader.read_bit(); if (!wpp) return wpp.error(); pps.entropy_coding_sync_enabled_flag = (*wpp != 0);

    return pps;
}

} // namespace atum

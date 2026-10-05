#include "atum/codec/video/av1/av1_obu.hpp"

namespace atum {

MediaResult<std::pair<Av1ObuHeader, size_t>> Av1ObuHeader::parse(std::span<const uint8_t> data) noexcept {
    if (data.empty()) return MediaError::NeedMoreData;

    Av1ObuHeader hdr{};
    uint8_t byte0 = data[0];
    hdr.obu_forbidden_bit = ((byte0 >> 7) & 1) != 0;
    if (hdr.obu_forbidden_bit) return MediaError::InvalidData;

    hdr.obu_type = static_cast<Av1ObuType>((byte0 >> 3) & 0x0F);
    hdr.obu_extension_flag = ((byte0 >> 2) & 1) != 0;
    hdr.obu_has_size_field = ((byte0 >> 1) & 1) != 0;

    size_t consumed = 1;
    if (hdr.obu_extension_flag) {
        if (data.size() < 2) return MediaError::NeedMoreData;
        uint8_t byte1 = data[1];
        hdr.temporal_id = (byte1 >> 5) & 0x07;
        hdr.spatial_id = (byte1 >> 3) & 0x03;
        consumed = 2;
    }

    return std::make_pair(hdr, consumed);
}

MediaResult<Av1ColorConfig> Av1ColorConfig::parse(BitReader& reader, Av1Profile profile) noexcept {
    Av1ColorConfig cfg{};

    auto high_bd = reader.read_bit(); if (!high_bd) return high_bd.error();
    cfg.high_bitdepth = (*high_bd != 0);

    if (profile == Av1Profile::Professional && cfg.high_bitdepth) {
        auto twelve = reader.read_bit(); if (!twelve) return twelve.error();
        cfg.twelve_bit = (*twelve != 0);
        cfg.bit_depth = cfg.twelve_bit ? 12 : 10;
    } else {
        cfg.bit_depth = cfg.high_bitdepth ? 10 : 8;
    }

    if (profile == Av1Profile::High) {
        cfg.mono_chrome = false;
    } else {
        auto mono = reader.read_bit(); if (!mono) return mono.error();
        cfg.mono_chrome = (*mono != 0);
    }

    auto col_desc = reader.read_bit(); if (!col_desc) return col_desc.error();
    cfg.color_description_present_flag = (*col_desc != 0);

    if (cfg.color_description_present_flag) {
        auto prim = reader.read_bits(8); if (!prim) return prim.error(); cfg.color_primaries = static_cast<uint8_t>(*prim);
        auto trans = reader.read_bits(8); if (!trans) return trans.error(); cfg.transfer_characteristics = static_cast<uint8_t>(*trans);
        auto mat = reader.read_bits(8); if (!mat) return mat.error(); cfg.matrix_coefficients = static_cast<uint8_t>(*mat);
    }

    if (cfg.mono_chrome) {
        auto full_rng = reader.read_bit(); if (!full_rng) return full_rng.error();
        cfg.color_range_full = (*full_rng != 0);
        cfg.subsampling_x = 1;
        cfg.subsampling_y = 1;
        return cfg;
    }

    if (cfg.color_description_present_flag &&
        cfg.color_primaries == 1 && cfg.transfer_characteristics == 13 && cfg.matrix_coefficients == 0) {
        cfg.color_range_full = true;
        if (profile == Av1Profile::High || (profile == Av1Profile::Professional && cfg.bit_depth == 12)) {
            cfg.subsampling_x = 0;
            cfg.subsampling_y = 0;
        }
    } else {
        auto full_rng = reader.read_bit(); if (!full_rng) return full_rng.error();
        cfg.color_range_full = (*full_rng != 0);

        if (profile == Av1Profile::Main) {
            cfg.subsampling_x = 1;
            cfg.subsampling_y = 1;
        } else if (profile == Av1Profile::High) {
            cfg.subsampling_x = 0;
            cfg.subsampling_y = 0;
        } else {
            if (cfg.bit_depth == 12) {
                auto sub_x = reader.read_bit(); if (!sub_x) return sub_x.error(); cfg.subsampling_x = static_cast<uint8_t>(*sub_x);
                if (cfg.subsampling_x) {
                    auto sub_y = reader.read_bit(); if (!sub_y) return sub_y.error(); cfg.subsampling_y = static_cast<uint8_t>(*sub_y);
                } else {
                    cfg.subsampling_y = 0;
                }
            } else {
                cfg.subsampling_x = 1;
                cfg.subsampling_y = 0;
            }
        }
        if (cfg.subsampling_x && cfg.subsampling_y) {
            auto csp = reader.read_bits(2); if (!csp) return csp.error();
            cfg.chroma_sample_position = static_cast<uint8_t>(*csp);
        }
    }

    auto sep_uv = reader.read_bit(); if (!sep_uv) return sep_uv.error();
    cfg.separate_uv_delta_q = (*sep_uv != 0);

    return cfg;
}

MediaResult<Av1SequenceHeader> Av1SequenceHeader::parse_payload(std::span<const uint8_t> payload) noexcept {
    BitReader reader(payload);
    return parse(reader);
}

MediaResult<Av1SequenceHeader> Av1SequenceHeader::parse(BitReader& reader) noexcept {
    Av1SequenceHeader seq{};

    auto prof = reader.read_bits(3); if (!prof) return prof.error();
    seq.seq_profile = static_cast<Av1Profile>(*prof);

    auto still = reader.read_bit(); if (!still) return still.error();
    seq.still_picture = (*still != 0);

    auto red_hdr = reader.read_bit(); if (!red_hdr) return red_hdr.error();
    seq.reduced_still_picture_header = (*red_hdr != 0);

    if (seq.reduced_still_picture_header) {
        auto lvl = reader.read_bits(5); if (!lvl) return lvl.error();
    } else {
        auto timing = reader.read_bit(); if (!timing) return timing.error();
        seq.timing_info_present_flag = (*timing != 0);
        if (seq.timing_info_present_flag) {
            auto num_u = reader.read_bits(32); if (!num_u) return num_u.error(); seq.num_units_in_display_tick = *num_u;
            auto scale = reader.read_bits(32); if (!scale) return scale.error(); seq.time_scale = *scale;
            auto eq_pic = reader.read_bit(); if (!eq_pic) return eq_pic.error();
            if (*eq_pic != 0) {
                auto ue = reader.read_ue(); if (!ue) return ue.error();
            }
            auto dec_model = reader.read_bit(); if (!dec_model) return dec_model.error();
        }

        auto init_delay = reader.read_bit(); if (!init_delay) return init_delay.error();
        auto op_cnt = reader.read_bits(5); if (!op_cnt) return op_cnt.error();
        for (uint32_t i = 0; i <= *op_cnt; ++i) {
            auto op_idc = reader.read_bits(12); if (!op_idc) return op_idc.error();
            auto seq_lvl = reader.read_bits(5); if (!seq_lvl) return seq_lvl.error();
            if (*seq_lvl > 7) {
                auto tier = reader.read_bit(); if (!tier) return tier.error();
            }
        }
    }

    auto w_bits = reader.read_bits(4); if (!w_bits) return w_bits.error(); seq.frame_width_bits_minus_1 = static_cast<uint8_t>(*w_bits);
    auto h_bits = reader.read_bits(4); if (!h_bits) return h_bits.error(); seq.frame_height_bits_minus_1 = static_cast<uint8_t>(*h_bits);

    auto max_w = reader.read_bits(seq.frame_width_bits_minus_1 + 1); if (!max_w) return max_w.error();
    seq.max_frame_width = *max_w + 1;

    auto max_h = reader.read_bits(seq.frame_height_bits_minus_1 + 1); if (!max_h) return max_h.error();
    seq.max_frame_height = *max_h + 1;

    if (!seq.reduced_still_picture_header) {
        auto frame_id = reader.read_bit(); if (!frame_id) return frame_id.error();
        if (*frame_id != 0) {
            auto sk = reader.skip_bits(4 + 3); if (!sk) return sk.error();
        }
    }

    auto sb128 = reader.read_bit(); if (!sb128) return sb128.error(); seq.use_128x128_superblock = (*sb128 != 0);
    auto f_intra = reader.read_bit(); if (!f_intra) return f_intra.error();
    auto e_intra = reader.read_bit(); if (!e_intra) return e_intra.error();

    if (!seq.reduced_still_picture_header) {
        auto comp1 = reader.read_bit(); if (!comp1) return comp1.error();
        auto comp2 = reader.read_bit(); if (!comp2) return comp2.error();
        auto warped = reader.read_bit(); if (!warped) return warped.error();
        auto dual_f = reader.read_bit(); if (!dual_f) return dual_f.error();
        auto ord_h = reader.read_bit(); if (!ord_h) return ord_h.error();
        if (*ord_h != 0) {
            auto jnt = reader.read_bit(); if (!jnt) return jnt.error();
            auto ref_mv = reader.read_bit(); if (!ref_mv) return ref_mv.error();
        }
        auto scr = reader.read_bit(); if (!scr) return scr.error();
        if (*scr == 0) {
            auto scr_f = reader.read_bit(); if (!scr_f) return scr_f.error();
        }
        auto ord_b = reader.read_bits(3); if (!ord_b) return ord_b.error();
    }

    auto sup_res = reader.read_bit(); if (!sup_res) return sup_res.error();
    auto cdef = reader.read_bit(); if (!cdef) return cdef.error(); seq.enable_cdef = (*cdef != 0);
    auto rest = reader.read_bit(); if (!rest) return rest.error(); seq.enable_restoration = (*rest != 0);

    auto col_res = Av1ColorConfig::parse(reader, seq.seq_profile);
    if (!col_res) return col_res.error();
    seq.color_config = *col_res;

    auto film_g = reader.read_bit(); if (!film_g) return film_g.error(); seq.film_grain_params_present = (*film_g != 0);

    return seq;
}

MediaResult<Av1FrameHeader> Av1FrameHeader::parse_payload(std::span<const uint8_t> payload, const Av1SequenceHeader& seq) noexcept {
    BitReader reader(payload);
    return parse(reader, seq);
}

MediaResult<Av1FrameHeader> Av1FrameHeader::parse(BitReader& reader, const Av1SequenceHeader& seq) noexcept {
    Av1FrameHeader hdr{};

    if (seq.reduced_still_picture_header) {
        hdr.show_existing_frame = false;
        hdr.frame_type = Av1FrameType::KeyFrame;
        hdr.show_frame = true;
        hdr.frame_width = seq.max_frame_width;
        hdr.frame_height = seq.max_frame_height;
        hdr.render_width = seq.max_frame_width;
        hdr.render_height = seq.max_frame_height;
        return hdr;
    }

    auto show_exist = reader.read_bit(); if (!show_exist) return show_exist.error();
    hdr.show_existing_frame = (*show_exist != 0);

    if (hdr.show_existing_frame) {
        auto idx = reader.read_bits(3); if (!idx) return idx.error();
        hdr.frame_to_show_map_idx = static_cast<uint8_t>(*idx);
        hdr.frame_width = seq.max_frame_width;
        hdr.frame_height = seq.max_frame_height;
        hdr.render_width = seq.max_frame_width;
        hdr.render_height = seq.max_frame_height;
        return hdr;
    }

    auto f_type = reader.read_bits(2); if (!f_type) return f_type.error();
    hdr.frame_type = static_cast<Av1FrameType>(*f_type);

    auto show_f = reader.read_bit(); if (!show_f) return show_f.error();
    hdr.show_frame = (*show_f != 0);

    if (!hdr.show_frame) {
        auto showable = reader.read_bit(); if (!showable) return showable.error();
        hdr.showable_frame = (*showable != 0);
    }

    if (hdr.frame_type == Av1FrameType::SwitchFrame ||
        (hdr.frame_type == Av1FrameType::KeyFrame && hdr.show_frame)) {
        hdr.error_resilient_mode = true;
    } else {
        auto err_res = reader.read_bit(); if (!err_res) return err_res.error();
        hdr.error_resilient_mode = (*err_res != 0);
    }

    auto dis_cdf = reader.read_bit(); if (!dis_cdf) return dis_cdf.error();
    hdr.disable_cdf_update = (*dis_cdf != 0);

    // Frame size
    if (hdr.frame_type == Av1FrameType::KeyFrame) {
        auto override_size = reader.read_bit(); if (!override_size) return override_size.error();
        if (*override_size != 0) {
            auto w = reader.read_bits(seq.frame_width_bits_minus_1 + 1); if (!w) return w.error(); hdr.frame_width = *w + 1;
            auto h = reader.read_bits(seq.frame_height_bits_minus_1 + 1); if (!h) return h.error(); hdr.frame_height = *h + 1;
        } else {
            hdr.frame_width = seq.max_frame_width;
            hdr.frame_height = seq.max_frame_height;
        }
        auto render_diff = reader.read_bit(); if (!render_diff) return render_diff.error();
        if (*render_diff != 0) {
            auto rw = reader.read_bits(16); if (!rw) return rw.error(); hdr.render_width = *rw + 1;
            auto rh = reader.read_bits(16); if (!rh) return rh.error(); hdr.render_height = *rh + 1;
        } else {
            hdr.render_width = hdr.frame_width;
            hdr.render_height = hdr.frame_height;
        }
    } else {
        hdr.frame_width = seq.max_frame_width;
        hdr.frame_height = seq.max_frame_height;
        hdr.render_width = seq.max_frame_width;
        hdr.render_height = seq.max_frame_height;
    }

    return hdr;
}

} // namespace atum

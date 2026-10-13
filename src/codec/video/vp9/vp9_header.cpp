#include "atum/codec/video/vp9/vp9_header.hpp"

namespace atum {

MediaResult<Vp9UncompressedHeader> Vp9UncompressedHeader::parse_frame(
    std::span<const uint8_t> frame_bytes, uint32_t ref_w, uint32_t ref_h) noexcept {
    BitReader reader(frame_bytes);
    return parse(reader, ref_w, ref_h);
}

MediaResult<Vp9UncompressedHeader> Vp9UncompressedHeader::parse(
    BitReader& reader, uint32_t ref_w, uint32_t ref_h) noexcept {
    Vp9UncompressedHeader hdr{};

    auto marker = reader.read_bits(2);
    if (!marker) return marker.error();
    if (*marker != 2) return MediaError::InvalidData; // Must be 0b10
    hdr.frame_marker = 2;

    auto p_low = reader.read_bit(); if (!p_low) return p_low.error();
    auto p_high = reader.read_bit(); if (!p_high) return p_high.error();
    uint8_t prof = static_cast<uint8_t>((*p_high << 1) | *p_low);
    if (prof == 3) {
        auto res_bit = reader.read_bit();
        if (!res_bit) return res_bit.error();
    }
    hdr.profile = static_cast<Vp9Profile>(prof);

    auto show_exist = reader.read_bit(); if (!show_exist) return show_exist.error();
    hdr.show_existing_frame = (*show_exist != 0);

    if (hdr.show_existing_frame) {
        auto idx = reader.read_bits(3); if (!idx) return idx.error();
        hdr.frame_to_show_map_idx = static_cast<uint8_t>(*idx);
        return hdr;
    }

    auto f_type = reader.read_bit(); if (!f_type) return f_type.error();
    hdr.frame_type = (*f_type == 0) ? Vp9FrameType::KeyFrame : Vp9FrameType::InterFrame;

    auto show_f = reader.read_bit(); if (!show_f) return show_f.error();
    hdr.show_frame = (*show_f != 0);

    auto err_res = reader.read_bit(); if (!err_res) return err_res.error();
    hdr.error_resilient_mode = (*err_res != 0);

    auto parse_color_config = [&]() -> MediaResult<void> {
        if (static_cast<uint8_t>(hdr.profile) >= 2) {
            auto ten_twelve = reader.read_bit(); if (!ten_twelve) return ten_twelve.error();
            hdr.bit_depth = (*ten_twelve != 0) ? 12 : 10;
        } else {
            hdr.bit_depth = 8;
        }

        auto cs = reader.read_bits(3); if (!cs) return cs.error();
        hdr.color_space = static_cast<Vp9ColorSpace>(*cs);

        if (hdr.color_space != Vp9ColorSpace::SRGB) {
            auto cr = reader.read_bit(); if (!cr) return cr.error();
            hdr.color_range_full = (*cr != 0);

            if (hdr.profile == Vp9Profile::Profile1 || hdr.profile == Vp9Profile::Profile3) {
                auto sub_x = reader.read_bit(); if (!sub_x) return sub_x.error();
                hdr.subsampling_x = static_cast<uint8_t>(*sub_x);
                auto sub_y = reader.read_bit(); if (!sub_y) return sub_y.error();
                hdr.subsampling_y = static_cast<uint8_t>(*sub_y);
                auto res_bit = reader.read_bit(); if (!res_bit) return res_bit.error();
            } else {
                hdr.subsampling_x = 1;
                hdr.subsampling_y = 1;
            }
        } else {
            hdr.color_range_full = true;
            if (hdr.profile == Vp9Profile::Profile1 || hdr.profile == Vp9Profile::Profile3) {
                hdr.subsampling_x = 0;
                hdr.subsampling_y = 0;
                auto res_bit = reader.read_bit(); if (!res_bit) return res_bit.error();
            }
        }
        return {};
    };

    auto parse_frame_size = [&]() -> MediaResult<void> {
        auto w_m1 = reader.read_bits(16); if (!w_m1) return w_m1.error(); hdr.width = *w_m1 + 1;
        auto h_m1 = reader.read_bits(16); if (!h_m1) return h_m1.error(); hdr.height = *h_m1 + 1;
        return {};
    };

    auto parse_render_size = [&]() -> MediaResult<void> {
        auto render_diff = reader.read_bit(); if (!render_diff) return render_diff.error();
        if (*render_diff != 0) {
            auto rw = reader.read_bits(16); if (!rw) return rw.error(); hdr.render_width = *rw + 1;
            auto rh = reader.read_bits(16); if (!rh) return rh.error(); hdr.render_height = *rh + 1;
        } else {
            hdr.render_width = hdr.width;
            hdr.render_height = hdr.height;
        }
        return {};
    };

    if (hdr.frame_type == Vp9FrameType::KeyFrame) {
        auto b0 = reader.read_bits(8); if (!b0) return b0.error();
        auto b1 = reader.read_bits(8); if (!b1) return b1.error();
        auto b2 = reader.read_bits(8); if (!b2) return b2.error();
        if (*b0 != VP9_SYNC_CODE[0] || *b1 != VP9_SYNC_CODE[1] || *b2 != VP9_SYNC_CODE[2]) {
            return MediaError::InvalidData;
        }

        auto c_res = parse_color_config(); if (!c_res) return c_res.error();
        auto s_res = parse_frame_size(); if (!s_res) return s_res.error();
        auto r_res = parse_render_size(); if (!r_res) return r_res.error();

        hdr.refresh_frame_flags = 0xFF;
    } else {
        if (!hdr.show_frame) {
            auto intra = reader.read_bit(); if (!intra) return intra.error();
            hdr.intra_only = (*intra != 0);
        } else {
            hdr.intra_only = false;
        }

        if (!hdr.error_resilient_mode) {
            auto rst = reader.read_bits(2); if (!rst) return rst.error();
            hdr.reset_frame_context = static_cast<uint8_t>(*rst);
        } else {
            hdr.reset_frame_context = 0;
        }

        if (hdr.intra_only) {
            auto b0 = reader.read_bits(8); if (!b0) return b0.error();
            auto b1 = reader.read_bits(8); if (!b1) return b1.error();
            auto b2 = reader.read_bits(8); if (!b2) return b2.error();
            if (*b0 != VP9_SYNC_CODE[0] || *b1 != VP9_SYNC_CODE[1] || *b2 != VP9_SYNC_CODE[2]) {
                return MediaError::InvalidData;
            }

            if (static_cast<uint8_t>(hdr.profile) > 0) {
                auto c_res = parse_color_config(); if (!c_res) return c_res.error();
            } else {
                hdr.color_space = Vp9ColorSpace::BT709;
                hdr.subsampling_x = 1;
                hdr.subsampling_y = 1;
                hdr.bit_depth = 8;
            }

            auto ref_flags = reader.read_bits(8); if (!ref_flags) return ref_flags.error();
            hdr.refresh_frame_flags = static_cast<uint8_t>(*ref_flags);

            auto s_res = parse_frame_size(); if (!s_res) return s_res.error();
            auto r_res = parse_render_size(); if (!r_res) return r_res.error();
        } else {
            auto ref_flags = reader.read_bits(8); if (!ref_flags) return ref_flags.error();
            hdr.refresh_frame_flags = static_cast<uint8_t>(*ref_flags);

            for (size_t i = 0; i < 3; ++i) {
                auto r_idx = reader.read_bits(3); if (!r_idx) return r_idx.error();
                hdr.ref_frame_indices[i] = static_cast<uint8_t>(*r_idx);
                auto sign_bias = reader.read_bit(); if (!sign_bias) return sign_bias.error();
                hdr.ref_frame_sign_bias[i] = static_cast<uint8_t>(*sign_bias);
            }

            // frame_size_with_refs
            bool found_ref = false;
            for (size_t i = 0; i < 3; ++i) {
                auto f_bit = reader.read_bit(); if (!f_bit) return f_bit.error();
                if (*f_bit != 0) {
                    found_ref = true;
                    break;
                }
            }

            if (!found_ref) {
                auto s_res = parse_frame_size(); if (!s_res) return s_res.error();
            } else {
                hdr.width = ref_w;
                hdr.height = ref_h;
            }
            auto r_res = parse_render_size(); if (!r_res) return r_res.error();

            auto hp_mv = reader.read_bit(); if (!hp_mv) return hp_mv.error();
            hdr.allow_high_precision_mv = (*hp_mv != 0);

            // interpolation_filter
            auto switchable = reader.read_bit(); if (!switchable) return switchable.error();
            if (*switchable != 0) {
                hdr.mcomp_filter_type = 4; // VA-API switchable filter value is 4
            } else {
                auto raw_filt = reader.read_bits(2); if (!raw_filt) return raw_filt.error();
                // Map to VA-API: 0 (smooth) -> 1, 1 (eighttap) -> 0, 2 (sharp) -> 2
                uint8_t rf = static_cast<uint8_t>(*raw_filt);
                hdr.mcomp_filter_type = (rf == 0) ? 1 : ((rf == 1) ? 0 : 2);
            }
        }
    }

    // Common context and update flags (Applies to both KeyFrames and InterFrames)
    if (!hdr.error_resilient_mode) {
        auto ref_ctx = reader.read_bit(); if (!ref_ctx) return ref_ctx.error();
        hdr.refresh_frame_context = (*ref_ctx != 0);
        auto par_mode = reader.read_bit(); if (!par_mode) return par_mode.error();
        hdr.frame_parallel_decoding_mode = (*par_mode != 0);
    } else {
        hdr.refresh_frame_context = false;
        hdr.frame_parallel_decoding_mode = true;
    }

    auto ctx_idx = reader.read_bits(2); if (!ctx_idx) return ctx_idx.error();
    hdr.frame_context_idx = static_cast<uint8_t>(*ctx_idx);

    // Loop filter
    auto fl = reader.read_bits(6); if (!fl) return fl.error();
    hdr.filter_level = static_cast<uint8_t>(*fl);
    auto sl = reader.read_bits(3); if (!sl) return sl.error();
    hdr.sharpness_level = static_cast<uint8_t>(*sl);

    auto mode_ref_delta_en = reader.read_bit(); if (!mode_ref_delta_en) return mode_ref_delta_en.error();
    if (*mode_ref_delta_en != 0) {
        auto delta_update = reader.read_bit(); if (!delta_update) return delta_update.error();
        if (*delta_update != 0) {
            for (int i = 0; i < 4; ++i) {
                auto upd = reader.read_bit(); if (!upd) return upd.error();
                if (*upd != 0) { reader.read_bits(6); reader.read_bit(); }
            }
            for (int i = 0; i < 2; ++i) {
                auto upd = reader.read_bit(); if (!upd) return upd.error();
                if (*upd != 0) { reader.read_bits(6); reader.read_bit(); }
            }
        }
    }

    // Quantization
    auto qp = reader.read_bits(8); if (!qp) return qp.error();
    hdr.base_q_idx = static_cast<uint8_t>(*qp);
    for (int i = 0; i < 3; ++i) {
        if (!reader.has_bits(1)) break;
        auto d_flag = reader.read_bit(); if (!d_flag) break;
        if (*d_flag != 0) {
            auto d_val = reader.read_bits(4); if (!d_val) break;
            auto d_sign = reader.read_bit(); if (!d_sign) break;
        }
    }

    // Segmentation
    if (reader.has_bits(1)) {
        auto seg_en = reader.read_bit();
        if (seg_en) {
            hdr.segmentation_enabled = (*seg_en != 0);
            if (hdr.segmentation_enabled) {
                auto upd_map = reader.read_bit();
                if (upd_map) {
                    hdr.segmentation_update_map = (*upd_map != 0);
                    if (hdr.segmentation_update_map) {
                        for (int i = 0; i < 7; ++i) {
                            if (!reader.has_bits(1)) break;
                            auto upd_prob = reader.read_bit();
                            if (upd_prob && *upd_prob != 0) {
                                auto p = reader.read_bits(8);
                                if (p) hdr.mb_segment_tree_probs[i] = static_cast<uint8_t>(*p);
                            }
                        }
                        if (reader.has_bits(1)) {
                            auto temp_upd = reader.read_bit();
                            if (temp_upd) {
                                hdr.segmentation_temporal_update = (*temp_upd != 0);
                                for (int i = 0; i < 3; ++i) {
                                    if (hdr.segmentation_temporal_update) {
                                        if (!reader.has_bits(1)) break;
                                        auto upd_pred = reader.read_bit();
                                        if (upd_pred && *upd_pred != 0) {
                                            auto p = reader.read_bits(8);
                                            if (p) hdr.segment_pred_probs[i] = static_cast<uint8_t>(*p);
                                        }
                                    } else {
                                        hdr.segment_pred_probs[i] = 255;
                                    }
                                }
                            }
                        }
                    }
                }
                if (reader.has_bits(1)) {
                    auto upd_data = reader.read_bit();
                    if (upd_data && *upd_data != 0) {
                        auto abs_delta = reader.read_bit();
                        (void)abs_delta;
                        for (int i = 0; i < 8; ++i) {
                            for (int j = 0; j < 4; ++j) {
                                if (!reader.has_bits(1)) break;
                                auto feat_en = reader.read_bit();
                                if (feat_en && *feat_en != 0) {
                                    int bits = (j == 0) ? 8 : (j == 1 ? 6 : (j == 2 ? 2 : 0));
                                    if (bits > 0) {
                                        auto val = reader.read_bits(bits);
                                        (void)val;
                                        if (j != 2) reader.read_bit();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Tile info
    uint32_t active_w = hdr.width > 0 ? hdr.width : (ref_w > 0 ? ref_w : 1920);
    uint32_t sb64_cols = (active_w + 63) / 64;
    uint32_t min_log2_tile_cols = 0;
    while ((64u << min_log2_tile_cols) < sb64_cols) min_log2_tile_cols++;
    uint32_t max_log2_tile_cols = 0;
    while ((sb64_cols >> (max_log2_tile_cols + 1)) >= 4) max_log2_tile_cols++;

    uint32_t tile_cols = min_log2_tile_cols;
    while (tile_cols < max_log2_tile_cols && reader.has_bits(1)) {
        auto bit = reader.read_bit(); if (!bit) break;
        if (*bit != 0) tile_cols++; else break;
    }
    hdr.tile_cols_log2 = static_cast<uint8_t>(tile_cols);

    hdr.tile_rows_log2 = 0;
    if (reader.has_bits(1)) {
        auto tr1 = reader.read_bit();
        if (tr1 && *tr1 != 0) {
            hdr.tile_rows_log2 = 1;
            if (reader.has_bits(1)) {
                auto tr2 = reader.read_bit();
                if (tr2 && *tr2 != 0) hdr.tile_rows_log2 = 2;
            }
        }
    }

    // Compressed header size (16-bit header_size_in_bytes)
    if (reader.has_bits(16)) {
        auto first_part = reader.read_bits(16);
        if (first_part) hdr.compressed_header_size = static_cast<uint16_t>(*first_part);
    }

    hdr.uncompressed_header_size = static_cast<uint8_t>((reader.bits_read() + 7) / 8);

    return hdr;
}

} // namespace atum

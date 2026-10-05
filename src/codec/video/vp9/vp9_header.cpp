#include "atum/codec/video/vp9/vp9_header.hpp"

namespace atum {

MediaResult<Vp9UncompressedHeader> Vp9UncompressedHeader::parse_frame(std::span<const uint8_t> frame_bytes) noexcept {
    BitReader reader(frame_bytes);
    return parse(reader);
}

MediaResult<Vp9UncompressedHeader> Vp9UncompressedHeader::parse(BitReader& reader) noexcept {
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

    if (hdr.frame_type == Vp9FrameType::KeyFrame) {
        auto b0 = reader.read_bits(8); if (!b0) return b0.error();
        auto b1 = reader.read_bits(8); if (!b1) return b1.error();
        auto b2 = reader.read_bits(8); if (!b2) return b2.error();
        if (*b0 != VP9_SYNC_CODE[0] || *b1 != VP9_SYNC_CODE[1] || *b2 != VP9_SYNC_CODE[2]) {
            return MediaError::InvalidData;
        }

        // Color config
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
                auto sub_x = reader.read_bit(); if (!sub_x) return sub_x.error(); hdr.subsampling_x = static_cast<uint8_t>(*sub_x);
                auto sub_y = reader.read_bit(); if (!sub_y) return sub_y.error(); hdr.subsampling_y = static_cast<uint8_t>(*sub_y);
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

        // Frame size
        auto w_m1 = reader.read_bits(16); if (!w_m1) return w_m1.error(); hdr.width = *w_m1 + 1;
        auto h_m1 = reader.read_bits(16); if (!h_m1) return h_m1.error(); hdr.height = *h_m1 + 1;

        auto render_diff = reader.read_bit(); if (!render_diff) return render_diff.error();
        if (*render_diff != 0) {
            auto rw = reader.read_bits(16); if (!rw) return rw.error(); hdr.render_width = *rw + 1;
            auto rh = reader.read_bits(16); if (!rh) return rh.error(); hdr.render_height = *rh + 1;
        } else {
            hdr.render_width = hdr.width;
            hdr.render_height = hdr.height;
        }
    } else {
        if (!hdr.show_frame) {
            auto intra = reader.read_bit(); if (!intra) return intra.error();
            hdr.intra_only = (*intra != 0);
        }

        if (!hdr.error_resilient_mode) {
            auto rst = reader.read_bits(2); if (!rst) return rst.error();
        }

        if (hdr.intra_only) {
            auto b0 = reader.read_bits(8); if (!b0) return b0.error();
            auto b1 = reader.read_bits(8); if (!b1) return b1.error();
            auto b2 = reader.read_bits(8); if (!b2) return b2.error();
            if (*b0 != VP9_SYNC_CODE[0] || *b1 != VP9_SYNC_CODE[1] || *b2 != VP9_SYNC_CODE[2]) {
                return MediaError::InvalidData;
            }

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
                    auto sub_x = reader.read_bit(); if (!sub_x) return sub_x.error(); hdr.subsampling_x = static_cast<uint8_t>(*sub_x);
                    auto sub_y = reader.read_bit(); if (!sub_y) return sub_y.error(); hdr.subsampling_y = static_cast<uint8_t>(*sub_y);
                    auto res_bit = reader.read_bit(); if (!res_bit) return res_bit.error();
                } else {
                    hdr.subsampling_x = 1; hdr.subsampling_y = 1;
                }
            } else {
                hdr.color_range_full = true;
                if (hdr.profile == Vp9Profile::Profile1 || hdr.profile == Vp9Profile::Profile3) {
                    hdr.subsampling_x = 0; hdr.subsampling_y = 0;
                    auto res_bit = reader.read_bit(); if (!res_bit) return res_bit.error();
                }
            }

            auto ref_flags = reader.read_bits(8); if (!ref_flags) return ref_flags.error();
            hdr.refresh_frame_flags = static_cast<uint8_t>(*ref_flags);

            auto w_m1 = reader.read_bits(16); if (!w_m1) return w_m1.error(); hdr.width = *w_m1 + 1;
            auto h_m1 = reader.read_bits(16); if (!h_m1) return h_m1.error(); hdr.height = *h_m1 + 1;

            auto render_diff = reader.read_bit(); if (!render_diff) return render_diff.error();
            if (*render_diff != 0) {
                auto rw = reader.read_bits(16); if (!rw) return rw.error(); hdr.render_width = *rw + 1;
                auto rh = reader.read_bits(16); if (!rh) return rh.error(); hdr.render_height = *rh + 1;
            } else {
                hdr.render_width = hdr.width;
                hdr.render_height = hdr.height;
            }
        } else {
            auto ref_flags = reader.read_bits(8); if (!ref_flags) return ref_flags.error();
            hdr.refresh_frame_flags = static_cast<uint8_t>(*ref_flags);

            for (size_t i = 0; i < 3; ++i) {
                auto r_idx = reader.read_bits(3); if (!r_idx) return r_idx.error();
                auto sign_bias = reader.read_bit(); if (!sign_bias) return sign_bias.error();
            }

            // Frame size with refs flag
            auto found_ref = reader.read_bit();
            if (found_ref && *found_ref == 0) {
                auto w_m1 = reader.read_bits(16); if (!w_m1) return w_m1.error(); hdr.width = *w_m1 + 1;
                auto h_m1 = reader.read_bits(16); if (!h_m1) return h_m1.error(); hdr.height = *h_m1 + 1;
                auto render_diff = reader.read_bit(); if (!render_diff) return render_diff.error();
                if (*render_diff != 0) {
                    auto rw = reader.read_bits(16); if (!rw) return rw.error(); hdr.render_width = *rw + 1;
                    auto rh = reader.read_bits(16); if (!rh) return rh.error(); hdr.render_height = *rh + 1;
                } else {
                    hdr.render_width = hdr.width;
                    hdr.render_height = hdr.height;
                }
            }
        }
    }

    // Loop filter
    if (reader.has_bits(6 + 3)) {
        auto fl = reader.read_bits(6); if (fl) hdr.filter_level = static_cast<uint8_t>(*fl);
        auto sl = reader.read_bits(3); if (sl) hdr.sharpness_level = static_cast<uint8_t>(*sl);
    }

    // Quantization
    if (reader.has_bits(8)) {
        auto qp = reader.read_bits(8); if (qp) hdr.base_q_idx = static_cast<uint8_t>(*qp);
    }

    return hdr;
}

} // namespace atum

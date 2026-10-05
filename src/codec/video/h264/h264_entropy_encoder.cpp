#include "atum/codec/video/h264/h264_entropy_encoder.hpp"
#include <algorithm>
#include <cstdlib>

namespace atum {

namespace {

constexpr uint8_t zigzag4x4[16] = {
    0,  1,  4,  8,
    5,  2,  3,  6,
    9, 12, 13, 10,
    7, 11, 14, 15
};

// H.264 Table 9-44: State transition table trans_mps
constexpr uint8_t trans_mps[64] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 62, 63
};

// H.264 Table 9-44: State transition table trans_lps
constexpr uint8_t trans_lps[64] = {
    0, 0, 1, 2, 2, 4, 4, 5, 6, 7, 8, 9, 9, 11, 11, 12,
    13, 13, 15, 15, 16, 16, 18, 18, 19, 19, 21, 21, 22, 22, 23, 24,
    24, 25, 26, 26, 27, 27, 28, 29, 29, 30, 30, 30, 31, 32, 32, 33,
    33, 33, 34, 34, 35, 35, 35, 36, 36, 36, 37, 37, 37, 38, 38, 63
};

// H.264 Table 9-44: rangeTabLPS[64][4]
constexpr uint8_t range_tab_lps[64][4] = {
    { 128, 176, 208, 240 }, { 128, 167, 197, 227 }, { 128, 158, 187, 216 }, { 123, 150, 178, 205 },
    { 116, 142, 169, 195 }, { 111, 135, 160, 185 }, { 105, 128, 152, 175 }, { 100, 122, 144, 166 },
    {  95, 116, 137, 158 }, {  90, 110, 130, 150 }, {  85, 104, 123, 142 }, {  81,  99, 117, 135 },
    {  77,  94, 111, 128 }, {  73,  89, 105, 122 }, {  69,  85, 100, 116 }, {  66,  81,  96, 110 },
    {  62,  76,  91, 105 }, {  59,  73,  86,  99 }, {  56,  69,  81,  94 }, {  53,  65,  77,  89 },
    {  51,  62,  73,  85 }, {  48,  59,  69,  80 }, {  46,  56,  66,  76 }, {  43,  53,  63,  72 },
    {  41,  50,  59,  69 }, {  39,  48,  56,  65 }, {  37,  45,  54,  62 }, {  35,  43,  51,  59 },
    {  33,  41,  48,  56 }, {  32,  39,  46,  53 }, {  30,  37,  43,  50 }, {  29,  35,  41,  48 },
    {  27,  33,  39,  45 }, {  26,  31,  37,  43 }, {  24,  30,  35,  41 }, {  23,  28,  33,  39 },
    {  22,  27,  32,  37 }, {  21,  26,  30,  35 }, {  20,  24,  29,  33 }, {  19,  23,  27,  31 },
    {  18,  22,  26,  30 }, {  17,  21,  25,  28 }, {  16,  20,  23,  27 }, {  15,  19,  22,  25 },
    {  14,  18,  21,  24 }, {  14,  17,  20,  23 }, {  13,  16,  19,  22 }, {  12,  15,  18,  21 },
    {  12,  14,  17,  20 }, {  11,  14,  16,  19 }, {  11,  13,  15,  18 }, {  10,  12,  15,  17 },
    {  10,  12,  14,  16 }, {   9,  11,  13,  15 }, {   9,  11,  12,  14 }, {   8,  10,  12,  14 },
    {   8,   9,  11,  13 }, {   7,   9,  11,  12 }, {   7,   9,  10,  12 }, {   7,   8,  10,  11 },
    {   6,   8,   9,  11 }, {   6,   7,   9,  10 }, {   6,   7,   8,   9 }, {   2,   2,   2,   2 }
};

} // namespace

// =============================================================================
// H264CavlcEncoder Implementation
// =============================================================================

int32_t H264CavlcEncoder::encode_residual4x4(const int32_t* block, int32_t nC) {
    int32_t coeffs[16];
    int32_t total_coeff = 0;
    int32_t trailing_ones = 0;

    for (int i = 0; i < 16; ++i) {
        coeffs[i] = block[zigzag4x4[i]];
        if (coeffs[i] != 0) {
            total_coeff++;
        }
    }

    if (total_coeff == 0) {
        // Encode coeff_token for 0 coefficients
        if (nC < 2) {
            bw_.write_bit(1); // 1-bit code in Table 9-5 for (0, 0)
        } else if (nC < 4) {
            bw_.write_bits(0, 2); // '00'
        } else if (nC < 8) {
            bw_.write_bits(0, 2);
        } else {
            bw_.write_bits(0, 6); // 6 zeros
        }
        return 0;
    }

    // Find trailing ones (up to 3 non-zero values with magnitude 1 starting from high frequencies)
    int32_t non_zero_idx[16];
    int32_t nz_count = 0;
    for (int i = 0; i < 16; ++i) {
        if (coeffs[i] != 0) {
            non_zero_idx[nz_count++] = i;
        }
    }

    for (int i = nz_count - 1; i >= 0 && trailing_ones < 3; --i) {
        if (std::abs(coeffs[non_zero_idx[i]]) == 1) {
            trailing_ones++;
        } else {
            break;
        }
    }

    // Encode coeff_token
    if (nC >= 8) {
        uint32_t code = (static_cast<uint32_t>(total_coeff) << 2) | static_cast<uint32_t>(trailing_ones);
        bw_.write_bits(code, 6);
    } else {
        // Robust Exp-Golomb fallback for coeff_token
        bw_.write_ue((static_cast<uint32_t>(total_coeff) << 2) | static_cast<uint32_t>(trailing_ones));
    }

    // Trailing ones sign bits
    for (int i = 0; i < trailing_ones; ++i) {
        int32_t idx = non_zero_idx[nz_count - 1 - i];
        bw_.write_bit(coeffs[idx] < 0 ? 1 : 0);
    }

    // Remaining non-zero levels (reverse order)
    for (int i = nz_count - 1 - trailing_ones; i >= 0; --i) {
        int32_t idx = non_zero_idx[i];
        int32_t level = coeffs[idx];
        int32_t level_code = (level > 0) ? (level - 1) : (-level - 1);
        bw_.write_ue(static_cast<uint32_t>(level_code));
        bw_.write_bit(level < 0 ? 1 : 0);
    }

    // Total zeros
    int32_t total_zeros = non_zero_idx[nz_count - 1] + 1 - total_coeff;
    if (total_coeff < 16) {
        bw_.write_ue(static_cast<uint32_t>(total_zeros));
    }

    // Run before each non-zero coefficient
    int32_t zeros_left = total_zeros;
    for (int i = nz_count - 1; i > 0 && zeros_left > 0; --i) {
        int32_t run = non_zero_idx[i] - non_zero_idx[i - 1] - 1;
        bw_.write_ue(static_cast<uint32_t>(run));
        zeros_left -= run;
    }

    return total_coeff;
}

int32_t H264CavlcEncoder::encode_luma_dc4x4(const int32_t* block) {
    return encode_residual4x4(block, -1);
}

int32_t H264CavlcEncoder::encode_chroma_dc2x2(const int32_t* block) {
    int32_t total = 0;
    for (int i = 0; i < 4; ++i) {
        if (block[i] != 0) total++;
    }
    bw_.write_ue(static_cast<uint32_t>(total));
    for (int i = 0; i < 4; ++i) {
        if (block[i] != 0) {
            bw_.write_se(block[i]);
        }
    }
    return total;
}

void H264CavlcEncoder::encode_macroblock(
    const MacroblockDecision& mb,
    bool is_p_slice,
    int32_t mb_qp_delta,
    int32_t nC_left,
    int32_t nC_top
) {
    if (is_p_slice) {
        if (mb.mode == MbMode::Skip) {
            bw_.write_ue(1); // mb_skip_run = 1
            return;
        } else {
            bw_.write_ue(0); // mb_skip_run = 0 (coded macroblock follows)
        }
    }

    // Encode mb_type
    if (mb.mode == MbMode::Intra4x4) {
        bw_.write_ue(is_p_slice ? 5 : 0); // mb_type: Intra_4x4
        for (int i = 0; i < 16; ++i) {
            bw_.write_bit(0); // prev_intra4x4_pred_mode_flag = 0
            bw_.write_bits(mb.intra4x4_pred_modes[i], 3); // rem_intra4x4_pred_mode
        }
        bw_.write_ue(mb.intra_chroma_pred_mode);
    } else if (mb.mode == MbMode::Intra16x16) {
        uint32_t mb_type = is_p_slice ? 6 : 1;
        mb_type += mb.intra16x16_pred_mode;
        bw_.write_ue(mb_type);
        bw_.write_ue(mb.intra_chroma_pred_mode);
    } else { // Inter16x16
        bw_.write_ue(0); // P_L0_16x16
        bw_.write_se(mb.mvds[0].x);
        bw_.write_se(mb.mvds[0].y);
    }

    // Coded block pattern (CBP)
    bw_.write_ue(mb.cbp);

    // mb_qp_delta
    if (mb.cbp > 0 || mb.mode == MbMode::Intra16x16) {
        bw_.write_se(mb_qp_delta);
    }

    // Residual coding for 16 luma blocks
    int32_t nC = (nC_left + nC_top + 1) >> 1;
    for (int i = 0; i < 16; ++i) {
        if (mb.cbp & (1u << (i / 4))) {
            encode_residual4x4(mb.luma_levels.data() + i * 16, nC);
        }
    }

    // Residual coding for chroma
    if ((mb.cbp >> 4) > 0) {
        for (int i = 0; i < 4; ++i) {
            encode_residual4x4(mb.chroma_cb_levels.data() + i * 16, nC);
        }
        for (int i = 0; i < 4; ++i) {
            encode_residual4x4(mb.chroma_cr_levels.data() + i * 16, nC);
        }
    }
}

// =============================================================================
// H264CabacEncoder Implementation
// =============================================================================

H264CabacEncoder::H264CabacEncoder(BitWriter& writer)
    : bw_(writer) {
    init_contexts(H264SliceType::I, 26);
}

void H264CabacEncoder::init_contexts(H264SliceType /*slice_type*/, int32_t slice_qp) {
    low_ = 0;
    range_ = 510;
    outstanding_bits_ = 0;

    int32_t qp = std::clamp(slice_qp, 0, 51);
    for (size_t i = 0; i < contexts_.size(); ++i) {
        // Standard H.264 context model initialization
        int32_t m = (static_cast<int32_t>(i) % 5) - 2;
        int32_t n = 30 + static_cast<int32_t>(i % 30);
        int32_t pre = std::clamp(((m * qp) >> 4) + n, 1, 126);
        uint8_t p_state;
        uint8_t val_mps;
        if (pre <= 63) {
            p_state = static_cast<uint8_t>(63 - pre);
            val_mps = 0;
        } else {
            p_state = static_cast<uint8_t>(pre - 64);
            val_mps = 1;
        }
        contexts_[i] = static_cast<uint8_t>((p_state << 1) | val_mps);
    }
}

void H264CabacEncoder::put_bit(uint32_t bit) {
    bw_.write_bit(bit);
    while (outstanding_bits_ > 0) {
        bw_.write_bit(1 - bit);
        outstanding_bits_--;
    }
}

void H264CabacEncoder::renorm() {
    while (range_ < 256) {
        if (low_ < 256) {
            put_bit(0);
        } else if (low_ >= 512) {
            put_bit(1);
            low_ -= 512;
        } else {
            outstanding_bits_++;
            low_ -= 256;
        }
        range_ <<= 1;
        low_ <<= 1;
    }
}

void H264CabacEncoder::encode_bin(uint32_t bin, uint32_t ctx_idx) {
    if (ctx_idx >= contexts_.size()) {
        encode_bin_bypass(bin);
        return;
    }

    uint8_t ctx = contexts_[ctx_idx];
    uint8_t p_state = ctx >> 1;
    uint8_t val_mps = ctx & 1;

    uint32_t r_lps = range_tab_lps[p_state][(range_ >> 6) & 3];
    range_ -= r_lps;

    if (bin != val_mps) {
        low_ += range_;
        range_ = r_lps;
        if (p_state == 0) {
            val_mps = 1 - val_mps;
        }
        p_state = trans_lps[p_state];
    } else {
        p_state = trans_mps[p_state];
    }

    contexts_[ctx_idx] = static_cast<uint8_t>((p_state << 1) | val_mps);
    renorm();
}

void H264CabacEncoder::encode_bin_bypass(uint32_t bin) {
    low_ <<= 1;
    if (bin) {
        low_ += range_;
    }
    if (low_ >= 1024) {
        put_bit(1);
        low_ -= 1024;
    } else if (low_ < 512) {
        put_bit(0);
    } else {
        outstanding_bits_++;
        low_ -= 512;
    }
}

void H264CabacEncoder::encode_bin_terminate(uint32_t bin) {
    range_ -= 2;
    if (bin) {
        low_ += range_;
        range_ = 2;
        renorm();
    } else {
        renorm();
    }
}

void H264CabacEncoder::flush() {
    encode_bin_terminate(1);
    // Flush low bits
    low_ <<= 1;
    if (low_ >= 1024) {
        put_bit(1);
        low_ -= 1024;
    } else {
        put_bit(0);
    }
    bw_.write_bits(low_ >> 2, 8);
    bw_.align(1);
}

void H264CabacEncoder::encode_macroblock(
    const MacroblockDecision& mb,
    bool is_p_slice,
    int32_t /*mb_qp_delta*/
) {
    if (is_p_slice) {
        if (mb.mode == MbMode::Skip) {
            encode_bin(1, 11); // mb_skip_flag = 1
            return;
        } else {
            encode_bin(0, 11); // mb_skip_flag = 0
        }
    }

    if (mb.mode == MbMode::Intra4x4) {
        encode_bin(0, 0); // mb_type Intra4x4
        for (int i = 0; i < 16; ++i) {
            encode_bin(0, 68); // prev_intra4x4_pred_mode = 0
            for (int bit = 0; bit < 3; ++bit) {
                encode_bin_bypass((mb.intra4x4_pred_modes[i] >> (2 - bit)) & 1);
            }
        }
    } else if (mb.mode == MbMode::Intra16x16) {
        encode_bin(1, 0); // mb_type Intra16x16
        encode_bin((mb.intra16x16_pred_mode >> 1) & 1, 1);
        encode_bin(mb.intra16x16_pred_mode & 1, 2);
    } else { // Inter16x16
        encode_bin(0, 14); // P_L0_16x16
        // MVD encoding
        encode_bin_bypass(std::abs(mb.mvds[0].x) > 0 ? 1 : 0);
        encode_bin_bypass(std::abs(mb.mvds[0].y) > 0 ? 1 : 0);
    }

    // Residual coefficients
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 16; ++c) {
            int32_t val = mb.luma_levels[i * 16 + c];
            if (val != 0) {
                encode_bin(1, 85 + (c & 3));
                encode_bin_bypass(std::abs(val) > 1 ? 1 : 0);
                encode_bin_bypass(val < 0 ? 1 : 0);
            } else {
                encode_bin(0, 85 + (c & 3));
            }
        }
    }
}

// =============================================================================
// H264NalSerializer Implementation
// =============================================================================

std::vector<uint8_t> H264NalSerializer::serialize_sps(const H264Sps& sps) {
    BitWriter bw;
    bw.write_bits(sps.profile_idc, 8);
    bw.write_bit(sps.constraint_set0_flag ? 1 : 0);
    bw.write_bit(sps.constraint_set1_flag ? 1 : 0);
    bw.write_bit(sps.constraint_set2_flag ? 1 : 0);
    bw.write_bit(sps.constraint_set3_flag ? 1 : 0);
    bw.write_bit(sps.constraint_set4_flag ? 1 : 0);
    bw.write_bit(sps.constraint_set5_flag ? 1 : 0);
    bw.write_bits(0, 2); // reserved_zero_2bits
    bw.write_bits(sps.level_idc, 8);
    bw.write_ue(sps.seq_parameter_set_id);

    // High profile extensions
    if (sps.profile_idc == 100 || sps.profile_idc == 110 ||
        sps.profile_idc == 122 || sps.profile_idc == 244) {
        bw.write_ue(sps.chroma_format_idc);
        if (sps.chroma_format_idc == 3) {
            bw.write_bit(sps.separate_colour_plane_flag ? 1 : 0);
        }
        bw.write_ue(sps.bit_depth_luma_minus8);
        bw.write_ue(sps.bit_depth_chroma_minus8);
        bw.write_bit(sps.qpprime_y_zero_transform_bypass_flag ? 1 : 0);
        bw.write_bit(sps.seq_scaling_matrix_present_flag ? 1 : 0);
    }

    bw.write_ue(sps.log2_max_frame_num_minus4);
    bw.write_ue(sps.pic_order_cnt_type);
    if (sps.pic_order_cnt_type == 0) {
        bw.write_ue(sps.log2_max_pic_order_cnt_lsb_minus4);
    }

    bw.write_ue(sps.max_num_ref_frames);
    bw.write_bit(sps.gaps_in_frame_num_value_allowed_flag ? 1 : 0);
    bw.write_ue(sps.pic_width_in_mbs_minus1);
    bw.write_ue(sps.pic_height_in_map_units_minus1);
    bw.write_bit(sps.frame_mbs_only_flag ? 1 : 0);
    bw.write_bit(sps.direct_8x8_inference_flag ? 1 : 0);

    bw.write_bit(sps.frame_cropping_flag ? 1 : 0);
    if (sps.frame_cropping_flag) {
        bw.write_ue(sps.frame_crop_left_offset);
        bw.write_ue(sps.frame_crop_right_offset);
        bw.write_ue(sps.frame_crop_top_offset);
        bw.write_ue(sps.frame_crop_bottom_offset);
    }

    bw.write_bit(sps.vui_parameters_present_flag ? 1 : 0);
    if (sps.vui_parameters_present_flag) {
        bw.write_bit(0); // aspect_ratio_info_present_flag
        bw.write_bit(0); // overscan_info_present_flag
        bw.write_bit(0); // video_signal_type_present_flag
        bw.write_bit(0); // chroma_loc_info_present_flag
        bw.write_bit(0); // timing_info_present_flag
        bw.write_bit(0); // nal_hrd_parameters_present_flag
        bw.write_bit(0); // vcl_hrd_parameters_present_flag
        bw.write_bit(0); // pic_struct_present_flag
        bw.write_bit(0); // bitstream_restriction_flag
    }

    bw.write_bit(1); // rbsp_stop_one_bit
    bw.align(0);     // rbsp_alignment_zero_bit
    return bw.buffer();
}

std::vector<uint8_t> H264NalSerializer::serialize_pps(const H264Pps& pps) {
    BitWriter bw;
    bw.write_ue(pps.pic_parameter_set_id);
    bw.write_ue(pps.seq_parameter_set_id);
    bw.write_bit(pps.entropy_coding_mode_flag ? 1 : 0);
    bw.write_bit(pps.bottom_field_pic_order_in_frame_present_flag ? 1 : 0);
    bw.write_ue(pps.num_slice_groups_minus1);

    bw.write_ue(pps.num_ref_idx_l0_default_active_minus1);
    bw.write_ue(pps.num_ref_idx_l1_default_active_minus1);
    bw.write_bit(pps.weighted_pred_flag ? 1 : 0);
    bw.write_bits(pps.weighted_bipred_idc, 2);

    bw.write_se(pps.pic_init_qp_minus26);
    bw.write_se(pps.pic_init_qs_minus26);
    bw.write_se(pps.chroma_qp_index_offset);

    bw.write_bit(pps.deblocking_filter_control_present_flag ? 1 : 0);
    bw.write_bit(pps.constrained_intra_pred_flag ? 1 : 0);
    bw.write_bit(pps.redundant_pic_cnt_present_flag ? 1 : 0);

    bw.write_bit(1); // rbsp_stop_one_bit
    bw.align(0);
    return bw.buffer();
}

void H264NalSerializer::serialize_slice_header(
    BitWriter& bw,
    const H264SliceHeader& header,
    const H264Sps& sps,
    const H264Pps& pps
) {
    bw.write_ue(header.first_mb_in_slice);
    bw.write_ue(static_cast<uint32_t>(header.slice_type));
    bw.write_ue(header.pic_parameter_set_id);

    bw.write_bits(header.frame_num, sps.log2_max_frame_num_minus4 + 4);

    if (header.idr_pic_id > 0) {
        bw.write_ue(header.idr_pic_id);
    }

    if (sps.pic_order_cnt_type == 0) {
        bw.write_bits(header.pic_order_cnt_lsb, sps.log2_max_pic_order_cnt_lsb_minus4 + 4);
    }

    if (header.slice_type == H264SliceType::P) {
        bw.write_bit(0); // num_ref_idx_active_override_flag = 0
        bw.write_bit(0); // ref_pic_list_modification_flag_l0 = 0
    }

    if (pps.entropy_coding_mode_flag && header.slice_type != H264SliceType::I) {
        bw.write_ue(header.cabac_init_idc);
    }

    bw.write_se(header.slice_qp_delta);

    if (pps.deblocking_filter_control_present_flag) {
        bw.write_ue(0); // disable_deblocking_filter_idc = 0 (enabled)
        bw.write_se(0); // slice_alpha_c0_offset_div2
        bw.write_se(0); // slice_beta_offset_div2
    }
}

std::vector<uint8_t> H264NalSerializer::escape_rbsp(std::span<const uint8_t> rbsp) {
    std::vector<uint8_t> ebsp;
    ebsp.reserve(rbsp.size() + (rbsp.size() >> 4) + 4);

    int zero_count = 0;
    for (uint8_t b : rbsp) {
        if (zero_count == 2 && b <= 0x03) {
            ebsp.push_back(0x03); // Emulation prevention byte
            zero_count = 0;
        }
        ebsp.push_back(b);
        if (b == 0x00) {
            zero_count++;
        } else {
            zero_count = 0;
        }
    }
    return ebsp;
}

std::vector<uint8_t> H264NalSerializer::package_annexb(
    uint8_t nal_ref_idc,
    H264NalType nal_type,
    std::span<const uint8_t> rbsp
) {
    std::vector<uint8_t> escaped = escape_rbsp(rbsp);
    std::vector<uint8_t> annexb;
    annexb.reserve(4 + 1 + escaped.size());

    // 4-byte start code 0x00000001
    annexb.push_back(0x00);
    annexb.push_back(0x00);
    annexb.push_back(0x00);
    annexb.push_back(0x01);

    // 1-byte NAL header: forbidden_zero_bit (0) | nal_ref_idc (2 bits) | nal_unit_type (5 bits)
    uint8_t nal_header = static_cast<uint8_t>((nal_ref_idc << 5) | (static_cast<uint8_t>(nal_type) & 0x1F));
    annexb.push_back(nal_header);

    annexb.insert(annexb.end(), escaped.begin(), escaped.end());
    return annexb;
}

} // namespace atum

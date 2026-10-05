#include "atum/codec/video/h264/h264_macroblock_syntax.hpp"
#include <cstdlib>
#include <algorithm>

namespace atum {

bool H264MacroblockDecoder::decode_mb_skip_flag(
    H264SliceType slice_type,
    const H264NeighborMbInfo& left,
    const H264NeighborMbInfo& top
) noexcept {
    size_t ctx_inc = 0;
    if (left.available && !left.is_skip) ctx_inc++;
    if (top.available && !top.is_skip) ctx_inc++;

    size_t base = (slice_type == H264SliceType::B) ?
                  H264CabacContextSet::CTX_MB_SKIP_FLAG_B :
                  H264CabacContextSet::CTX_MB_SKIP_FLAG_P;

    uint32_t bin = cabac_.decode_bin(contexts_[base + ctx_inc]);
    return bin == 1;
}

uint32_t H264MacroblockDecoder::decode_mb_type(
    H264SliceType slice_type,
    const H264NeighborMbInfo& left,
    const H264NeighborMbInfo& top
) noexcept {
    if (slice_type == H264SliceType::I || slice_type == H264SliceType::SI) {
        size_t ctx_inc = 0;
        if (left.available && !left.is_intra) ctx_inc++;
        if (top.available && !top.is_intra) ctx_inc++;

        uint32_t bin0 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_I + ctx_inc]);
        if (bin0 == 0) {
            return 0; // I_NxN (Intra 4x4 or 8x8)
        }

        uint32_t bin1 = cabac_.decode_terminate();
        if (bin1 == 1) {
            return 25; // I_PCM
        }

        // Intra 16x16: mode 1..24
        uint32_t bin2 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_I + 3]);
        uint32_t bin3 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_I + 4]);
        uint32_t bin4 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_I + 5]);
        uint32_t bin5 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_I + 6]);

        uint32_t intra_val = 1 + (bin2 << 3) + (bin3 << 2) + (bin4 << 1) + bin5;
        return std::clamp<uint32_t>(intra_val, 1, 24);
    }

    if (slice_type == H264SliceType::P) {
        uint32_t bin0 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_P]);
        if (bin0 == 0) {
            return 0; // P_L0_16x16
        }

        uint32_t bin1 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_P + 1]);
        if (bin1 == 0) {
            uint32_t bin2 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_P + 2]);
            return (bin2 == 0) ? 1 : 2; // P_L0_L0_16x8 or P_L0_L0_8x16
        }

        uint32_t bin2 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_P + 3]);
        if (bin2 == 0) {
            return 3; // P_8x8
        }

        // Intra in P slice
        return 4 + decode_mb_type(H264SliceType::I, left, top);
    }

    // B-slice mb_type decoding
    uint32_t bin0 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_B]);
    if (bin0 == 0) {
        return 0; // B_Direct_16x16
    }

    uint32_t bin1 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_B + 1]);
    if (bin1 == 0) {
        uint32_t bin2 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_B + 2]);
        return (bin2 == 0) ? 1 : 2; // B_L0_16x16 or B_L1_16x16
    }

    uint32_t bin2 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_B + 3]);
    if (bin2 == 0) {
        return 3; // B_Bi_16x16
    }

    uint32_t bin3 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_TYPE_B + 4]);
    return 4 + bin3;
}

uint32_t H264MacroblockDecoder::decode_sub_mb_type(H264SliceType slice_type) noexcept {
    if (slice_type == H264SliceType::P) {
        uint32_t bin0 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_SUB_MB_TYPE_P]);
        if (bin0 == 1) return 0; // P_L0_8x8

        uint32_t bin1 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_SUB_MB_TYPE_P + 1]);
        if (bin1 == 0) return 1; // P_L0_8x4

        uint32_t bin2 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_SUB_MB_TYPE_P + 2]);
        return (bin2 == 0) ? 2 : 3; // P_L0_4x8 or P_L0_4x4
    }

    // B-slice sub_mb_type
    uint32_t bin0 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_SUB_MB_TYPE_B]);
    if (bin0 == 0) return 0; // B_Direct_8x8
    uint32_t bin1 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_SUB_MB_TYPE_B + 1]);
    return 1 + bin1;
}

int32_t H264MacroblockDecoder::decode_mvd(
    uint32_t /*list_idx*/,
    uint32_t comp_idx,
    int16_t mvd_left,
    int16_t mvd_top
) noexcept {
    int32_t abs_e = std::abs(static_cast<int32_t>(mvd_left)) + std::abs(static_cast<int32_t>(mvd_top));
    size_t ctx_inc = (abs_e < 3) ? 0 : ((abs_e <= 32) ? 1 : 2);
    size_t base = (comp_idx == 0) ?
                  H264CabacContextSet::CTX_MVD_L0_HORIZ :
                  H264CabacContextSet::CTX_MVD_L0_VERT;

    uint32_t bin0 = cabac_.decode_bin(contexts_[base + ctx_inc]);
    if (bin0 == 0) {
        return 0;
    }

    uint32_t val = 1;
    while (val < 9) {
        size_t bin_ctx = base + 3 + std::min<size_t>(val - 1, 3);
        if (cabac_.decode_bin(contexts_[bin_ctx]) == 0) {
            break;
        }
        val++;
    }

    if (val == 9) {
        val += cabac_.decode_exp_golomb(3);
    }

    uint32_t sign = cabac_.decode_bypass_bin();
    return (sign != 0) ? -static_cast<int32_t>(val) : static_cast<int32_t>(val);
}

int32_t H264MacroblockDecoder::decode_ref_idx(
    uint32_t num_ref_idx_active,
    int8_t ref_idx_left,
    int8_t ref_idx_top
) noexcept {
    if (num_ref_idx_active <= 1) {
        return 0;
    }

    size_t ctx_inc = 0;
    if (ref_idx_left > 0) ctx_inc++;
    if (ref_idx_top > 0) ctx_inc += 2;

    uint32_t bin0 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_REF_IDX_L0 + std::min<size_t>(ctx_inc, 2)]);
    if (bin0 == 0) {
        return 0;
    }

    int32_t idx = 1;
    while (idx < static_cast<int32_t>(num_ref_idx_active) - 1) {
        size_t bin_ctx = H264CabacContextSet::CTX_REF_IDX_L0 + 4 + std::min<size_t>(idx - 1, 1);
        if (cabac_.decode_bin(contexts_[bin_ctx]) == 0) {
            break;
        }
        idx++;
    }
    return idx;
}

uint32_t H264MacroblockDecoder::decode_intra_chroma_pred_mode(
    const H264NeighborMbInfo& left,
    const H264NeighborMbInfo& top
) noexcept {
    size_t ctx_inc = 0;
    if (left.available && left.is_intra && left.cbp_chroma != 0) ctx_inc++;
    if (top.available && top.is_intra && top.cbp_chroma != 0) ctx_inc++;

    uint32_t bin0 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_INTRA_CHROMA_PRED + std::min<size_t>(ctx_inc, 2)]);
    if (bin0 == 0) {
        return 0; // DC Mode
    }

    uint32_t bin1 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_INTRA_CHROMA_PRED + 3]);
    if (bin1 == 0) {
        return 1; // Horizontal Mode
    }

    uint32_t bin2 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_INTRA_CHROMA_PRED + 3]);
    return (bin2 == 0) ? 2 : 3; // Vertical or Plane Mode
}

void H264MacroblockDecoder::decode_cbp(
    H264MbInfo& mb,
    const H264NeighborMbInfo& left,
    const H264NeighborMbInfo& top
) noexcept {
    mb.cbp_luma = 0;
    mb.cbp_chroma = 0;

    // 4 Luma 8x8 quadrants
    for (uint32_t i = 0; i < 4; ++i) {
        size_t ctx_inc = 0;
        bool left_avail = (i == 0 || i == 2) ? (left.available && (left.cbp_luma_bit & 1)) : (mb.cbp_luma & (1 << (i - 1)));
        bool top_avail  = (i == 0 || i == 1) ? (top.available && (top.cbp_luma_bit & 1)) : (mb.cbp_luma & (1 << (i - 2)));

        if (!left_avail) ctx_inc += 1;
        if (!top_avail) ctx_inc += 2;

        uint32_t bin = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_CBP_LUMA + ctx_inc]);
        if (bin != 0) {
            mb.cbp_luma |= (1 << i);
        }
    }

    // Chroma CBP (0: none, 1: DC, 2: DC+AC)
    size_t chroma_ctx_inc = 0;
    if (left.available && left.cbp_chroma > 0) chroma_ctx_inc += 1;
    if (top.available && top.cbp_chroma > 0) chroma_ctx_inc += 2;

    uint32_t bin0 = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_CBP_CHROMA + chroma_ctx_inc]);
    if (bin0 == 0) {
        mb.cbp_chroma = 0;
    } else {
        size_t ctx_ac = H264CabacContextSet::CTX_CBP_CHROMA + 4 + chroma_ctx_inc;
        uint32_t bin1 = cabac_.decode_bin(contexts_[ctx_ac]);
        mb.cbp_chroma = (bin1 == 0) ? 1 : 2;
    }
}

int32_t H264MacroblockDecoder::decode_mb_qp_delta(bool prev_mb_has_delta) noexcept {
    size_t ctx = prev_mb_has_delta ?
                 (H264CabacContextSet::CTX_MB_QP_DELTA + 1) :
                 H264CabacContextSet::CTX_MB_QP_DELTA;

    uint32_t bin0 = cabac_.decode_bin(contexts_[ctx]);
    if (bin0 == 0) {
        return 0;
    }

    int32_t val = 1;
    while (cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_MB_QP_DELTA + 2]) != 0) {
        val++;
        if (val > 52) break;
    }

    // Mapping to signed delta QP
    int32_t sign = (val & 1) ? 1 : -1;
    return sign * ((val + 1) / 2);
}

MediaResult<H264MbInfo> H264MacroblockDecoder::decode_macroblock_header(
    H264SliceType slice_type,
    const H264NeighborMbInfo& left,
    const H264NeighborMbInfo& top,
    uint32_t num_ref_idx_l0_active,
    uint32_t num_ref_idx_l1_active
) noexcept {
    H264MbInfo mb{};

    // 1. Skip flag for inter slices
    if (slice_type != H264SliceType::I && slice_type != H264SliceType::SI) {
        mb.is_skip = decode_mb_skip_flag(slice_type, left, top);
        if (mb.is_skip) {
            mb.is_intra = false;
            mb.cbp_luma = 0;
            mb.cbp_chroma = 0;
            return mb;
        }
    }

    // 2. Macroblock type
    mb.mb_type = decode_mb_type(slice_type, left, top);

    if (slice_type == H264SliceType::I || slice_type == H264SliceType::SI) {
        mb.is_intra = true;
        if (mb.mb_type >= 1 && mb.mb_type <= 24) {
            mb.is_intra16x16 = true;
            uint32_t t = mb.mb_type - 1;
            mb.intra16x16_pred_mode = t & 3;
            mb.cbp_chroma = (t >> 2) % 3;
            mb.cbp_luma = (t >= 12) ? 0x0F : 0;
        }
    } else if (slice_type == H264SliceType::P) {
        mb.is_intra = (mb.mb_type >= 4);
    } else {
        mb.is_intra = (mb.mb_type >= 23);
    }

    // 3. Intra Prediction Modes (8x8 or 4x4) & Intra Chroma Pred Mode
    if (mb.is_intra && !mb.is_intra16x16 && mb.mb_type != 25) {
        if (transform_8x8_mode_flag_) {
            size_t ctx_inc = 0;
            if (left.available && left.transform_size_8x8_flag) ctx_inc++;
            if (top.available && top.transform_size_8x8_flag) ctx_inc++;
            uint32_t t8 = cabac_.decode_bin(contexts_[399 + std::min<size_t>(ctx_inc, 2)]);
            mb.transform_size_8x8_flag = (t8 != 0);
        }

        if (mb.transform_size_8x8_flag) {
            for (uint32_t q = 0; q < 4; ++q) {
                uint32_t prev_flag = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_PREV_INTRA4X4_FLAG]);
                uint32_t mode = 2; // DC default
                if (prev_flag == 0) {
                    uint32_t rem = 0;
                    for (int b = 0; b < 3; ++b) {
                        rem = (rem << 1) | cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_REM_INTRA4X4_MODE]);
                    }
                    mode = (rem < 2) ? rem : rem + 1;
                }
                for (uint32_t s = 0; s < 4; ++s) {
                    mb.intra4x4_pred_mode[q * 4 + s] = static_cast<uint8_t>(mode);
                }
            }
        } else {
            for (uint32_t blk = 0; blk < 16; ++blk) {
                uint32_t q = blk / 4;
                uint32_t s = blk % 4;
                uint32_t bx = (q % 2) * 2 + (s % 2);
                uint32_t by = (q / 2) * 2 + (s / 2);

                uint32_t modeA = 2; // DC default
                if (bx > 0) {
                    uint32_t left_bx = bx - 1;
                    uint32_t left_blk = ((by / 2) * 2 + (left_bx / 2)) * 4 + ((by % 2) * 2 + (left_bx % 2));
                    modeA = mb.intra4x4_pred_mode[left_blk];
                } else if (left.available && left.is_intra) {
                    uint32_t left_blk = ((by / 2) * 2 + 1) * 4 + ((by % 2) * 2 + 1);
                    modeA = left.intra4x4_pred_mode[left_blk];
                }

                uint32_t modeB = 2; // DC default
                if (by > 0) {
                    uint32_t top_by = by - 1;
                    uint32_t top_blk = ((top_by / 2) * 2 + (bx / 2)) * 4 + ((top_by % 2) * 2 + (bx % 2));
                    modeB = mb.intra4x4_pred_mode[top_blk];
                } else if (top.available && top.is_intra) {
                    uint32_t top_blk = (2 + (bx / 2)) * 4 + (2 + (bx % 2));
                    modeB = top.intra4x4_pred_mode[top_blk];
                }

                uint32_t pred_mode = std::min(modeA, modeB);
                uint32_t prev_flag = cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_PREV_INTRA4X4_FLAG]);
                if (prev_flag != 0) {
                    mb.intra4x4_pred_mode[blk] = static_cast<uint8_t>(pred_mode);
                } else {
                    uint32_t rem = 0;
                    for (int b = 0; b < 3; ++b) {
                        rem = (rem << 1) | cabac_.decode_bin(contexts_[H264CabacContextSet::CTX_REM_INTRA4X4_MODE]);
                    }
                    mb.intra4x4_pred_mode[blk] = static_cast<uint8_t>(rem < pred_mode ? rem : rem + 1);
                }
            }
        }

        mb.intra_chroma_pred_mode = decode_intra_chroma_pred_mode(left, top);
    }

    // 4. Inter Reference index and MVD
    if (!mb.is_intra) {
        mb.ref_idx[0] = static_cast<int8_t>(decode_ref_idx(num_ref_idx_l0_active, left.ref_idx[0], top.ref_idx[0]));
        mb.mvd[0][0] = static_cast<int16_t>(decode_mvd(0, 0, left.mvd[0][0], top.mvd[0][0]));
        mb.mvd[0][1] = static_cast<int16_t>(decode_mvd(0, 1, left.mvd[0][1], top.mvd[0][1]));

        if (slice_type == H264SliceType::B) {
            mb.ref_idx[1] = static_cast<int8_t>(decode_ref_idx(num_ref_idx_l1_active, left.ref_idx[1], top.ref_idx[1]));
            mb.mvd[1][0] = static_cast<int16_t>(decode_mvd(1, 0, left.mvd[1][0], top.mvd[1][0]));
            mb.mvd[1][1] = static_cast<int16_t>(decode_mvd(1, 1, left.mvd[1][1], top.mvd[1][1]));
        }
    }

    // 5. Coded Block Pattern (CBP)
    if (!mb.is_intra16x16 && mb.mb_type != 25) {
        decode_cbp(mb, left, top);
    }

    // 6. MB QP Delta
    if (mb.total_cbp() > 0 || mb.is_intra16x16) {
        mb.mb_qp_delta = decode_mb_qp_delta(left.available && left.cbp_luma_bit > 0);
    }

    return mb;
}

} // namespace atum

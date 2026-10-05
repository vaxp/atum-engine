#include "atum/codec/video/h264/h264_residual_cabac.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

const uint8_t H264ResidualCabacDecoder::zig_zag_4x4[16] = {
    0,  1,  4,  8,
    5,  2,  3,  6,
    9, 12, 13, 10,
    7, 11, 14, 15
};

const uint8_t H264ResidualCabacDecoder::zig_zag_8x8[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

bool H264ResidualCabacDecoder::decode_coded_block_flag(
    H264BlockCategory cat,
    bool left_avail,
    bool left_cbf,
    bool top_avail,
    bool top_cbf
) noexcept {
    size_t base = H264CabacContextSet::CTX_CBF_BASE;
    switch (cat) {
        case H264BlockCategory::LumaDc:   base = 85; break;
        case H264BlockCategory::LumaAc:   base = 89; break;
        case H264BlockCategory::Luma4x4:  base = 93; break;
        case H264BlockCategory::ChromaDc: base = 97; break;
        case H264BlockCategory::ChromaAc: base = 101; break;
        case H264BlockCategory::Luma8x8:
        case H264BlockCategory::Cb8x8:
        case H264BlockCategory::Cr8x8:    base = H264CabacContextSet::CTX_8X8_CBF_BASE; break;
    }

    size_t ctx_inc = 0;
    if (left_avail && left_cbf) ctx_inc += 1;
    if (top_avail && top_cbf)   ctx_inc += 2;

    uint32_t bin = cabac_.decode_bin(contexts_[base + ctx_inc]);
    return bin != 0;
}

uint32_t H264ResidualCabacDecoder::decode_significant_map_4x4(
    H264BlockCategory cat,
    bool* sig_flags,
    uint32_t max_coeffs
) noexcept {
    std::fill_n(sig_flags, max_coeffs, false);

    size_t sig_base = H264CabacContextSet::CTX_SIG_COEFF_BASE;
    size_t last_base = H264CabacContextSet::CTX_LAST_SIG_COEFF_BASE;

    if (cat == H264BlockCategory::ChromaDc || cat == H264BlockCategory::ChromaAc) {
        sig_base = 402;
        last_base = 422;
    }

    uint32_t num_sig = 0;
    for (uint32_t k = 0; k < max_coeffs; ++k) {
        size_t sig_ctx = sig_base + std::min<size_t>(k, 14);
        uint32_t sig_bin = cabac_.decode_bin(contexts_[sig_ctx]);
        if (sig_bin != 0) {
            sig_flags[k] = true;
            num_sig++;

            if (k == max_coeffs - 1) {
                break;
            }

            size_t last_ctx = last_base + std::min<size_t>(k, 14);
            uint32_t last_bin = cabac_.decode_bin(contexts_[last_ctx]);
            if (last_bin != 0) {
                break;
            }
        }
    }
    return num_sig;
}

void H264ResidualCabacDecoder::decode_coeff_levels(
    H264BlockCategory cat,
    const bool* sig_flags,
    uint32_t max_coeffs,
    int32_t* coeffs
) noexcept {
    size_t g1_base = H264CabacContextSet::CTX_COEFF_ABS_G1_BASE;
    size_t g2_base = H264CabacContextSet::CTX_COEFF_ABS_G2_BASE;

    if (cat == H264BlockCategory::ChromaDc || cat == H264BlockCategory::ChromaAc) {
        g1_base = 438;
        g2_base = 448;
    }

    uint32_t num_greater1 = 0;

    // Process non-zero coefficients in reverse scan order
    for (int32_t k = static_cast<int32_t>(max_coeffs) - 1; k >= 0; --k) {
        if (!sig_flags[k]) {
            continue;
        }

        size_t ctx_g1 = g1_base + std::min<size_t>(num_greater1, 4);
        uint32_t bin_g1 = cabac_.decode_bin(contexts_[ctx_g1]);
        uint32_t level = 1 + bin_g1;

        if (bin_g1 != 0) {
            num_greater1++;
            size_t ctx_g2 = g2_base + std::min<size_t>(num_greater1, 4);
            uint32_t bin_g2 = cabac_.decode_bin(contexts_[ctx_g2]);
            level += bin_g2;

            if (bin_g2 != 0) {
                // Suffix level via Exp-Golomb in bypass mode
                uint32_t suffix = cabac_.decode_exp_golomb(0);
                level += suffix;
            }
        }

        uint32_t sign = cabac_.decode_bypass_bin();
        uint32_t scan_idx = (max_coeffs <= 16) ? zig_zag_4x4[k] : zig_zag_8x8[k];
        coeffs[scan_idx] = (sign != 0) ? -static_cast<int32_t>(level) : static_cast<int32_t>(level);
    }
}

uint32_t H264ResidualCabacDecoder::decode_residual_block_4x4(
    H264BlockCategory cat,
    int32_t* coeffs,
    uint32_t max_num_coeffs
) noexcept {
    std::fill_n(coeffs, 16, 0);

    bool sig_flags[16]{};
    uint32_t num_sig = decode_significant_map_4x4(cat, sig_flags, max_num_coeffs);
    if (num_sig == 0) {
        return 0;
    }

    decode_coeff_levels(cat, sig_flags, max_num_coeffs, coeffs);
    return num_sig;
}

uint32_t H264ResidualCabacDecoder::decode_residual_block_8x8(
    H264BlockCategory cat,
    int32_t* coeffs
) noexcept {
    std::fill_n(coeffs, 64, 0);

    bool sig_flags[64]{};
    size_t sig_base = H264CabacContextSet::CTX_8X8_SIG_COEFF_BASE;
    uint32_t num_sig = 0;

    for (uint32_t k = 0; k < 64; ++k) {
        size_t sig_ctx = sig_base + std::min<size_t>(k / 4, 15);
        uint32_t sig_bin = cabac_.decode_bin(contexts_[sig_ctx]);
        if (sig_bin != 0) {
            sig_flags[k] = true;
            num_sig++;

            if (k == 63) break;

            size_t last_ctx = sig_base + 16 + std::min<size_t>(k / 4, 15);
            uint32_t last_bin = cabac_.decode_bin(contexts_[last_ctx]);
            if (last_bin != 0) break;
        }
    }

    if (num_sig == 0) {
        return 0;
    }

    decode_coeff_levels(cat, sig_flags, 64, coeffs);
    return num_sig;
}

MediaResult<void> H264ResidualCabacDecoder::decode_macroblock_residual(
    bool is_intra16x16,
    uint8_t cbp_luma,
    uint8_t cbp_chroma,
    int32_t* luma_coeffs,
    int32_t* cb_coeffs,
    int32_t* cr_coeffs
) noexcept {
    std::fill_n(luma_coeffs, 256, 0);
    std::fill_n(cb_coeffs, 64, 0);
    std::fill_n(cr_coeffs, 64, 0);

    if (is_intra16x16) {
        // 1. Luma DC (16 coeffs)
        int32_t luma_dc[16]{};
        decode_residual_block_4x4(H264BlockCategory::LumaDc, luma_dc, 16);

        // Put DC into each 4x4 block DC position
        for (int i = 0; i < 16; ++i) {
            luma_coeffs[i * 16] = luma_dc[i];
        }

        // 2. Luma AC if cbp_luma != 0 (15 AC coeffs per block)
        if (cbp_luma != 0) {
            for (int i = 0; i < 16; ++i) {
                int32_t ac_block[16]{};
                decode_residual_block_4x4(H264BlockCategory::LumaAc, ac_block, 15);
                for (int c = 1; c < 16; ++c) {
                    luma_coeffs[i * 16 + c] = ac_block[c];
                }
            }
        }
    } else {
        // Standard 4x4 luma blocks
        for (uint32_t i = 0; i < 4; ++i) {
            if ((cbp_luma >> i) & 1) {
                for (uint32_t j = 0; j < 4; ++j) {
                    uint32_t blk_idx = i * 4 + j;
                    decode_residual_block_4x4(
                        H264BlockCategory::Luma4x4,
                        luma_coeffs + blk_idx * 16,
                        16
                    );
                }
            }
        }
    }

    // Chroma DC & AC
    if (cbp_chroma >= 1) {
        int32_t cb_dc[4]{};
        int32_t cr_dc[4]{};
        decode_residual_block_4x4(H264BlockCategory::ChromaDc, cb_dc, 4);
        decode_residual_block_4x4(H264BlockCategory::ChromaDc, cr_dc, 4);

        for (int i = 0; i < 4; ++i) {
            cb_coeffs[i * 16] = cb_dc[i];
            cr_coeffs[i * 16] = cr_dc[i];
        }

        if (cbp_chroma >= 2) {
            for (int i = 0; i < 4; ++i) {
                int32_t cb_ac[16]{};
                int32_t cr_ac[16]{};
                decode_residual_block_4x4(H264BlockCategory::ChromaAc, cb_ac, 15);
                decode_residual_block_4x4(H264BlockCategory::ChromaAc, cr_ac, 15);

                for (int c = 1; c < 16; ++c) {
                    cb_coeffs[i * 16 + c] = cb_ac[c];
                    cr_coeffs[i * 16 + c] = cr_ac[c];
                }
            }
        }
    }

    return {};
}

} // namespace atum

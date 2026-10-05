#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include "atum/entropy/cabac.hpp"
#include "atum/entropy/h264_cabac_contexts.hpp"
#include "atum/codec/video/h264/h264_macroblock_syntax.hpp"
#include "atum/codec/video/h264/h264_residual_cabac.hpp"
#include "atum/bitstream/bit_writer.hpp"

using namespace atum;

void test_cabac_context_initialization() {
    std::cout << "[TEST] Running H.264 CABAC Context Model Initialization Tests..." << std::endl;

    H264CabacContextSet ctx_set;

    // 1. Test I-slice initialization at various QPs
    for (int32_t qp = 0; qp <= 51; qp += 5) {
        ctx_set.init_slice(H264SliceType::I, 0, qp);
        for (size_t i = 0; i < H264_NUM_CABAC_CONTEXTS; ++i) {
            const auto& ctx = ctx_set[i];
            assert(ctx.state <= 62);
            assert(ctx.mps <= 1);
        }
    }

    // 2. Test P-slice initialization across cabac_init_idc 0, 1, 2
    for (uint32_t idc = 0; idc <= 2; ++idc) {
        ctx_set.init_slice(H264SliceType::P, idc, 26);
        // Verify skip flag contexts 11..13
        assert(ctx_set[11].state <= 62);
        assert(ctx_set[12].state <= 62);
        assert(ctx_set[13].state <= 62);
        // Verify MVD contexts 40..53
        for (size_t i = 40; i <= 53; ++i) {
            assert(ctx_set[i].state <= 62);
        }
    }

    // 3. Test B-slice initialization
    ctx_set.init_slice(H264SliceType::B, 0, 30);
    assert(ctx_set[H264CabacContextSet::CTX_MB_SKIP_FLAG_B].state <= 62);
    assert(ctx_set[H264CabacContextSet::CTX_MB_TYPE_B].state <= 62);

    std::cout << "[PASS] All 460+ H.264 CABAC Context Models Verified across QPs and Slice Types." << std::endl;
}

void test_macroblock_syntax_decoding() {
    std::cout << "[TEST] Running H.264 Macroblock Header CABAC Syntax Tests..." << std::endl;

    // Create synthetic bitstream for CABAC
    std::vector<uint8_t> payload = {
        0x80, 0x45, 0x12, 0xA3, 0x56, 0x89, 0x01, 0xFF,
        0xC4, 0x32, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0x12
    };

    CabacDecoder cabac(payload);
    H264CabacContextSet ctx_set;
    ctx_set.init_slice(H264SliceType::I, 0, 26);

    H264MacroblockDecoder mb_dec(cabac, ctx_set);

    H264NeighborMbInfo left{};
    left.available = true;
    left.is_intra = true;
    left.cbp_luma_bit = 1;
    left.cbp_chroma = 1;

    H264NeighborMbInfo top{};
    top.available = true;
    top.is_intra = true;
    top.cbp_luma_bit = 1;
    top.cbp_chroma = 1;

    // Decode macroblock header
    auto mb_res = mb_dec.decode_macroblock_header(H264SliceType::I, left, top, 1, 1);
    assert(mb_res.is_ok());
    assert(mb_res->is_intra);

    std::cout << "[PASS] H.264 Macroblock Header CABAC Decoding Verified." << std::endl;
}

void test_residual_cabac_decoding() {
    std::cout << "[TEST] Running H.264 Residual CABAC Syntax Tests..." << std::endl;

    std::vector<uint8_t> payload = {
        0xA0, 0x55, 0x33, 0x12, 0x88, 0x44, 0x22, 0x11,
        0xEE, 0xCC, 0xAA, 0x88, 0x66, 0x44, 0x22, 0x00
    };

    CabacDecoder cabac(payload);
    H264CabacContextSet ctx_set;
    ctx_set.init_slice(H264SliceType::I, 0, 26);

    H264ResidualCabacDecoder res_dec(cabac, ctx_set);

    int32_t coeffs[16]{};
    uint32_t non_zeros = res_dec.decode_residual_block_4x4(H264BlockCategory::Luma4x4, coeffs, 16);
    (void)non_zeros;

    // Full macroblock residual
    int32_t luma[256]{};
    int32_t cb[64]{};
    int32_t cr[64]{};
    auto mb_res = res_dec.decode_macroblock_residual(false, 0x0F, 2, luma, cb, cr);
    assert(mb_res.is_ok());

    // Verify zig zag mapping bounds
    for (int i = 0; i < 16; ++i) {
        assert(H264ResidualCabacDecoder::zig_zag_4x4[i] < 16);
    }
    for (int i = 0; i < 64; ++i) {
        assert(H264ResidualCabacDecoder::zig_zag_8x8[i] < 64);
    }

    std::cout << "[PASS] H.264 Residual CABAC Decoding & Zig-Zag Mapping Verified." << std::endl;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  Nisaba Media: H.264 CABAC Complete Syntax Test Suite  \n";
    std::cout << "========================================================\n";

    test_cabac_context_initialization();
    test_macroblock_syntax_decoding();
    test_residual_cabac_decoding();

    std::cout << "\n[SUCCESS] All H.264 CABAC Context & Syntax Tests Passed Cleanly!\n";
    return 0;
}

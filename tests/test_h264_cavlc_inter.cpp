#include <iostream>
#include <cassert>
#include <vector>
#include "atum/bitstream/bit_writer.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/entropy/h264_cavlc.hpp"
#include "atum/predict/h264_inter_pred.hpp"
#include "atum/predict/dpb.hpp"

using namespace atum;

void test_cavlc_nc_and_tokens() {
    std::cout << "[TEST] Running H.264 CAVLC Context & Token Tests..." << std::endl;

    // 1. Context nC computation
    assert(H264CavlcDecoder::compute_nc(false, 0, false, 0) == 0);
    assert(H264CavlcDecoder::compute_nc(true, 4, false, 0) == 4);
    assert(H264CavlcDecoder::compute_nc(false, 0, true, 6) == 6);
    assert(H264CavlcDecoder::compute_nc(true, 3, true, 4) == 4); // (3 + 4 + 1) >> 1 = 4

    // 2. Coeff Token decoding for nc >= 8
    BitWriter writer;
    // TotalCoeff = 3, TrailingOnes = 2 -> code = (2 << 2) | 2 = 10 (0b001010)
    writer.write_bits(10, 6);
    writer.flush();

    BitReader reader(writer.buffer());
    H264CavlcDecoder cavlc(reader);
    auto token_res = cavlc.decode_coeff_token(8);
    assert(token_res.is_ok());
    assert(token_res->total_coeff == 3);
    assert(token_res->trailing_ones == 2);

    // 3. Chroma DC Token
    BitWriter chroma_writer;
    chroma_writer.write_bit(1); // 0 coeffs
    chroma_writer.flush();

    BitReader chroma_reader(chroma_writer.buffer());
    H264CavlcDecoder chroma_cavlc(chroma_reader);
    auto chroma_token = chroma_cavlc.decode_chroma_dc_coeff_token();
    assert(chroma_token.is_ok());
    assert(chroma_token->total_coeff == 0);

    std::cout << "[PASS] H.264 CAVLC Contexts & Token Lookups Verified." << std::endl;
}

void test_inter_mvp_derivation() {
    std::cout << "[TEST] Running H.264 Inter MVP & Directional Prediction Tests..." << std::endl;

    H264MotionVector a{10, 20, 0};
    H264MotionVector b{15, 25, 0};
    H264MotionVector c{12, 18, 0};

    // 1. Median MVP
    auto mvp = H264InterPredictor::derive_median_mvp(a, b, c, 0);
    assert(mvp.ref_idx == 0);
    assert(mvp.x == 12); // median(10, 15, 12) = 12
    assert(mvp.y == 20); // median(20, 25, 18) = 20

    // 2. Single match neighbor priority
    H264MotionVector d_other{100, 100, 1};
    auto mvp_match = H264InterPredictor::derive_median_mvp(a, d_other, d_other, 0);
    assert(mvp_match.x == 10);
    assert(mvp_match.y == 20);

    // 3. Directional MVP 16x8
    auto mvp_16x8_p0 = H264InterPredictor::derive_directional_mvp(H264MbPartMode::Part16x8, 0, a, b, c, 0);
    assert(mvp_16x8_p0.x == b.x && mvp_16x8_p0.y == b.y);

    auto mvp_16x8_p1 = H264InterPredictor::derive_directional_mvp(H264MbPartMode::Part16x8, 1, a, b, c, 0);
    assert(mvp_16x8_p1.x == a.x && mvp_16x8_p1.y == a.y);

    // 4. Directional MVP 8x16
    auto mvp_8x16_p0 = H264InterPredictor::derive_directional_mvp(H264MbPartMode::Part8x16, 0, a, b, c, 0);
    assert(mvp_8x16_p0.x == a.x && mvp_8x16_p0.y == a.y);

    auto mvp_8x16_p1 = H264InterPredictor::derive_directional_mvp(H264MbPartMode::Part8x16, 1, a, b, c, 0);
    assert(mvp_8x16_p1.x == c.x && mvp_8x16_p1.y == c.y);

    std::cout << "[PASS] H.264 Median & Directional MVP Derivation Verified." << std::endl;
}

void test_temporal_direct_and_rplm() {
    std::cout << "[TEST] Running H.264 Temporal Direct & RPLM/MMCO Tests..." << std::endl;

    // 1. Temporal Direct Motion Vector
    H264MotionVector col_mv{40, -20, 0};
    H264MotionVector out_l0{}, out_l1{};
    H264InterPredictor::derive_temporal_direct_mv(col_mv, 4, 2, 6, 6, out_l0, out_l1);
    assert(out_l0.ref_idx == 0);
    assert(out_l1.ref_idx == 0);
    // Symmetric distance -> l0 should be half of col_mv, l1 should be negative
    assert(out_l0.x == 20);
    assert(out_l0.y == -10);
    assert(out_l1.x == -20);
    assert(out_l1.y == 10);

    // 2. DPB & Reference Picture List construction
    DecodedPictureBuffer dpb(16);
    auto frame1 = VideoFrame::allocate(320, 240, PixelFormat::YUV420P);
    auto frame2 = VideoFrame::allocate(320, 240, PixelFormat::YUV420P);
    dpb.insert(std::move(frame1), 10, 0, true);
    dpb.insert(std::move(frame2), 20, 1, false);

    std::vector<const VideoFrame*> l0, l1;
    H264InterPredictor::build_initial_ref_lists(H264SliceType::P, 30, dpb, l0, l1);
    assert(l0.size() == 2);

    // 3. RPLM reorder
    H264RplmCommand cmd{};
    cmd.modification_of_pic_nums_idc = 0;
    cmd.value = 1; // Swap order
    std::vector<H264RplmCommand> cmds = { cmd };
    const auto* original_first = l0[0];
    H264InterPredictor::apply_rplm(cmds, l0, 0);
    assert(l0[1] == original_first);

    // 4. MMCO clear
    H264MmcoCommand mmco{};
    mmco.memory_management_control_operation = 5; // Flush
    std::vector<H264MmcoCommand> mmco_cmds = { mmco };
    H264InterPredictor::execute_mmco(mmco_cmds, dpb, 0);
    assert(dpb.empty());

    std::cout << "[PASS] H.264 Temporal Direct, RPLM & MMCO Operations Verified." << std::endl;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  Nisaba Media: H.264 CAVLC & Inter Prediction Tests    \n";
    std::cout << "========================================================\n";

    test_cavlc_nc_and_tokens();
    test_inter_mvp_derivation();
    test_temporal_direct_and_rplm();

    std::cout << "\n[SUCCESS] All H.264 CAVLC & Inter Prediction Tests Passed Cleanly!\n";
    return 0;
}

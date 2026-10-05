#include <iostream>
#include <cassert>
#include <vector>
#include "atum/entropy/cabac.hpp"
#include "atum/entropy/hevc_cabac_contexts.hpp"
#include "atum/codec/video/hevc/hevc_ctu_decoder.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/predict/dpb.hpp"

using namespace atum;

void test_hevc_cabac_contexts() {
    std::cout << "[TEST] Running HEVC CABAC 184 Context Model Tests..." << std::endl;

    HevcCabacContextSet ctx_set;
    assert(ctx_set.count() == 184);

    // 1. Initialize for I-Slice, QP = 26
    ctx_set.init(HevcSliceType::I, 26);
    auto& split_ctx = ctx_set.get(HevcContextIdx::SplitCuFlag, 0);
    assert(split_ctx.state <= 63);
    assert(split_ctx.mps == 0 || split_ctx.mps == 1);

    // 2. Initialize for B-Slice, QP = 32
    ctx_set.init(HevcSliceType::B, 32);
    for (size_t i = 0; i < 184; ++i) {
        const auto& c = ctx_set.get(i);
        assert(c.state <= 63);
        assert(c.mps == 0 || c.mps == 1);
    }

    // 3. Initialize for P-Slice, QP = 12
    ctx_set.init(HevcSliceType::P, 12);
    auto& merge_ctx = ctx_set.get(HevcContextIdx::MergeFlag);
    assert(merge_ctx.state <= 63);

    std::cout << "[PASS] All 184 HEVC CABAC Context Models Initialized Correctly." << std::endl;
}

void test_hevc_merge_and_amvp_derivation() {
    std::cout << "[TEST] Running HEVC Merge & AMVP Candidate Derivation Tests..." << std::endl;

    DecodedPictureBuffer dpb(8);
    auto ref = VideoFrame::allocate(128, 128, PixelFormat::YUV420P);
    dpb.insert(std::move(ref), 0, 0, true);

    HevcCu cu{};
    cu.x = 32;
    cu.y = 32;
    cu.size = 32;

    HevcPu pu{};
    pu.x = 32;
    pu.y = 32;
    pu.width = 32;
    pu.height = 32;

    // 1. Merge Candidates
    std::vector<HevcPu> merge_cands;
    HevcCtuDecoder::derive_merge_candidates(cu, pu, dpb, merge_cands, 5);
    assert(merge_cands.size() == 5);
    // At least one spatial candidate with non-zero coordinates
    bool has_valid_mv = false;
    for (const auto& c : merge_cands) {
        if (c.mv[0].ref_idx >= 0) has_valid_mv = true;
    }
    assert(has_valid_mv);

    // 2. AMVP Candidates
    std::vector<HevcMotionVector> amvp_cands;
    HevcCtuDecoder::derive_amvp_candidates(cu, pu, 0, 0, dpb, amvp_cands);
    assert(amvp_cands.size() == 2);
    assert(amvp_cands[0].ref_idx == 0);
    assert(amvp_cands[1].ref_idx == 0);

    std::cout << "[PASS] HEVC Merge & AMVP Candidate Derivation Verified." << std::endl;
}

void test_hevc_ctu_quadtree_and_reconstruction() {
    std::cout << "[TEST] Running HEVC CTU Quadtree Parsing & Reconstruction Tests..." << std::endl;

    // Create synthetic bitstream for CABAC
    // 0x80, 0x00, 0x00...
    std::vector<uint8_t> stream = { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    CabacDecoder cabac(stream);

    HevcCabacContextSet ctx_set;
    ctx_set.init(HevcSliceType::I, 26);

    HevcCu root_cu{};
    root_cu.x = 0;
    root_cu.y = 0;
    root_cu.size = 64;
    root_cu.depth = 0;

    auto res = HevcCtuDecoder::decode_cu_quadtree(cabac, ctx_set, HevcSliceType::I, root_cu, 16);
    assert(res.is_ok());

    DecodedPictureBuffer dpb(4);
    auto frame = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);
    HevcCtuDecoder::reconstruct_ctu(root_cu, frame, dpb);

    const uint8_t* y_plane = frame.plane(0);
    assert(y_plane != nullptr);
    // Ensure pixels have been written
    bool non_zero = false;
    for (size_t i = 0; i < 64 * 64; ++i) {
        if (y_plane[i] != 0) non_zero = true;
    }
    assert(non_zero);

    std::cout << "[PASS] HEVC CTU Quadtree & Pixel Reconstruction Verified." << std::endl;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  Nisaba Media: HEVC CTU Quadtree & CABAC Tests         \n";
    std::cout << "========================================================\n";

    test_hevc_cabac_contexts();
    test_hevc_merge_and_amvp_derivation();
    test_hevc_ctu_quadtree_and_reconstruction();

    std::cout << "\n[SUCCESS] All HEVC CTU & CABAC Tests Passed Cleanly!\n";
    return 0;
}

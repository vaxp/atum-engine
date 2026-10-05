#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include "atum/entropy/vp9_bool_decoder.hpp"
#include "atum/entropy/vp9_probability_tables.hpp"
#include "atum/codec/video/vp9/vp9_block_decoder.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/predict/dpb.hpp"

using namespace atum;

void test_vp9_probability_tables() {
    std::cout << "[TEST] Running VP9 Probability Table Initialization Tests..." << std::endl;

    auto ctx = Vp9ProbabilityTables::get_default_context();

    // Check partition probabilities bounds
    for (int l = 0; l < 4; ++l) {
        for (int c = 0; c < 4; ++c) {
            for (int b = 0; b < 3; ++b) {
                uint8_t p = ctx.partition_probs[l][c][b];
                assert(p > 0 && p < 255);
            }
        }
    }

    // Check compound inter and ref probabilities
    for (int i = 0; i < 5; ++i) {
        assert(ctx.comp_inter_prob[i] > 0);
        assert(ctx.comp_ref_prob[i] > 0);
    }

    std::cout << "[PASS] VP9 Canonical Probability Tables Verified." << std::endl;
}

void test_vp9_partition_and_tree() {
    std::cout << "[TEST] Running VP9 Superblock 4-Way Partitioning Tests..." << std::endl;

    // Synthetic payload for BoolDecoder
    std::vector<uint8_t> payload = { 0xAA, 0x55, 0xF0, 0x0F, 0xFF, 0x00, 0x12, 0x34 };
    Vp9BoolDecoder bool_dec(payload);
    auto ctx = Vp9ProbabilityTables::get_default_context();

    // Test partition decoding
    auto part = Vp9BlockDecoder::decode_partition(bool_dec, ctx, 3, 0);
    assert(static_cast<uint8_t>(part) <= 3);

    // Test full 64x64 superblock recursive tree
    Vp9Superblock sb{};
    sb.x = 0;
    sb.y = 0;
    sb.size = 64;
    sb.bsize_level = 3;

    auto res = Vp9BlockDecoder::decode_superblock_tree(bool_dec, ctx, false, sb);
    assert(res.is_ok());

    if (sb.partition == Vp9Partition::Split) {
        assert(sb.children.size() == 4);
        assert(sb.children[0].size == 32);
    } else {
        assert(!sb.leaf_blocks.empty());
    }

    std::cout << "[PASS] VP9 Superblock Recursive Partition Tree Verified." << std::endl;
}

void test_vp9_compound_prediction() {
    std::cout << "[TEST] Running VP9 Compound Bi-Prediction Reconstruction Tests..." << std::endl;

    DecodedPictureBuffer dpb(4);

    // Create Reference 0 (value 100)
    auto ref0 = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);
    std::memset(ref0.plane_mut(0), 100, 64 * 64);
    dpb.insert(std::move(ref0), 0, 0, true);

    // Create Reference 1 (value 200)
    auto ref1 = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);
    std::memset(ref1.plane_mut(0), 200, 64 * 64);
    dpb.insert(std::move(ref1), 1, 1, false);

    // Create Superblock with a single Compound Inter block
    Vp9Superblock sb{};
    sb.x = 0;
    sb.y = 0;
    sb.size = 64;
    sb.partition = Vp9Partition::None;

    Vp9Block comp_blk{};
    comp_blk.x = 0;
    comp_blk.y = 0;
    comp_blk.width = 64;
    comp_blk.height = 64;
    comp_blk.is_intra = false;
    comp_blk.is_compound = true;
    comp_blk.ref_frame[0] = Vp9Ref::Last;
    comp_blk.ref_frame[1] = Vp9Ref::Golden;
    comp_blk.mv[0] = {0, 0};
    comp_blk.mv[1] = {0, 0};
    sb.leaf_blocks.push_back(comp_blk);

    auto target_frame = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);
    Vp9BlockDecoder::reconstruct_superblock(sb, target_frame, dpb);

    const uint8_t* y_plane = target_frame.plane(0);
    assert(y_plane != nullptr);

    // Compound average: (100 + 200 + 1) / 2 = 150
    assert(y_plane[0] == 150);
    assert(y_plane[64 * 32 + 32] == 150);

    std::cout << "[PASS] VP9 Compound Motion Prediction Numerical Fidelity Verified (150 == 150)." << std::endl;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  Nisaba Media: VP9 Superblock & Compound Pred Tests    \n";
    std::cout << "========================================================\n";

    test_vp9_probability_tables();
    test_vp9_partition_and_tree();
    test_vp9_compound_prediction();

    std::cout << "\n[SUCCESS] All VP9 Superblock & Compound Prediction Tests Passed!\n";
    return 0;
}

#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>
#include "atum/codec/video/motion_estimate.hpp"

using namespace atum;

void test_sad_and_satd() {
    std::cout << "Testing SAD and SATD Cost Metrics..." << std::endl;

    std::vector<uint8_t> block_a(256, 100);
    std::vector<uint8_t> block_b(256, 100);

    // Identical blocks should yield 0 distortion
    assert(MotionEstimator::sad16x16(block_a.data(), 16, block_b.data(), 16) == 0);
    assert(MotionEstimator::satd16x16(block_a.data(), 16, block_b.data(), 16) == 0);

    // Constant offset: diff = 5 for all 256 pixels
    for (size_t i = 0; i < 256; ++i) block_b[i] = 105;
    assert(MotionEstimator::sad16x16(block_a.data(), 16, block_b.data(), 16) == 256 * 5);

    // Verify 8x8 and 4x4 SAD
    assert(MotionEstimator::sad8x8(block_a.data(), 16, block_b.data(), 16) == 64 * 5);
    assert(MotionEstimator::sad4x4(block_a.data(), 16, block_b.data(), 16) == 16 * 5);
    std::cout << "  -> SAD and SATD metrics verified." << std::endl;
}

void test_subpixel_interpolation() {
    std::cout << "Testing Subpixel Fractional Interpolation..." << std::endl;

    uint32_t fw = 64, fh = 64;
    std::vector<uint8_t> ref(fw * fh, 50);

    // Create a horizontal gradient: pixel(x, y) = x * 2
    for (uint32_t y = 0; y < fh; ++y) {
        for (uint32_t x = 0; x < fw; ++x) {
            ref[y * fw + x] = static_cast<uint8_t>(x * 2);
        }
    }

    std::vector<uint8_t> dst(256, 0);

    // Interpolate half-pel horizontally: (frac_x = 2, frac_y = 0)
    // At x=10, pixel is 20; at x=11, pixel is 22. Half-pel should be exactly 21!
    MotionEstimator::interpolate_block(
        ref.data(), fw,
        fw, fh,
        10, 10,
        2, 0,
        dst.data(), 16,
        16, 16
    );

    assert(dst[0] == 21);
    std::cout << "  -> Subpixel 6-tap interpolation verified." << std::endl;
}

void test_motion_search_algorithms() {
    std::cout << "Testing Motion Search Convergence (Diamond, Hexagonal, FullSearch)..." << std::endl;

    uint32_t fw = 128, fh = 128;
    std::vector<uint8_t> ref(fw * fh, 128);

    // Place a distinct 16x16 white box at (x=40, y=40)
    for (int y = 40; y < 56; ++y) {
        for (int x = 40; x < 56; ++x) {
            ref[y * fw + x] = 240;
        }
    }

    // In current frame, the box has moved to (x=46, y=43): displacement dx=+6, dy=+3
    std::vector<uint8_t> cur(fw * fh, 128);
    for (int y = 43; y < 59; ++y) {
        for (int x = 46; x < 62; ++x) {
            cur[y * fw + x] = 240;
        }
    }

    const uint8_t* cur_block = cur.data() + 43 * fw + 46;

    // 1. Test Full Search
    MotionVector mv_fs = MotionEstimator::search_integer(
        cur_block, fw,
        ref.data(), fw,
        fw, fh,
        46, 43,
        MotionVector{},
        16,
        MotionSearchMethod::FullSearch
    );
    // Displaced by (-6, -3) in reference picture
    assert(mv_fs.int_x() == -6);
    assert(mv_fs.int_y() == -3);
    assert(mv_fs.cost == 0);

    // 2. Test Hexagonal Search
    MotionVector mv_hex = MotionEstimator::search_integer(
        cur_block, fw,
        ref.data(), fw,
        fw, fh,
        46, 43,
        MotionVector{},
        16,
        MotionSearchMethod::Hexagonal
    );
    assert(mv_hex.int_x() == -6);
    assert(mv_hex.int_y() == -3);
    assert(mv_hex.cost == 0);

    // 3. Test Diamond Search
    MotionVector mv_dia = MotionEstimator::search_integer(
        cur_block, fw,
        ref.data(), fw,
        fw, fh,
        46, 43,
        MotionVector{},
        16,
        MotionSearchMethod::Diamond
    );
    assert(mv_dia.int_x() == -6);
    assert(mv_dia.int_y() == -3);
    assert(mv_dia.cost == 0);

    std::cout << "  -> FullSearch, Hexagonal, and Diamond converged to exact motion vector (-6, -3)." << std::endl;
}

void test_motion_vector_predictor() {
    std::cout << "Testing Motion Vector Predictor (MVP) Median Calculation..." << std::endl;

    MotionVector mv_l{.x = 8, .y = 4};   // Left: (2.0, 1.0)
    MotionVector mv_t{.x = 12, .y = 0};  // Top: (3.0, 0.0)
    MotionVector mv_tr{.x = 4, .y = 8};  // Top-Right: (1.0, 2.0)

    MotionVector mvp = MotionEstimator::calculate_mvp(
        mv_l, true,
        mv_t, true,
        mv_tr, true,
        MotionVector{}, false
    );

    // Median of (8, 12, 4) is 8; median of (4, 0, 8) is 4
    assert(mvp.x == 8);
    assert(mvp.y == 4);
    std::cout << "  -> MVP median calculation validated." << std::endl;
}

int main() {
    std::cout << "=== Running Motion Estimation Tests ===" << std::endl;
    test_sad_and_satd();
    test_subpixel_interpolation();
    test_motion_search_algorithms();
    test_motion_vector_predictor();
    std::cout << "[SUCCESS] All motion estimation tests passed!" << std::endl;
    return 0;
}

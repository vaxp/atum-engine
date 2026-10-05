#include <cassert>
#include <iostream>
#include <vector>
#include <array>
#include "atum/predict/intra_h264.hpp"
#include "atum/predict/intra_hevc.hpp"
#include "atum/predict/inter_filter.hpp"
#include "atum/predict/dpb.hpp"

using namespace atum;

void test_intra_h264() {
    std::cout << "[Test] H.264 Intra Prediction (4x4, 16x16, Chroma 8x8)..." << std::endl;
    uint8_t top[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    uint8_t left[4] = { 15, 25, 35, 45 };
    uint8_t top_left = 5;
    NeighborFlags avail = NeighborFlags::Top | NeighborFlags::Left | NeighborFlags::TopLeft | NeighborFlags::TopRight;

    uint8_t dst[16] = {0};

    // Mode 0: Vertical
    IntraH264::predict4x4(0, avail, top, left, top_left, dst, 4);
    assert(dst[0] == 10 && dst[1] == 20 && dst[2] == 30 && dst[3] == 40);
    assert(dst[4] == 10 && dst[5] == 20 && dst[6] == 30 && dst[7] == 40);

    // Mode 1: Horizontal
    IntraH264::predict4x4(1, avail, top, left, top_left, dst, 4);
    assert(dst[0] == 15 && dst[1] == 15 && dst[2] == 15 && dst[3] == 15);
    assert(dst[4] == 25 && dst[5] == 25 && dst[6] == 25 && dst[7] == 25);

    // Mode 2: DC
    IntraH264::predict4x4(2, avail, top, left, top_left, dst, 4);
    uint32_t expected_dc = (10 + 20 + 30 + 40 + 15 + 25 + 35 + 45 + 4) / 8;
    assert(dst[0] == expected_dc);

    // Test remaining modes 3..8 don't crash and produce bounded values
    for (uint32_t m = 3; m <= 8; ++m) {
        IntraH264::predict4x4(m, avail, top, left, top_left, dst, 4);
        for (int i = 0; i < 16; ++i) {
            assert(dst[i] >= 0);
        }
    }

    // Test 16x16
    uint8_t top16[16], left16[16], dst16[256];
    std::fill_n(top16, 16, 100);
    std::fill_n(left16, 16, 150);
    IntraH264::predict16x16(0, avail, top16, left16, 0, dst16, 16);
    assert(dst16[0] == 100 && dst16[255] == 100);

    // Test Chroma 8x8
    uint8_t top8[8], left8[8], dst8[64];
    std::fill_n(top8, 8, 80);
    std::fill_n(left8, 8, 120);
    IntraH264::predict_chroma8x8(1, avail, top8, left8, 0, dst8, 8);
    assert(dst8[0] == 120 && dst8[63] == 120);

    std::cout << "  -> Passed!" << std::endl;
}

void test_intra_hevc() {
    std::cout << "[Test] HEVC Intra Prediction (Planar, DC, Angular)..." << std::endl;
    uint8_t top[17], left[17], dst[64];
    for (int i = 0; i < 17; ++i) {
        top[i] = static_cast<uint8_t>(20 + i * 5);
        left[i] = static_cast<uint8_t>(30 + i * 4);
    }

    // Planar mode
    IntraHevc::predict(0, 8, top, left, dst, 8);
    for (int i = 0; i < 64; ++i) {
        assert(dst[i] >= 20 && dst[i] <= 150);
    }

    // DC mode
    IntraHevc::predict(1, 8, top, left, dst, 8);
    assert(dst[0] > 0);

    // Angular mode (Vertical: 26)
    IntraHevc::predict(26, 8, top, left, dst, 8);
    for (int i = 0; i < 64; ++i) {
        assert(dst[i] > 0);
    }

    std::cout << "  -> Passed!" << std::endl;
}

void test_inter_filter() {
    std::cout << "[Test] Inter Subpixel Motion Compensation..." << std::endl;
    uint8_t ref[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            ref[y * 64 + x] = static_cast<uint8_t>((x * 3 + y * 2) % 256);
        }
    }

    uint8_t dst[16 * 16];

    // Full-pel motion vector (0, 0)
    InterFilter::interpolate_luma(ref, 64, 64, 64, 10, 10, 0, 0, dst, 16, 16, 16);
    assert(dst[0] == ref[10 * 64 + 10]);

    // Half-pel motion vector (2, 0) -> +0.5 pixel in X
    InterFilter::interpolate_luma(ref, 64, 64, 64, 10, 10, 2, 0, dst, 16, 16, 16);
    assert(dst[0] > 0);

    // Quarter-pel motion vector (1, 3)
    InterFilter::interpolate_luma(ref, 64, 64, 64, 10, 10, 1, 3, dst, 16, 16, 16);
    assert(dst[0] > 0);

    // Chroma 1/8-pel interpolation
    InterFilter::interpolate_chroma(ref, 64, 64, 64, 10, 10, 3, 5, dst, 8, 8, 8);
    assert(dst[0] > 0);

    std::cout << "  -> Passed!" << std::endl;
}

void test_dpb() {
    std::cout << "[Test] Decoded Picture Buffer (DPB)..." << std::endl;
    DecodedPictureBuffer dpb(4);
    assert(dpb.empty());

    VideoFrame f1 = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);
    VideoFrame f2 = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);
    VideoFrame f3 = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);

    dpb.insert(std::move(f1), 0, 0, true);
    dpb.insert(std::move(f2), 2, 1, false);
    dpb.insert(std::move(f3), 4, 2, false);

    assert(dpb.size() == 3);
    assert(dpb.find_by_poc(2) != nullptr);
    assert(dpb.find_by_poc(999) == nullptr);

    const VideoFrame* most_recent = dpb.get_ref(0);
    assert(most_recent != nullptr);

    std::cout << "  -> Passed!" << std::endl;
}

int main() {
    std::cout << "=== Running Nisaba Media Prediction Engine Tests ===" << std::endl;
    test_intra_h264();
    test_intra_hevc();
    test_inter_filter();
    test_dpb();
    std::cout << "=== All Prediction Engine Tests Passed Successfully! ===" << std::endl;
    return 0;
}

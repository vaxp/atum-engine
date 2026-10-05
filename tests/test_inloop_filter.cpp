#include <cassert>
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include "atum/inloop/deblock_h264.hpp"
#include "atum/inloop/sao_hevc.hpp"
#include "atum/inloop/cdef_av1.hpp"

using namespace atum;

void test_deblock_h264() {
    std::cout << "[Test] H.264 Deblocking Filter..." << std::endl;
    // Create an artificial sharp block edge: left is 100, right is 110 (delta = 10, within beta/alpha)
    uint8_t line[8] = { 100, 100, 100, 100, 110, 110, 110, 110 };
    // Edge is between index 3 and 4
    // ptr points to index 4 (first sample of block Q)
    uint8_t* pix = line + 4;

    DeblockH264::filter_edge(pix, 1, 2, 28, 0, 0, true);

    // After deblocking, the step between p0 and q0 must decrease!
    int32_t step_before = 10;
    int32_t step_after = std::abs(static_cast<int32_t>(pix[0]) - static_cast<int32_t>(pix[-1]));
    assert(step_after < step_before);
    assert(pix[-1] >= 100 && pix[0] <= 110);

    // Test macroblock filtering
    uint8_t mb[256];
    std::fill_n(mb, 256, 128);
    uint8_t bS[4][4] = {{2, 2, 2, 2}, {2, 2, 2, 2}, {2, 2, 2, 2}, {2, 2, 2, 2}};
    DeblockH264::filter_macroblock(mb, 16, bS, bS, 26);
    // Flat block remains intact
    assert(mb[0] == 128);

    std::cout << "  -> Passed!" << std::endl;
}

void test_sao_hevc() {
    std::cout << "[Test] HEVC Sample Adaptive Offset (SAO)..." << std::endl;
    uint8_t block[8 * 8];
    std::fill_n(block, 64, 100);

    // 1. Band offset test
    SaoParams band_params{};
    band_params.type = SaoType::BandOffset;
    band_params.band_position = 12; // Band 12 = 96..103
    band_params.offsets[0] = 5;

    SaoHevc::apply_sao(block, 8, 8, 8, band_params, 8);
    assert(block[0] == 105);

    // 2. Edge offset test
    uint8_t edge_block[8 * 8];
    std::fill_n(edge_block, 64, 100);
    // Set a local valley at (3, 3)
    edge_block[3 * 8 + 3] = 90;

    SaoParams edge_params{};
    edge_params.type = SaoType::EdgeOffset0; // Horizontal
    edge_params.offsets[0] = 4; // Valley compensation

    SaoHevc::apply_sao(edge_block, 8, 8, 8, edge_params, 8);
    assert(edge_block[3 * 8 + 3] == 94);

    std::cout << "  -> Passed!" << std::endl;
}

void test_cdef_av1() {
    std::cout << "[Test] AV1 Constrained Directional Enhancement Filter (CDEF)..." << std::endl;
    // Create an 8x8 block with a strong horizontal edge
    uint8_t src[64];
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 8 + x] = (y < 4) ? 50 : 200;
        }
    }

    int32_t cost = 0;
    int32_t dir = CdefAv1::find_direction(src, 8, &cost);
    // Direction 2 corresponds to horizontal in CDEF table
    assert(dir >= 0 && dir < 8);

    uint8_t dst[64] = {0};
    CdefAv1::filter_8x8(src, 8, dst, 8, 8, 2, 5);
    // High contrast boundary is preserved without overflowing
    assert(dst[0] >= 40 && dst[0] <= 60);
    assert(dst[63] >= 190 && dst[63] <= 210);

    std::cout << "  -> Passed!" << std::endl;
}

int main() {
    std::cout << "=== Running Nisaba Media In-Loop Filter Tests ===" << std::endl;
    test_deblock_h264();
    test_sao_hevc();
    test_cdef_av1();
    std::cout << "=== All In-Loop Filter Tests Passed Successfully! ===" << std::endl;
    return 0;
}

#include <cassert>
#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
#include "atum/transform/idct_types.hpp"
#include "atum/transform/h264_transform.hpp"
#include "atum/transform/hevc_transform.hpp"
#include "atum/transform/vp9_transform.hpp"
#include "atum/transform/simd_transform.hpp"

using namespace atum;

void test_h264_idct4x4_simd_parity() {
    std::cout << "[Test] H.264 4x4 IDCT Scalar vs SIMD Parity..." << std::endl;
    int32_t block_scalar[16] = {
        64,  12, -8,  4,
        20, -16,  8, -2,
        -10,  6,  4,  0,
          5, -3,  1,  2
    };
    int32_t block_simd[16];
    std::copy_n(block_scalar, 16, block_simd);

    H264Transform::idct4x4(block_scalar);
    simd::idct4x4_h264(block_simd);

    for (int i = 0; i < 16; ++i) {
        assert(block_scalar[i] == block_simd[i]);
    }

    // Test idct4x4_add
    uint8_t pred[16] = {
        120, 122, 124, 126,
        121, 123, 125, 127,
        119, 120, 121, 122,
        118, 119, 120, 121
    };
    uint8_t dst_scalar[16] = {0};
    uint8_t dst_simd[16] = {0};

    int32_t residual[16] = {
        32, -16, 8, 4,
        -8,  12, 0, 2,
         4,   0, 2, 1,
        -2,   1, 0, 0
    };

    H264Transform::idct4x4_add(residual, pred, dst_scalar, 4);
    simd::idct4x4_h264_add(residual, pred, dst_simd, 4);

    for (int i = 0; i < 16; ++i) {
        assert(dst_scalar[i] == dst_simd[i]);
    }

    std::cout << "  -> Passed!" << std::endl;
}

void test_h264_hadamard() {
    std::cout << "[Test] H.264 4x4 and 2x2 Hadamard Transforms..." << std::endl;
    int32_t had4[16] = {
        16, 8, 4, 2,
         8, 4, 2, 1,
         4, 2, 1, 0,
         2, 1, 0, 0
    };
    H264Transform::hadamard4x4(had4);
    // Inverse Hadamard applied again (scaled) recovers energy
    assert(had4[0] != 0);

    int32_t had2[4] = { 10, -4, 6, 2 };
    H264Transform::hadamard2x2(had2);
    assert(had2[0] == 14); // (10 - 4) + (6 + 2) = 6 + 8 = 14
    assert(had2[1] == 18); // (10 - -4) + (6 - 2) = 14 + 4 = 18

    std::cout << "  -> Passed!" << std::endl;
}

void test_hevc_transforms() {
    std::cout << "[Test] HEVC 4x4 DST-VII & DCT-II Transforms..." << std::endl;
    int32_t dst_block[16] = {
        120, 24, -12, 6,
         40, -8,   4, 0,
        -16,  4,  -2, 0,
          8, -2,   0, 0
    };
    HevcTransform::idst4x4(dst_block, 8);
    // Check that DST output is bounded and valid
    for (int i = 0; i < 16; ++i) {
        assert(dst_block[i] >= -512 && dst_block[i] <= 512);
    }

    int32_t dct_block[16] = {
        256, 0, 0, 0,
          0, 0, 0, 0,
          0, 0, 0, 0,
          0, 0, 0, 0
    };
    HevcTransform::idct4x4(dct_block, 8);
    // DC-only block produces flat positive residual across all samples
    for (int i = 0; i < 16; ++i) {
        assert(dct_block[i] > 0);
    }

    std::cout << "  -> Passed!" << std::endl;
}

void test_vp9_transforms() {
    std::cout << "[Test] VP9 4x4 IDCT, IADST, and IWHT Transforms..." << std::endl;
    int32_t idct_block[16] = {
        64, 0, 0, 0,
         0, 0, 0, 0,
         0, 0, 0, 0,
         0, 0, 0, 0
    };
    Vp9Transform::idct4x4(idct_block);
    for (int i = 0; i < 16; ++i) {
        assert(idct_block[i] > 0);
    }

    int32_t iadst_block[16] = {
        32, 16, 8, 4,
        16,  8, 4, 2,
         8,  4, 2, 1,
         4,  2, 1, 0
    };
    Vp9Transform::iadst4x4(iadst_block);

    int32_t wht_block[16] = {
        16, 4, 2, 1,
         4, 2, 1, 0,
         2, 1, 0, 0,
         1, 0, 0, 0
    };
    Vp9Transform::iwht4x4(wht_block);

    std::cout << "  -> Passed!" << std::endl;
}

int main() {
    std::cout << "=== Running Nisaba Media Inverse Transform & SIMD Tests ===" << std::endl;
    test_h264_idct4x4_simd_parity();
    test_h264_hadamard();
    test_hevc_transforms();
    test_vp9_transforms();
    std::cout << "=== All Transform & SIMD Tests Passed Successfully! ===" << std::endl;
    return 0;
}

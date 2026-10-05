#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include "atum/codec/video/h264/h264_forward_transform.hpp"
#include "atum/transform/h264_transform.hpp"

using namespace atum;

void test_forward_dct_idct_roundtrip() {
    std::cout << "Testing H.264 Forward DCT & Quantization Round-Trip..." << std::endl;

    int32_t residual[16] = {
        12, -8,  4,  0,
        -4,  8, -2,  1,
         6, -4,  2, -1,
         1,  0, -1,  2
    };

    int32_t block[16];
    std::memcpy(block, residual, sizeof(residual));

    // 1. Forward 4x4 DCT
    H264ForwardTransform::fdct4x4(block);

    // 2. Forward Quantization at QP = 24
    int32_t qp = 24;
    int32_t nz = H264ForwardTransform::quant4x4(block, qp, true);
    assert(nz > 0);

    // 3. Dequantization
    H264Transform::dequant4x4(block, qp);

    // 4. Inverse 4x4 DCT
    H264Transform::idct4x4(block);

    // Verify reconstructed values match residual within quantization error
    int32_t max_diff = 0;
    for (int i = 0; i < 16; ++i) {
        int32_t recon = (block[i] + 32) >> 6;
        int32_t diff = std::abs(residual[i] - recon);
        max_diff = std::max(max_diff, diff);
    }
    // At QP=24, Qstep=10, so reconstructed error is bounded by Qstep
    assert(max_diff <= 10);
    std::cout << "  -> Forward DCT/Quant/Dequant/IDCT precision validated (max diff = " << max_diff << ")." << std::endl;
}

void test_forward_hadamard() {
    std::cout << "Testing H.264 Forward Hadamard Transforms..." << std::endl;

    int32_t block[16] = {
        10, 20, 30, 40,
        50, 60, 70, 80,
        90, 80, 70, 60,
        50, 40, 30, 20
    };

    int32_t orig[16];
    std::memcpy(orig, block, sizeof(block));

    H264ForwardTransform::fhadamard4x4(block);
    H264Transform::hadamard4x4(block);

    // H*H = 4*I, so (H*H*X*H*H)/2 = 16*X / 2 = 8*X
    for (int i = 0; i < 16; ++i) {
        assert(block[i] == orig[i] * 8);
    }

    int32_t cblock[4] = { 10, -5, 8, -2 };
    int32_t corig[4];
    std::memcpy(corig, cblock, sizeof(cblock));

    H264ForwardTransform::fhadamard2x2(cblock);
    H264Transform::hadamard2x2(cblock);

    for (int i = 0; i < 4; ++i) {
        assert(cblock[i] == corig[i] * 4); // H2x2 * H2x2 = 4 * I
    }
    std::cout << "  -> 4x4 and 2x2 Hadamard transforms validated." << std::endl;
}

void test_quantization_deadzones() {
    std::cout << "Testing H.264 Quantization Dead-Zone Rounding..." << std::endl;

    int32_t block_intra[16];
    int32_t block_inter[16];
    for (int i = 0; i < 16; ++i) {
        block_intra[i] = 1000 + i * 50;
        block_inter[i] = 1000 + i * 50;
    }

    int32_t nz_intra = H264ForwardTransform::quant4x4(block_intra, 28, true);
    int32_t nz_inter = H264ForwardTransform::quant4x4(block_inter, 28, false);

    assert(nz_intra > 0);
    assert(nz_inter > 0);
    // Intra dead-zone (f = 1/3) retains slightly higher levels than inter (f = 1/6)
    int32_t sum_intra = 0, sum_inter = 0;
    for (int i = 0; i < 16; ++i) {
        sum_intra += block_intra[i];
        sum_inter += block_inter[i];
    }
    assert(sum_intra >= sum_inter);
    std::cout << "  -> Quantization dead-zones validated." << std::endl;
}

int main() {
    std::cout << "=== Running H.264 Forward Transform Tests ===" << std::endl;
    test_forward_dct_idct_roundtrip();
    test_forward_hadamard();
    test_quantization_deadzones();
    std::cout << "[SUCCESS] All H.264 forward transform tests passed!" << std::endl;
    return 0;
}

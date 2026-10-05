#include <cassert>
#include <iostream>
#include <cmath>
#include <vector>
#include "atum/color/transfer_function.hpp"

using namespace atum;

static void test_pq_transfer() {
    std::cout << "Testing SMPTE ST 2084 (PQ) EOTF / OETF..." << std::endl;

    // 0 nits maps to 0
    assert(TransferFunction::pq_eotf(0.0f) == 0.0f);
    assert(TransferFunction::pq_oetf(0.0f) == 0.0f);

    // 1.0 (10,000 nits) maps to 10,000 nits
    const float peak = TransferFunction::pq_eotf_nits(1.0f);
    assert(std::fabs(peak - 10000.0f) < 1.0f);

    // Round-trip test: non_linear -> linear -> non_linear
    for (int i = 1; i <= 100; ++i) {
        const float code = static_cast<float>(i) / 100.0f;
        const float linear = TransferFunction::pq_eotf(code);
        const float round_trip = TransferFunction::pq_oetf(linear);
        assert(std::fabs(round_trip - code) < 1e-4f);
    }

    // Direct nits round-trip
    const std::vector<float> test_nits = { 0.005f, 0.1f, 1.0f, 10.0f, 100.0f, 400.0f, 1000.0f, 4000.0f, 10000.0f };
    for (float nits : test_nits) {
        const float code = TransferFunction::pq_oetf_nits(nits);
        const float recovered = TransferFunction::pq_eotf_nits(code);
        const float rel_err = std::fabs(recovered - nits) / nits;
        assert(rel_err < 1e-3f);
    }
    std::cout << "  PQ Transfer OK!" << std::endl;
}

static void test_hlg_transfer() {
    std::cout << "Testing ARIB STD-B67 (HLG) OETF / Inverse OETF..." << std::endl;

    // 0 maps to 0
    assert(TransferFunction::hlg_oetf(0.0f) == 0.0f);
    assert(TransferFunction::hlg_inverse_oetf(0.0f) == 0.0f);

    // Round-trip test
    for (int i = 1; i <= 100; ++i) {
        const float scene_linear = static_cast<float>(i) / 100.0f;
        const float code = TransferFunction::hlg_oetf(scene_linear);
        const float recovered = TransferFunction::hlg_inverse_oetf(code);
        assert(std::fabs(recovered - scene_linear) < 1e-4f);
    }

    // HLG EOTF at 1000 nits nominal display peak
    const float lum_white = TransferFunction::hlg_eotf_nits(1.0f, 1000.0f);
    assert(std::fabs(lum_white - 1000.0f) < 1.0f);

    std::cout << "  HLG Transfer OK!" << std::endl;
}

static void test_bt709_and_srgb() {
    std::cout << "Testing BT.709 & sRGB Transfer Functions..." << std::endl;

    // BT.709 round-trip
    for (int i = 0; i <= 100; ++i) {
        const float linear = static_cast<float>(i) / 100.0f;
        const float code = TransferFunction::bt709_oetf(linear);
        const float recovered = TransferFunction::bt709_inverse_oetf(code);
        assert(std::fabs(recovered - linear) < 1e-4f);
    }

    // sRGB round-trip
    for (int i = 0; i <= 100; ++i) {
        const float linear = static_cast<float>(i) / 100.0f;
        const float code = TransferFunction::srgb_oetf(linear);
        const float recovered = TransferFunction::srgb_inverse_oetf(code);
        assert(std::fabs(recovered - linear) < 1e-4f);
    }

    std::cout << "  BT.709 and sRGB Transfer OK!" << std::endl;
}

static void test_transfer_lut() {
    std::cout << "Testing Fast 1024-entry TransferFunctionLUT..." << std::endl;

    auto pq_lut = TransferFunctionLUT::create_pq_eotf();

    // Verify interpolation accuracy across 10,000 sub-step points
    for (int i = 0; i <= 10000; ++i) {
        const float x = static_cast<float>(i) / 10000.0f;
        const float math_val = TransferFunction::pq_eotf(x);
        const float lut_val = pq_lut.evaluate(x);
        // Error should be within tiny interpolation margin
        assert(std::fabs(lut_val - math_val) < 5e-4f);
    }

    // Batch evaluation
    std::vector<float> src(2048);
    std::vector<float> dst(2048);
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<float>(i) / static_cast<float>(src.size() - 1);
    }

    pq_lut.evaluate_batch(src.data(), dst.data(), src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        const float expected = pq_lut.evaluate(src[i]);
        assert(std::fabs(dst[i] - expected) < 1e-6f);
    }

    std::cout << "  TransferFunctionLUT OK!" << std::endl;
}

int main() {
    std::cout << "=== Running test_hdr_color_science ===" << std::endl;
    test_pq_transfer();
    test_hlg_transfer();
    test_bt709_and_srgb();
    test_transfer_lut();
    std::cout << "=== ALL test_hdr_color_science PASSED ===" << std::endl;
    return 0;
}

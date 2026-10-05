#include <cassert>
#include <iostream>
#include <cmath>
#include <vector>
#include "atum/color/tone_mapper.hpp"

using namespace atum;

static void test_tone_curves() {
    std::cout << "Testing Tone Mapping Curves (Reinhard, ACES, Hable)..." << std::endl;

    // 0 maps to 0
    assert(ToneMapper::reinhard(0.0f) == 0.0f);
    assert(ToneMapper::reinhard_extended(0.0f, 4.0f) == 0.0f);
    assert(ToneMapper::aces_filmic(0.0f) == 0.0f);
    assert(ToneMapper::hable(0.0f) == 0.0f);

    // Monotonicity test: f(x2) >= f(x1) for x2 > x1
    float prev_r = 0.0f, prev_re = 0.0f, prev_aces = 0.0f, prev_hable = 0.0f;
    for (int i = 1; i <= 200; ++i) {
        const float x = static_cast<float>(i) * 0.1f; // 0.1 to 20.0
        const float r = ToneMapper::reinhard(x);
        const float re = ToneMapper::reinhard_extended(x, 4.0f);
        const float aces = ToneMapper::aces_filmic(x);
        const float hab = ToneMapper::hable(x);

        assert(r >= prev_r);
        assert(re >= prev_re);
        assert(aces >= prev_aces);
        assert(hab >= prev_hable);

        // Range constraint: must stay in [0, 1]
        assert(r <= 1.0f);
        assert(aces <= 1.0f);
        assert(hab <= 1.0f);

        prev_r = r;
        prev_re = re;
        prev_aces = aces;
        prev_hable = hab;
    }

    std::cout << "  Tone Curves OK!" << std::endl;
}

static void test_chromaticity_preservation() {
    std::cout << "Testing Chromaticity Preservation in Luminance Tone Mapping..." << std::endl;

    ToneMapper tm;
    tm.op = ToneMappingOperator::ACES_FILMIC;
    tm.preserve_chromaticity = true;
    tm.primaries = ColorPrimaries::BT2020;

    // Saturated HDR color: R: 5.0, G: 2.0, B: 0.5
    const float in_r = 5.0f;
    const float in_g = 2.0f;
    const float in_b = 0.5f;

    const float ratio_rg_in = in_r / in_g;
    const float ratio_bg_in = in_b / in_g;

    float out_r = 0, out_g = 0, out_b = 0;
    tm.map_rgb(in_r, in_g, in_b, out_r, out_g, out_b);

    // After tone mapping with chromaticity preservation, ratios must be preserved
    const float ratio_rg_out = out_r / out_g;
    const float ratio_bg_out = out_b / out_g;

    assert(std::fabs(ratio_rg_out - ratio_rg_in) < 1e-4f);
    assert(std::fabs(ratio_bg_out - ratio_bg_in) < 1e-4f);

    // Outputs must be strictly clamped to SDR [0, 1]
    assert(out_r >= 0.0f && out_r <= 1.0f);
    assert(out_g >= 0.0f && out_g <= 1.0f);
    assert(out_b >= 0.0f && out_b <= 1.0f);

    std::cout << "  Chromaticity Preservation OK!" << std::endl;
}

static void test_batch_tone_mapping() {
    std::cout << "Testing Batch In-Place Tone Mapping..." << std::endl;

    ToneMapper tm;
    tm.op = ToneMappingOperator::HABLE;
    tm.preserve_chromaticity = true;

    constexpr size_t N = 64;
    std::vector<float> rgba(N * 4);
    for (size_t i = 0; i < N; ++i) {
        rgba[i * 4 + 0] = static_cast<float>(i) * 0.2f;
        rgba[i * 4 + 1] = static_cast<float>(i) * 0.1f;
        rgba[i * 4 + 2] = static_cast<float>(i) * 0.05f;
        rgba[i * 4 + 3] = 1.0f;
    }

    tm.map_rgba_inplace(rgba.data(), N);

    for (size_t i = 0; i < N; ++i) {
        assert(rgba[i * 4 + 0] >= 0.0f && rgba[i * 4 + 0] <= 1.0f);
        assert(rgba[i * 4 + 1] >= 0.0f && rgba[i * 4 + 1] <= 1.0f);
        assert(rgba[i * 4 + 2] >= 0.0f && rgba[i * 4 + 2] <= 1.0f);
        assert(rgba[i * 4 + 3] == 1.0f); // Alpha intact
    }

    std::cout << "  Batch Tone Mapping OK!" << std::endl;
}

int main() {
    std::cout << "=== Running test_tone_mapping ===" << std::endl;
    test_tone_curves();
    test_chromaticity_preservation();
    test_batch_tone_mapping();
    std::cout << "=== ALL test_tone_mapping PASSED ===" << std::endl;
    return 0;
}

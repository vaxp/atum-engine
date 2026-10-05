#include <cassert>
#include <iostream>
#include <cmath>
#include <vector>
#include "atum/color/color_gamut.hpp"

using namespace atum;

static void test_gamut_matrices() {
    std::cout << "Testing ColorMatrix3x3 gamut conversion matrices..." << std::endl;

    const auto m_2020_to_709 = ColorMatrix3x3::bt2020_to_bt709();
    const auto m_709_to_2020 = ColorMatrix3x3::bt709_to_bt2020();

    // 1. D65 White point preservation: [1, 1, 1] -> [1, 1, 1]
    float r = 0, g = 0, b = 0;
    m_2020_to_709.transform(1.0f, 1.0f, 1.0f, r, g, b);
    assert(std::fabs(r - 1.0f) < 1e-4f);
    assert(std::fabs(g - 1.0f) < 1e-4f);
    assert(std::fabs(b - 1.0f) < 1e-4f);

    m_709_to_2020.transform(1.0f, 1.0f, 1.0f, r, g, b);
    assert(std::fabs(r - 1.0f) < 1e-4f);
    assert(std::fabs(g - 1.0f) < 1e-4f);
    assert(std::fabs(b - 1.0f) < 1e-4f);

    // 2. Matrix inversion round-trip: M * M_inv = Identity
    const auto round_trip = m_2020_to_709 * m_709_to_2020;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float expected = (row == col) ? 1.0f : 0.0f;
            assert(std::fabs(round_trip.m[row * 3 + col] - expected) < 1e-3f);
        }
    }

    // 3. DCI-P3 transforms
    const auto m_2020_to_p3 = ColorMatrix3x3::bt2020_to_dcip3();
    const auto m_p3_to_2020 = ColorMatrix3x3::dcip3_to_bt2020();
    m_2020_to_p3.transform(1.0f, 1.0f, 1.0f, r, g, b);
    assert(std::fabs(r - 1.0f) < 1e-4f);
    assert(std::fabs(g - 1.0f) < 1e-4f);
    assert(std::fabs(b - 1.0f) < 1e-4f);

    const auto round_trip_p3 = m_2020_to_p3 * m_p3_to_2020;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float expected = (row == col) ? 1.0f : 0.0f;
            assert(std::fabs(round_trip_p3.m[row * 3 + col] - expected) < 1e-3f);
        }
    }

    // 4. get_transform dispatch
    const auto disp_2020_709 = ColorMatrix3x3::get_transform(ColorPrimaries::BT2020, ColorPrimaries::BT709);
    for (size_t i = 0; i < 9; ++i) {
        assert(disp_2020_709.m[i] == m_2020_to_709.m[i]);
    }

    const auto disp_same = ColorMatrix3x3::get_transform(ColorPrimaries::BT709, ColorPrimaries::BT709);
    for (size_t i = 0; i < 9; ++i) {
        const float expected = (i % 4 == 0) ? 1.0f : 0.0f;
        assert(disp_same.m[i] == expected);
    }

    std::cout << "  Gamut Matrices OK!" << std::endl;
}

static void test_batch_gamut_transforms() {
    std::cout << "Testing SIMD & Batch Gamut Transformations..." << std::endl;

    const auto m_2020_to_709 = ColorMatrix3x3::bt2020_to_bt709();
    constexpr size_t N = 128;

    std::vector<float> r_in(N), g_in(N), b_in(N);
    std::vector<float> r_out(N), g_out(N), b_out(N);

    for (size_t i = 0; i < N; ++i) {
        r_in[i] = static_cast<float>(i) / static_cast<float>(N);
        g_in[i] = 1.0f - r_in[i];
        b_in[i] = 0.5f;
    }

    m_2020_to_709.transform_batch(r_in.data(), g_in.data(), b_in.data(),
                                  r_out.data(), g_out.data(), b_out.data(), N);

    for (size_t i = 0; i < N; ++i) {
        float expected_r = 0, expected_g = 0, expected_b = 0;
        m_2020_to_709.transform(r_in[i], g_in[i], b_in[i], expected_r, expected_g, expected_b);
        assert(std::fabs(r_out[i] - expected_r) < 1e-5f);
        assert(std::fabs(g_out[i] - expected_g) < 1e-5f);
        assert(std::fabs(b_out[i] - expected_b) < 1e-5f);
    }

    // Interleaved RGBA batch
    std::vector<float> rgba(N * 4);
    for (size_t i = 0; i < N; ++i) {
        rgba[i * 4 + 0] = r_in[i];
        rgba[i * 4 + 1] = g_in[i];
        rgba[i * 4 + 2] = b_in[i];
        rgba[i * 4 + 3] = 1.0f;
    }

    m_2020_to_709.transform_rgba_batch(rgba.data(), N);

    for (size_t i = 0; i < N; ++i) {
        assert(std::fabs(rgba[i * 4 + 0] - r_out[i]) < 1e-5f);
        assert(std::fabs(rgba[i * 4 + 1] - g_out[i]) < 1e-5f);
        assert(std::fabs(rgba[i * 4 + 2] - b_out[i]) < 1e-5f);
        assert(rgba[i * 4 + 3] == 1.0f); // Alpha preserved
    }

    std::cout << "  Batch Gamut Transforms OK!" << std::endl;
}

int main() {
    std::cout << "=== Running test_color_gamut ===" << std::endl;
    test_gamut_matrices();
    test_batch_gamut_transforms();
    std::cout << "=== ALL test_color_gamut PASSED ===" << std::endl;
    return 0;
}

#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include "atum/entropy/av1_symbol_decoder.hpp"
#include "atum/entropy/av1_cdf_tables.hpp"
#include "atum/codec/video/av1/av1_partition.hpp"
#include "atum/inloop/av1_restoration.hpp"
#include "atum/inloop/av1_film_grain.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/predict/dpb.hpp"

using namespace atum;

void test_av1_cdf_and_partitions() {
    std::cout << "[TEST] Running AV1 10-Way Partition & CDF Model Tests..." << std::endl;

    auto ctx = Av1CdfTables::get_default_context();

    // 1. Verify 10-way partition CDF structure
    for (size_t c = 0; c < 10; ++c) {
        for (size_t s = 0; s < 9; ++s) {
            assert(ctx.partition_cdf[c][s] < ctx.partition_cdf[c][s + 1]);
        }
        assert(ctx.partition_cdf[c][9] == 32768);
    }

    // 2. Decode 10-way partition with Av1SymbolDecoder
    std::vector<uint8_t> stream = { 0x40, 0x00, 0x80, 0x20, 0x10, 0x08, 0x04, 0x02 };
    Av1SymbolDecoder sym_dec(stream);

    auto part = Av1PartitionDecoder::decode_partition(sym_dec, ctx, 0);
    assert(static_cast<uint8_t>(part) < 10);

    // 3. Superblock Tree
    Av1Superblock sb{};
    sb.x = 0;
    sb.y = 0;
    sb.size = 64;

    auto res = Av1PartitionDecoder::decode_superblock_tree(sym_dec, ctx, true, sb);
    assert(res.is_ok());
    assert(sb.children.size() == 4 || !sb.leaf_blocks.empty());

    std::cout << "[PASS] AV1 10-Way Partition & Multi-Symbol CDF Models Verified." << std::endl;
}

void test_av1_dual_loop_restoration() {
    std::cout << "[TEST] Running AV1 Dual Loop Restoration (Wiener + Sgrproj) Tests..." << std::endl;

    auto frame = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);
    uint8_t* y_plane = frame.plane_mut(0);
    size_t stride = frame.stride(0);

    // Fill with test gradient pattern
    for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            y_plane[y * stride + x] = static_cast<uint8_t>((x * 3 + y * 2) % 256);
        }
    }

    uint8_t initial_sample = y_plane[32 * stride + 32];
    (void)initial_sample;

    // 1. Apply 7-tap separable Wiener Filter
    const int16_t h_taps[3] = { -4, 8, 28 };
    const int16_t v_taps[3] = { -4, 8, 28 };
    Av1LoopRestoration::apply_wiener(y_plane, stride, 64, 64, h_taps, v_taps);

    // Pixel values remain smooth and valid 8-bit range
    uint8_t wiener_sample = y_plane[32 * stride + 32];
    assert(wiener_sample > 0 && wiener_sample < 255);

    // 2. Apply Self-Guided Restoration (Sgrproj)
    Av1LoopRestoration::apply_sgrproj(y_plane, stride, 64, 64, 32, 32, 2, 1);
    uint8_t sgr_sample = y_plane[32 * stride + 32];
    assert(sgr_sample > 0 && sgr_sample < 255);

    std::cout << "[PASS] AV1 7-Tap Separable Wiener & SGRproj Filters Verified." << std::endl;
}

void test_av1_film_grain_synthesis() {
    std::cout << "[TEST] Running AV1 Film Grain Synthesis & AR Filter Tests..." << std::endl;

    Av1FilmGrainParams params{};
    params.apply_grain = true;
    params.grain_seed = 0x1234;
    params.scaling_shift = 8;
    params.ar_coeff_lag = 1;
    params.ar_coeffs_y = { 4, 8, -4, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    params.num_y_points = 2;
    params.scaling_points_y[0] = { 0, 10 };
    params.scaling_points_y[1] = { 255, 30 };

    // 1. Piecewise scaling test
    assert(Av1FilmGrainSynthesizer::get_scaling_factor(0, params) == 10);
    assert(Av1FilmGrainSynthesizer::get_scaling_factor(255, params) == 30);
    uint8_t mid_scale = Av1FilmGrainSynthesizer::get_scaling_factor(128, params);
    assert(mid_scale >= 19 && mid_scale <= 21);

    // 2. Grain Block synthesis
    int8_t grain_template[64 * 64];
    uint16_t seed = params.grain_seed;
    Av1FilmGrainSynthesizer::generate_grain_block(seed, grain_template, params.ar_coeff_lag, params.ar_coeffs_y.data());

    // Check grain variance
    int32_t total_abs = 0;
    for (size_t i = 0; i < 64 * 64; ++i) {
        total_abs += std::abs(grain_template[i]);
    }
    assert(total_abs > 0);

    // 3. Apply to Frame
    auto frame = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);
    std::memset(frame.plane_mut(0), 128, 64 * 64);
    Av1FilmGrainSynthesizer::apply_grain_to_frame(frame, params);

    // Verify non-uniform synthesized texture
    const uint8_t* y_plane = frame.plane(0);
    bool has_variation = false;
    for (size_t i = 1; i < 64 * 64; ++i) {
        if (y_plane[i] != y_plane[0]) {
            has_variation = true;
            break;
        }
    }
    assert(has_variation);

    std::cout << "[PASS] AV1 Film Grain AR Synthesis & Piecewise Blending Verified." << std::endl;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  Nisaba Media: AV1 Advanced Partitions & Filters Tests \n";
    std::cout << "========================================================\n";

    test_av1_cdf_and_partitions();
    test_av1_dual_loop_restoration();
    test_av1_film_grain_synthesis();

    std::cout << "\n[SUCCESS] All AV1 Advanced Architecture Tests Passed Cleanly!\n";
    return 0;
}

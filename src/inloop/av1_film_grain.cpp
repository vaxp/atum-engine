#include "atum/inloop/av1_film_grain.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

namespace {

inline uint16_t lcg_next(uint16_t seed) noexcept {
    return static_cast<uint16_t>((seed * 1103515245u + 12345u) & 0xFFFFu);
}

} // namespace

void Av1FilmGrainSynthesizer::generate_grain_block(
    uint16_t& seed,
    int8_t grain_block[64 * 64],
    uint8_t ar_lag,
    const int8_t* ar_coeffs
) noexcept {
    // 1. Generate uniform pseudo-random Gaussian approximation template
    for (size_t i = 0; i < 64 * 64; ++i) {
        seed = lcg_next(seed);
        int32_t r1 = static_cast<int32_t>(seed >> 8) - 128;
        seed = lcg_next(seed);
        int32_t r2 = static_cast<int32_t>(seed >> 8) - 128;
        grain_block[i] = static_cast<int8_t>(std::clamp((r1 + r2) >> 1, -128, 127));
    }

    // 2. Apply Auto-Regressive (AR) filter if lag > 0
    if (ar_lag > 0 && ar_coeffs) {
        for (int32_t y = 0; y < 64; ++y) {
            for (int32_t x = 0; x < 64; ++x) {
                int32_t ar_sum = 0;
                size_t coeff_idx = 0;

                for (int32_t dy = -static_cast<int32_t>(ar_lag); dy <= 0; ++dy) {
                    for (int32_t dx = -static_cast<int32_t>(ar_lag); dx <= static_cast<int32_t>(ar_lag); ++dx) {
                        if (dy == 0 && dx >= 0) break; // Causal filter
                        int32_t sy = std::clamp<int32_t>(y + dy, 0, 63);
                        int32_t sx = std::clamp<int32_t>(x + dx, 0, 63);
                        ar_sum += grain_block[sy * 64 + sx] * ar_coeffs[coeff_idx++];
                        if (coeff_idx >= 24) break;
                    }
                    if (coeff_idx >= 24) break;
                }

                int32_t cur = grain_block[y * 64 + x] + (ar_sum >> 6);
                grain_block[y * 64 + x] = static_cast<int8_t>(std::clamp(cur, -128, 127));
            }
        }
    }
}

uint8_t Av1FilmGrainSynthesizer::get_scaling_factor(
    uint8_t intensity,
    const Av1FilmGrainParams& params
) noexcept {
    if (params.num_y_points == 0) return 0;
    if (params.num_y_points == 1) return params.scaling_points_y[0][1];

    if (intensity <= params.scaling_points_y[0][0]) {
        return params.scaling_points_y[0][1];
    }
    if (intensity >= params.scaling_points_y[params.num_y_points - 1][0]) {
        return params.scaling_points_y[params.num_y_points - 1][1];
    }

    // Piecewise linear interpolation
    for (size_t i = 0; i + 1 < static_cast<size_t>(params.num_y_points); ++i) {
        uint8_t x0 = params.scaling_points_y[i][0];
        uint8_t x1 = params.scaling_points_y[i + 1][0];
        if (intensity >= x0 && intensity <= x1) {
            uint8_t y0 = params.scaling_points_y[i][1];
            uint8_t y1 = params.scaling_points_y[i + 1][1];
            int32_t dx = x1 - x0;
            if (dx == 0) return y0;
            int32_t dy = y1 - y0;
            return static_cast<uint8_t>(y0 + ((intensity - x0) * dy) / dx);
        }
    }
    return params.scaling_points_y[params.num_y_points - 1][1];
}

void Av1FilmGrainSynthesizer::apply_grain_to_frame(
    VideoFrame& frame,
    const Av1FilmGrainParams& params
) noexcept {
    if (!params.apply_grain) return;

    uint8_t* y_plane = frame.plane_mut(0);
    if (!y_plane) return;

    size_t stride = frame.stride(0);
    uint32_t width = frame.width();
    uint32_t height = frame.height();

    uint16_t seed = params.grain_seed;
    int8_t grain_template[64 * 64];
    generate_grain_block(seed, grain_template, params.ar_coeff_lag, params.ar_coeffs_y.data());

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t orig = y_plane[y * stride + x];
            uint8_t scale = get_scaling_factor(orig, params);

            int32_t grain_val = grain_template[(y % 64) * 64 + (x % 64)];
            int32_t delta = (grain_val * scale) >> params.scaling_shift;

            y_plane[y * stride + x] = static_cast<uint8_t>(std::clamp(static_cast<int32_t>(orig) + delta, 0, 255));
        }
    }
}

} // namespace atum

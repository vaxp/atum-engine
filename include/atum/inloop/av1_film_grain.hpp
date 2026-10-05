#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"

namespace atum {

/// AV1 Film Grain Synthesis Parameters (Section 6.8.20 / 7.18).
struct Av1FilmGrainParams {
    bool apply_grain{false};
    uint16_t grain_seed{0};
    uint8_t scaling_shift{8};
    uint8_t ar_coeff_lag{0}; // 0..3
    std::array<int8_t, 24> ar_coeffs_y{};
    uint8_t num_y_points{0};
    std::array<std::array<uint8_t, 2>, 14> scaling_points_y{};
};

/// Sovereign AV1 Film Grain Synthesizer Engine.
/// Reconstructs film grain characteristics without compromising bitstream transmission bandwidth.
class Av1FilmGrainSynthesizer {
public:
    /// Synthesizes pseudo-random grain across 64x64 block template using AV1 LCG & AR filter.
    static void generate_grain_block(
        uint16_t& seed,
        int8_t grain_block[64 * 64],
        uint8_t ar_lag,
        const int8_t* ar_coeffs
    ) noexcept;

    /// Evaluates piecewise-linear scaling LUT for a given luminance intensity.
    [[nodiscard]] static uint8_t get_scaling_factor(
        uint8_t intensity,
        const Av1FilmGrainParams& params
    ) noexcept;

    /// Synthesizes and applies film grain to the entire video frame.
    static void apply_grain_to_frame(
        VideoFrame& frame,
        const Av1FilmGrainParams& params
    ) noexcept;
};

} // namespace atum

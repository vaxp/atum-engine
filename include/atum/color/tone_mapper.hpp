#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "atum/color/hdr_metadata.hpp"

namespace atum {

/// Tone mapping operator algorithms
enum class ToneMappingOperator : uint8_t {
    NONE = 0,
    REINHARD = 1,
    REINHARD_EXTENDED = 2,
    ACES_FILMIC = 3,
    HABLE = 4,
};

/// High-performance tone mapping configuration and mathematical operators
/// for compressing High Dynamic Range (HDR) content down to Standard Dynamic Range (SDR)
/// while faithfully preserving chromaticity, contrast, and color fidelity.
class ToneMapper {
public:
    ToneMappingOperator op{ToneMappingOperator::ACES_FILMIC};
    float exposure{1.0f};
    float white_point{4.0f}; // Maximum luminance mapped to pure white for Reinhard Extended
    bool preserve_chromaticity{true};
    ColorPrimaries primaries{ColorPrimaries::BT2020};

    // =========================================================================
    // Core scalar tone mapping curves mapping [0, inf) -> [0, 1]
    // =========================================================================

    /// Simple Reinhard: x / (1 + x)
    [[nodiscard]] static inline float reinhard(float x) noexcept {
        if (x <= 0.0f) return 0.0f;
        return x / (1.0f + x);
    }

    /// Extended Reinhard: x * (1 + x / (w^2)) / (1 + x)
    [[nodiscard]] static inline float reinhard_extended(float x, float w = 4.0f) noexcept {
        if (x <= 0.0f) return 0.0f;
        const float w2 = w * w;
        return (x * (1.0f + x / w2)) / (1.0f + x);
    }

    /// ACES Filmic Tone Mapping Curve (Krzysztof Narkowicz fit)
    /// Widely used in film, Unreal Engine, and broadcast production.
    [[nodiscard]] static inline float aces_filmic(float x) noexcept {
        if (x <= 0.0f) return 0.0f;
        constexpr float a = 2.51f;
        constexpr float b = 0.03f;
        constexpr float c = 2.43f;
        constexpr float d = 0.59f;
        constexpr float e = 0.14f;
        return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
    }

    /// John Hable's Filmic curve (Uncharted 2)
    [[nodiscard]] static inline float hable(float x) noexcept {
        if (x <= 0.0f) return 0.0f;
        auto hable_curve = [](float v) noexcept -> float {
            constexpr float A = 0.15f; // Shoulder Strength
            constexpr float B = 0.50f; // Linear Strength
            constexpr float C = 0.10f; // Linear Angle
            constexpr float D = 0.20f; // Toe Strength
            constexpr float E = 0.02f; // Toe Numerator
            constexpr float F = 0.30f; // Toe Denominator
            return ((v * (A * v + C * B) + D * E) / (v * (A * v + B) + D * F)) - E / F;
        };
        constexpr float white_scale = 1.0f / 0.835492f; // 1.0 / hable_curve(11.2)
        return std::clamp(hable_curve(x) * white_scale, 0.0f, 1.0f);
    }

    /// Evaluate selected scalar tone curve
    [[nodiscard]] inline float evaluate_curve(float x) const noexcept {
        x *= exposure;
        switch (op) {
            case ToneMappingOperator::REINHARD:
                return reinhard(x);
            case ToneMappingOperator::REINHARD_EXTENDED:
                return reinhard_extended(x, white_point);
            case ToneMappingOperator::ACES_FILMIC:
                return aces_filmic(x);
            case ToneMappingOperator::HABLE:
                return hable(x);
            case ToneMappingOperator::NONE:
            default:
                return std::clamp(x, 0.0f, 1.0f);
        }
    }

    /// Map a linear RGB triplet with chromaticity preservation
    void map_rgb(float r, float g, float b, float& out_r, float& out_g, float& out_b) const noexcept;

    /// Batch map planar linear RGB buffers
    void map_batch(const float* __restrict r_in,
                   const float* __restrict g_in,
                   const float* __restrict b_in,
                   float* __restrict r_out,
                   float* __restrict g_out,
                   float* __restrict b_out,
                   size_t count) const noexcept;

    /// Batch map interleaved linear RGBA float buffer in-place
    void map_rgba_inplace(float* __restrict rgba, size_t pixel_count) const noexcept;

private:
    void get_luminance_weights(float& kr, float& kg, float& kb) const noexcept;
};

} // namespace atum

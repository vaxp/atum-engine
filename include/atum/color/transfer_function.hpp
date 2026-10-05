#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include "atum/color/hdr_metadata.hpp"

namespace atum {

/// Mathematical implementations of broadcast and cinematic electro-optical
/// and opto-electrical transfer functions (EOTF / OETF).
class TransferFunction {
public:
    // =========================================================================
    // SMPTE ST 2084 (Perceptual Quantizer - PQ)
    // Dynamic range: 0.0001 cd/m^2 to 10,000 cd/m^2
    // =========================================================================

    static constexpr float PQ_M1 = 2610.0f / 16384.0f;          // 0.1593017578125f
    static constexpr float PQ_M2 = (2523.0f / 4096.0f) * 128.0f; // 78.84375f
    static constexpr float PQ_C1 = 3424.0f / 4096.0f;           // 0.8359375f
    static constexpr float PQ_C2 = (2413.0f / 4096.0f) * 32.0f;  // 18.8515625f
    static constexpr float PQ_C3 = (2392.0f / 4096.0f) * 32.0f;  // 18.6875f
    static constexpr float PQ_PEAK_LUMINANCE = 10000.0f;        // 10,000 nits

    /// PQ EOTF: Non-linear code value [0.0, 1.0] -> Linear luminance in cd/m^2 (nits) [0.0, 10000.0]
    [[nodiscard]] static inline float pq_eotf_nits(float n) noexcept {
        n = std::clamp(n, 0.0f, 1.0f);
        if (n <= 0.0f) return 0.0f;
        const float np = std::pow(n, 1.0f / PQ_M2);
        const float num = std::max(np - PQ_C1, 0.0f);
        const float den = PQ_C2 - PQ_C3 * np;
        if (den <= 0.0f) return PQ_PEAK_LUMINANCE;
        const float linear_norm = std::pow(num / den, 1.0f / PQ_M1);
        return linear_norm * PQ_PEAK_LUMINANCE;
    }

    /// PQ EOTF: Non-linear code value [0.0, 1.0] -> Normalized linear light [0.0, 1.0] (where 1.0 = 10,000 nits)
    [[nodiscard]] static inline float pq_eotf(float n) noexcept {
        return pq_eotf_nits(n) / PQ_PEAK_LUMINANCE;
    }

    /// PQ OETF / Inverse EOTF: Linear luminance in cd/m^2 (nits) [0.0, 10000.0] -> Non-linear code value [0.0, 1.0]
    [[nodiscard]] static inline float pq_oetf_nits(float l_nits) noexcept {
        if (l_nits <= 0.0f) return 0.0f;
        const float y = std::clamp(l_nits / PQ_PEAK_LUMINANCE, 0.0f, 1.0f);
        const float ym1 = std::pow(y, PQ_M1);
        const float num = PQ_C1 + PQ_C2 * ym1;
        const float den = 1.0f + PQ_C3 * ym1;
        return std::pow(num / den, PQ_M2);
    }

    /// PQ OETF: Normalized linear light [0.0, 1.0] -> Non-linear code value [0.0, 1.0]
    [[nodiscard]] static inline float pq_oetf(float linear_norm) noexcept {
        return pq_oetf_nits(linear_norm * PQ_PEAK_LUMINANCE);
    }

    // =========================================================================
    // ARIB STD-B67 / ITU-R BT.2100 (Hybrid Log-Gamma - HLG)
    // =========================================================================

    static constexpr float HLG_A = 0.17883277f;
    static constexpr float HLG_B = 1.0f - 4.0f * HLG_A;         // 0.28466892f
    static constexpr float HLG_C = 0.55991073f;                 // 0.5 - a * ln(4a)

    /// HLG OETF: Scene linear light [0.0, 1.0] -> Non-linear signal [0.0, 1.0]
    [[nodiscard]] static inline float hlg_oetf(float e) noexcept {
        e = std::max(0.0f, e);
        if (e <= 1.0f / 12.0f) {
            return std::sqrt(3.0f * e);
        }
        return HLG_A * std::log(12.0f * e - HLG_B) + HLG_C;
    }

    /// HLG Inverse OETF: Non-linear signal [0.0, 1.0] -> Scene linear light [0.0, 1.0]
    [[nodiscard]] static inline float hlg_inverse_oetf(float ep) noexcept {
        ep = std::clamp(ep, 0.0f, 1.0f);
        if (ep <= 0.5f) {
            return (ep * ep) / 3.0f;
        }
        return (std::exp((ep - HLG_C) / HLG_A) + HLG_B) / 12.0f;
    }

    /// HLG EOTF: Non-linear signal [0.0, 1.0] -> Display luminance in nits
    /// @param ep Non-linear signal [0, 1]
    /// @param peak_luminance Nominal display peak luminance in cd/m^2 (typically 1000 nits)
    [[nodiscard]] static inline float hlg_eotf_nits(float ep, float peak_luminance = 1000.0f) noexcept {
        const float scene_linear = hlg_inverse_oetf(ep);
        // Display Opto-Optical Transfer Function (OOTF) system gamma:
        // gamma = 1.2 + 0.42 * log10(L_w / 1000)
        const float gamma = 1.2f + 0.42f * std::log10(std::max(peak_luminance, 100.0f) / 1000.0f);
        return peak_luminance * std::pow(scene_linear, gamma);
    }

    // =========================================================================
    // ITU-R BT.709
    // =========================================================================

    /// BT.709 OETF: Linear light [0.0, 1.0] -> Non-linear code value [0.0, 1.0]
    [[nodiscard]] static inline float bt709_oetf(float l) noexcept {
        l = std::clamp(l, 0.0f, 1.0f);
        if (l < 0.018f) {
            return 4.5f * l;
        }
        return 1.099f * std::pow(l, 0.45f) - 0.099f;
    }

    /// BT.709 Inverse OETF: Non-linear code value [0.0, 1.0] -> Linear light [0.0, 1.0]
    [[nodiscard]] static inline float bt709_inverse_oetf(float v) noexcept {
        v = std::clamp(v, 0.0f, 1.0f);
        if (v < 0.081f) {
            return v / 4.5f;
        }
        return std::pow((v + 0.099f) / 1.099f, 1.0f / 0.45f);
    }

    // =========================================================================
    // sRGB (IEC 61966-2-1)
    // =========================================================================

    /// sRGB OETF: Linear light [0.0, 1.0] -> Non-linear sRGB [0.0, 1.0]
    [[nodiscard]] static inline float srgb_oetf(float l) noexcept {
        l = std::clamp(l, 0.0f, 1.0f);
        if (l <= 0.0031308f) {
            return 12.92f * l;
        }
        return 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    }

    /// sRGB EOTF (Inverse OETF): Non-linear sRGB [0.0, 1.0] -> Linear light [0.0, 1.0]
    [[nodiscard]] static inline float srgb_inverse_oetf(float s) noexcept {
        s = std::clamp(s, 0.0f, 1.0f);
        if (s <= 0.04045f) {
            return s / 12.92f;
        }
        return std::pow((s + 0.055f) / 1.055f, 2.4f);
    }

    // =========================================================================
    // Generic evaluation by TransferCharacteristic enum
    // =========================================================================

    /// Converts non-linear code [0.0, 1.0] to normalized linear light [0.0, 1.0]
    [[nodiscard]] static float to_linear(float non_linear, TransferCharacteristic transfer) noexcept;

    /// Converts normalized linear light [0.0, 1.0] to non-linear code [0.0, 1.0]
    [[nodiscard]] static float from_linear(float linear_val, TransferCharacteristic transfer) noexcept;
};

/// High-performance 1024-entry precomputed Look-Up Table with linear interpolation
/// for transfer function evaluations in high-resolution video frame processing.
class TransferFunctionLUT {
public:
    static constexpr size_t TABLE_SIZE = 1024;

    TransferFunctionLUT() = default;

    /// Build LUT from a mathematical function mapping [0, 1] -> [0, 1] (or arbitrary output range)
    template <typename Func>
    void initialize(Func&& func) {
        for (size_t i = 0; i < TABLE_SIZE; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(TABLE_SIZE - 1);
            table_[i] = func(t);
        }
    }

    /// Factory: Build precomputed PQ EOTF LUT (maps [0, 1] to normalized linear light [0, 1])
    [[nodiscard]] static TransferFunctionLUT create_pq_eotf();

    /// Factory: Build precomputed PQ OETF LUT
    [[nodiscard]] static TransferFunctionLUT create_pq_oetf();

    /// Factory: Build precomputed HLG Inverse OETF LUT
    [[nodiscard]] static TransferFunctionLUT create_hlg_inverse_oetf();

    /// Factory: Build precomputed BT.709 Inverse OETF LUT
    [[nodiscard]] static TransferFunctionLUT create_bt709_inverse_oetf();

    /// Factory: Build precomputed sRGB Inverse OETF LUT
    [[nodiscard]] static TransferFunctionLUT create_srgb_inverse_oetf();

    /// Fast lookup with linear interpolation
    [[nodiscard]] inline float evaluate(float x) const noexcept {
        x = std::clamp(x, 0.0f, 1.0f);
        const float pos = x * static_cast<float>(TABLE_SIZE - 1);
        const size_t idx = static_cast<size_t>(pos);
        if (idx >= TABLE_SIZE - 1) {
            return table_[TABLE_SIZE - 1];
        }
        const float frac = pos - static_cast<float>(idx);
        return table_[idx] * (1.0f - frac) + table_[idx + 1] * frac;
    }

    /// Batch lookup for contiguous float arrays
    void evaluate_batch(const float* __restrict src, float* __restrict dst, size_t count) const noexcept;

private:
    std::array<float, TABLE_SIZE> table_{};
};

} // namespace atum

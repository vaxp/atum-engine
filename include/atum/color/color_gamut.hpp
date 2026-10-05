#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include "atum/color/hdr_metadata.hpp"

namespace atum {

/// A 3x3 matrix for linear color space transformations (gamut conversions).
/// Stored in row-major order:
/// [ m[0] m[1] m[2] ]
/// [ m[3] m[4] m[5] ]
/// [ m[6] m[7] m[8] ]
struct ColorMatrix3x3 {
    std::array<float, 9> m{1.0f, 0.0f, 0.0f,
                           0.0f, 1.0f, 0.0f,
                           0.0f, 0.0f, 1.0f};

    /// Identity matrix
    [[nodiscard]] static constexpr ColorMatrix3x3 identity() noexcept {
        return ColorMatrix3x3{
            .m = {1.0f, 0.0f, 0.0f,
                  0.0f, 1.0f, 0.0f,
                  0.0f, 0.0f, 1.0f}
        };
    }

    /// Analytical 3x3 matrix inversion via Cramer's rule
    [[nodiscard]] inline ColorMatrix3x3 invert() const noexcept {
        const float det = m[0] * (m[4] * m[8] - m[5] * m[7])
                        - m[1] * (m[3] * m[8] - m[5] * m[6])
                        + m[2] * (m[3] * m[7] - m[4] * m[6]);
        if (std::fabs(det) < 1e-12f) return identity();
        const float inv_det = 1.0f / det;
        return ColorMatrix3x3{
            .m = {
                (m[4] * m[8] - m[5] * m[7]) * inv_det,
                (m[2] * m[7] - m[1] * m[8]) * inv_det,
                (m[1] * m[5] - m[2] * m[4]) * inv_det,
                (m[5] * m[6] - m[3] * m[8]) * inv_det,
                (m[0] * m[8] - m[2] * m[6]) * inv_det,
                (m[2] * m[3] - m[0] * m[5]) * inv_det,
                (m[3] * m[7] - m[4] * m[6]) * inv_det,
                (m[1] * m[6] - m[0] * m[7]) * inv_det,
                (m[0] * m[4] - m[1] * m[3]) * inv_det
            }
        };
    }

    /// Transformation from ITU-R BT.2020 to ITU-R BT.709 (linear D65)
    [[nodiscard]] static constexpr ColorMatrix3x3 bt2020_to_bt709() noexcept {
        return ColorMatrix3x3{
            .m = { 1.6604910f, -0.5876411f, -0.0728499f,
                  -0.1245505f,  1.1328999f, -0.0083494f,
                  -0.0181508f, -0.1005789f,  1.1187297f}
        };
    }

    /// Transformation from ITU-R BT.709 to ITU-R BT.2020 (linear D65)
    [[nodiscard]] static inline ColorMatrix3x3 bt709_to_bt2020() noexcept {
        return bt2020_to_bt709().invert();
    }

    /// Transformation from ITU-R BT.2020 to DCI-P3 (D65)
    [[nodiscard]] static constexpr ColorMatrix3x3 bt2020_to_dcip3() noexcept {
        return ColorMatrix3x3{
            .m = { 1.3416041f, -0.3157451f, -0.0258590f,
                  -0.0543213f,  1.0700516f, -0.0157303f,
                  -0.0028246f, -0.0160298f,  1.0188544f}
        };
    }

    /// Transformation from DCI-P3 (D65) to ITU-R BT.2020
    [[nodiscard]] static inline ColorMatrix3x3 dcip3_to_bt2020() noexcept {
        return bt2020_to_dcip3().invert();
    }

    /// Transformation from DCI-P3 (D65) to ITU-R BT.709
    [[nodiscard]] static constexpr ColorMatrix3x3 dcip3_to_bt709() noexcept {
        return ColorMatrix3x3{
            .m = { 1.2249402f, -0.2249402f,  0.0000000f,
                  -0.0420569f,  1.0420569f,  0.0000000f,
                  -0.0196375f, -0.0786361f,  1.0982736f}
        };
    }

    /// Transformation from ITU-R BT.709 to DCI-P3 (D65)
    [[nodiscard]] static inline ColorMatrix3x3 bt709_to_dcip3() noexcept {
        return dcip3_to_bt709().invert();
    }

    /// Matrix multiplication C = A * B
    [[nodiscard]] ColorMatrix3x3 operator*(const ColorMatrix3x3& other) const noexcept;

    /// Get transformation matrix between source and destination color primaries
    [[nodiscard]] static ColorMatrix3x3 get_transform(ColorPrimaries src, ColorPrimaries dst) noexcept;

    /// Transform a single linear RGB triplet
    inline void transform(float r, float g, float b, float& out_r, float& out_g, float& out_b) const noexcept {
        out_r = m[0] * r + m[1] * g + m[2] * b;
        out_g = m[3] * r + m[4] * g + m[5] * b;
        out_b = m[6] * r + m[7] * g + m[8] * b;
    }

    /// Batch planar transform
    void transform_batch(const float* __restrict r_in,
                         const float* __restrict g_in,
                         const float* __restrict b_in,
                         float* __restrict r_out,
                         float* __restrict g_out,
                         float* __restrict b_out,
                         size_t count) const noexcept;

    /// Batch interleaved RGBA float buffer transform [R0, G0, B0, A0, R1, ...]
    void transform_rgba_batch(float* __restrict rgba, size_t pixel_count) const noexcept;
};

} // namespace atum

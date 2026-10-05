#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>
#include "atum/surface/pixel_format.hpp"

namespace atum {

/// Professional Chroma Prediction & Cross-Component Modeling (CfL / RExt).
class ChromaPredictor {
public:
    // =========================================================================
    // Cross-Component Prediction (Chroma from Luma - CfL)
    // Used in AV1 and HEVC Range Extensions (RExt)
    // =========================================================================

    /// Downsamples reconstructed luma to chroma resolution (supports 4:2:0, 4:2:2, 4:4:4).
    /// @param luma Reconstructed luma samples [luma_stride * (height * 2 or height)]
    /// @param luma_stride Stride in elements (samples)
    /// @param dst_downsampled Destination downsampled buffer [width * height]
    /// @param width Chroma block width
    /// @param height Chroma block height
    /// @param subsampling Chroma subsampling mode
    static void downsample_luma(
        const int16_t* luma, size_t luma_stride,
        int16_t* dst_downsampled,
        uint32_t width, uint32_t height,
        ChromaSubsampling subsampling = ChromaSubsampling::CS_420
    ) noexcept;

    /// Computes AC luma: L_AC(x, y) = L(x, y) - mean(L)
    static void compute_ac_luma(
        const int16_t* luma_downsampled,
        int16_t* dst_ac_luma,
        uint32_t width, uint32_t height
    ) noexcept;

    /// Predicts chroma block using Cross-Component Prediction (CfL):
    /// C_pred(x, y) = clip(C_base(x, y) + (alpha * L_AC(x, y)) / 16)
    /// @param base_pred Base intra prediction (e.g. DC prediction)
    /// @param ac_luma AC-centered downsampled luma
    /// @param dst_pred Output predicted chroma samples
    /// @param width Chroma block width
    /// @param height Chroma block height
    /// @param alpha_q4 Scaling parameter in Q4 fixed-point format (range [-32, 32])
    /// @param bit_depth 8, 10, or 12 bits
    static void predict_cfl(
        const int16_t* base_pred,
        const int16_t* ac_luma,
        int16_t* dst_pred,
        uint32_t width, uint32_t height,
        int16_t alpha_q4,
        uint8_t bit_depth = 10
    ) noexcept;

    // =========================================================================
    // YUV 4:2:2 Rectangular Chroma Intra Prediction
    // Blocks are non-square: e.g. 4x8, 8x16, 16x32
    // =========================================================================

    /// DC prediction for rectangular chroma block (W x H)
    static void pred_422_dc(
        const int16_t* top_neighbors,
        const int16_t* left_neighbors,
        int16_t* dst, size_t dst_stride,
        uint32_t width, uint32_t height
    ) noexcept;

    /// Planar prediction for rectangular chroma block (W x H)
    static void pred_422_planar(
        const int16_t* top_neighbors,
        const int16_t* left_neighbors,
        int16_t* dst, size_t dst_stride,
        uint32_t width, uint32_t height
    ) noexcept;

    /// Vertical prediction for rectangular chroma block (W x H)
    static void pred_422_vertical(
        const int16_t* top_neighbors,
        int16_t* dst, size_t dst_stride,
        uint32_t width, uint32_t height
    ) noexcept;

    /// Horizontal prediction for rectangular chroma block (W x H)
    static void pred_422_horizontal(
        const int16_t* left_neighbors,
        int16_t* dst, size_t dst_stride,
        uint32_t width, uint32_t height
    ) noexcept;

    /// Cross-Component residual prediction for HEVC RExt:
    /// R_chroma(x, y) += (alpha * R_luma(x, y)) >> shift
    static void predict_residual_rext(
        const int16_t* luma_residual, size_t luma_stride,
        int16_t* chroma_residual, size_t chroma_stride,
        uint32_t width, uint32_t height,
        int16_t alpha, uint8_t shift
    ) noexcept;
};

} // namespace atum

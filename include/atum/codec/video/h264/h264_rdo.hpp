#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include "atum/codec/video/motion_estimate.hpp"
#include "atum/codec/video/h264/h264_types.hpp"

namespace atum {

enum class MbMode {
    Intra4x4,
    Intra16x16,
    Inter16x16,
    Inter16x8,
    Inter8x16,
    Inter8x8,
    Skip
};

struct MacroblockDecision {
    MbMode mode{MbMode::Intra16x16};
    uint8_t intra16x16_pred_mode{2};          // 0=V, 1=H, 2=DC, 3=Plane
    std::array<uint8_t, 16> intra4x4_pred_modes{}; // 9 modes per 4x4 block
    uint8_t intra_chroma_pred_mode{0};        // 0=DC, 1=H, 2=V, 3=Plane

    // Motion vectors for Inter modes
    std::array<MotionVector, 4> mvs{};
    std::array<MotionVector, 4> mvds{};

    // Quantized transform coefficients
    // 16 luma 4x4 blocks (each 16 int32_t)
    alignas(16) std::array<int32_t, 256> luma_levels{};
    // 4 Cb 4x4 blocks + 4 Cr 4x4 blocks (each 16 int32_t)
    alignas(16) std::array<int32_t, 64> chroma_cb_levels{};
    alignas(16) std::array<int32_t, 64> chroma_cr_levels{};

    // Coded Block Pattern (CBP)
    uint32_t cbp{0}; // Lower 4 bits: luma 8x8 blocks, bits 4-5: chroma
    uint32_t total_distortion{0};
    uint32_t estimated_rate{0};
    double cost{1e12};

    // Reconstructed macroblock pixels (for DPB update)
    alignas(32) std::array<uint8_t, 256> recon_y{};
    alignas(16) std::array<uint8_t, 64> recon_cb{};
    alignas(16) std::array<uint8_t, 64> recon_cr{};
};

class H264Rdo {
public:
    /// Computes Lagrangian multiplier lambda for mode decision given QP.
    [[nodiscard]] static double compute_lambda(int32_t qp) noexcept;

    /// Computes Lagrangian multiplier for motion estimation given QP.
    [[nodiscard]] static uint32_t compute_lambda_motion(int32_t qp) noexcept;

    /// Sum of Squared Differences (SSD) distortion between two blocks.
    static uint32_t ssd(
        const uint8_t* src, size_t src_stride,
        const uint8_t* ref, size_t ref_stride,
        uint32_t width, uint32_t height
    ) noexcept;

    /// Evaluates Intra 16x16 modes (V, H, DC, Plane) and selects the best mode.
    static MacroblockDecision evaluate_intra16x16(
        const uint8_t* orig_y, size_t orig_stride,
        const uint8_t* recon_frame_y, size_t recon_stride,
        int32_t mb_x, int32_t mb_y,
        int32_t qp, double lambda
    ) noexcept;

    /// Evaluates Intra 4x4 modes across all 16 sub-blocks.
    static MacroblockDecision evaluate_intra4x4(
        const uint8_t* orig_y, size_t orig_stride,
        const uint8_t* recon_frame_y, size_t recon_stride,
        int32_t mb_x, int32_t mb_y,
        int32_t qp, double lambda
    ) noexcept;

    /// Evaluates Inter 16x16 and Skip modes against reference frame.
    static MacroblockDecision evaluate_inter(
        const uint8_t* orig_y, size_t orig_stride,
        const uint8_t* ref_frame_y, size_t ref_stride,
        int32_t frame_width, int32_t frame_height,
        int32_t mb_x, int32_t mb_y,
        MotionVector mvp,
        int32_t qp, double lambda,
        MotionSearchMethod me_method = MotionSearchMethod::Hexagonal,
        int32_t me_range = 16
    ) noexcept;

    /// Evaluates chroma prediction and quantization for 8x8 Cb and Cr blocks.
    static void evaluate_chroma(
        const uint8_t* orig_cb, size_t orig_cb_stride,
        const uint8_t* orig_cr, size_t orig_cr_stride,
        const uint8_t* recon_cb_frame, size_t recon_cb_stride,
        const uint8_t* recon_cr_frame, size_t recon_cr_stride,
        int32_t mb_x, int32_t mb_y,
        int32_t qp, bool is_intra,
        MacroblockDecision& decision
    ) noexcept;

    /// Master mode decision for a macroblock:
    /// In I-slices: compares Intra 16x16 and Intra 4x4.
    /// In P-slices: compares Skip, Inter 16x16, Intra 16x16, and Intra 4x4.
    static MacroblockDecision select_best_mode(
        const uint8_t* orig_y, size_t orig_y_stride,
        const uint8_t* orig_cb, size_t orig_cb_stride,
        const uint8_t* orig_cr, size_t orig_cr_stride,
        const uint8_t* recon_y, size_t recon_y_stride,
        const uint8_t* recon_cb, size_t recon_cb_stride,
        const uint8_t* recon_cr, size_t recon_cr_stride,
        const uint8_t* ref_y, size_t ref_y_stride,
        int32_t frame_width, int32_t frame_height,
        int32_t mb_x, int32_t mb_y,
        MotionVector mvp,
        int32_t qp, bool is_p_slice,
        MotionSearchMethod me_method = MotionSearchMethod::Hexagonal,
        int32_t me_range = 16
    ) noexcept;
};

} // namespace atum

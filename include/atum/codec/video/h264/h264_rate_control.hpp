#pragma once

#include <cstdint>
#include <cstddef>
#include "atum/core/media_types.hpp"

namespace atum {

enum class RateControlMode {
    CQP, // Constant Quantization Parameter
    CBR, // Constant Bitrate (Leaky Bucket VBV)
    CRF  // Constant Rate Factor (Complexity-adaptive)
};

enum class H264FrameType {
    IDR,
    I,
    P,
    B
};

struct RateControlConfig {
    RateControlMode mode{RateControlMode::CRF};
    int32_t base_qp{23};              // Used for CQP and base for CRF
    uint32_t target_bitrate_kbps{2500}; // Used for CBR
    uint32_t fps{30};
    uint32_t vbv_buffer_size_kb{2500}; // 1.0 second buffer default
    int32_t min_qp{10};
    int32_t max_qp{51};
    int32_t p_offset{2};               // QP offset for P-frames
    int32_t b_offset{4};               // QP offset for B-frames
};

class H264RateControl {
public:
    explicit H264RateControl(const RateControlConfig& config = {});

    /// Resets the rate control state.
    void reset();

    /// Evaluates spatial complexity (luma variance) of a plane.
    static double calculate_spatial_complexity(
        const uint8_t* plane, size_t stride,
        uint32_t width, uint32_t height
    ) noexcept;

    /// Called at the start of a frame to obtain the allocated QP.
    [[nodiscard]] int32_t start_frame(H264FrameType type, double spatial_complexity = 1.0);

    /// Called at the end of a frame to update rate control statistics.
    void end_frame(H264FrameType type, size_t bits_spent);

    // =========================================================================
    // Telemetry & Diagnostics
    // =========================================================================

    [[nodiscard]] int32_t current_qp() const noexcept { return current_qp_; }
    [[nodiscard]] double buffer_fullness_ratio() const noexcept;
    [[nodiscard]] size_t total_bits_encoded() const noexcept { return total_bits_; }
    [[nodiscard]] size_t total_frames_encoded() const noexcept { return total_frames_; }
    [[nodiscard]] double average_bitrate_kbps() const noexcept;
    [[nodiscard]] const RateControlConfig& config() const noexcept { return config_; }

private:
    RateControlConfig config_;
    int32_t current_qp_{23};

    // VBV leaky bucket state (in bits)
    int64_t vbv_buffer_size_bits_{0};
    int64_t vbv_fullness_bits_{0};
    int64_t target_bits_per_frame_{0};

    // Overall telemetry
    size_t total_bits_{0};
    size_t total_frames_{0};
    double rolling_complexity_{1.0};
};

} // namespace atum

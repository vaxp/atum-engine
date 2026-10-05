#include "atum/codec/video/h264/h264_rate_control.hpp"
#include <algorithm>
#include <cmath>

namespace atum {

H264RateControl::H264RateControl(const RateControlConfig& config)
    : config_(config) {
    reset();
}

void H264RateControl::reset() {
    current_qp_ = config_.base_qp;
    vbv_buffer_size_bits_ = static_cast<int64_t>(config_.vbv_buffer_size_kb) * 1000LL * 8LL;
    vbv_fullness_bits_ = vbv_buffer_size_bits_ / 2; // Start half-full
    target_bits_per_frame_ = (static_cast<int64_t>(config_.target_bitrate_kbps) * 1000LL) /
                             std::max(config_.fps, 1u);
    total_bits_ = 0;
    total_frames_ = 0;
    rolling_complexity_ = 1.0;
}

double H264RateControl::calculate_spatial_complexity(
    const uint8_t* plane, size_t stride,
    uint32_t width, uint32_t height
) noexcept {
    if (width == 0 || height == 0 || !plane) {
        return 1.0;
    }

    // Step across pixels for fast complexity estimation
    uint32_t step = (width > 640) ? 4 : 2;
    uint64_t sum = 0;
    uint64_t sq_sum = 0;
    uint32_t count = 0;

    for (uint32_t y = 0; y < height; y += step) {
        const uint8_t* row = plane + y * stride;
        for (uint32_t x = 0; x < width; x += step) {
            uint32_t v = row[x];
            sum += v;
            sq_sum += v * v;
            count++;
        }
    }

    if (count == 0) return 1.0;

    double mean = static_cast<double>(sum) / count;
    double variance = (static_cast<double>(sq_sum) / count) - (mean * mean);
    double std_dev = std::sqrt(std::max(variance, 0.0));

    // Normalize to standard unit: 16.0 std dev is roughly nominal complexity 1.0
    return std::max(std_dev / 16.0, 0.1);
}

int32_t H264RateControl::start_frame(H264FrameType type, double spatial_complexity) {
    if (config_.mode == RateControlMode::CQP) {
        current_qp_ = config_.base_qp;
        if (type == H264FrameType::P) {
            current_qp_ += config_.p_offset;
        } else if (type == H264FrameType::B) {
            current_qp_ += config_.b_offset;
        }
    } else if (config_.mode == RateControlMode::CBR) {
        // Evaluate VBV fullness relative to nominal target (50% full)
        int64_t target_fullness = vbv_buffer_size_bits_ / 2;
        int64_t fullness_diff = vbv_fullness_bits_ - target_fullness;

        // Scale delta QP smoothly: buffer overflow increases QP, underflow decreases QP
        double normalized_diff = static_cast<double>(fullness_diff) / std::max<int64_t>(target_fullness, 1);
        int32_t delta_qp = std::clamp(static_cast<int32_t>(std::round(normalized_diff * 8.0)), -12, 12);

        current_qp_ = std::clamp(config_.base_qp + delta_qp, config_.min_qp, config_.max_qp);

        if (type == H264FrameType::IDR || type == H264FrameType::I) {
            // Give keyframes higher fidelity
            current_qp_ = std::max(current_qp_ - config_.p_offset, config_.min_qp);
        }
    } else { // CRF
        rolling_complexity_ = 0.85 * rolling_complexity_ + 0.15 * std::max(spatial_complexity, 0.1);
        double ratio = std::clamp(spatial_complexity / rolling_complexity_, 0.25, 4.0);

        // Standard logarithmic scaling: doubling complexity adjusts QP slightly
        int32_t delta_qp = static_cast<int32_t>(std::round(3.0 * std::log2(ratio)));
        current_qp_ = std::clamp(config_.base_qp + delta_qp, config_.min_qp, config_.max_qp);

        if (type == H264FrameType::P) {
            current_qp_ += config_.p_offset;
        } else if (type == H264FrameType::B) {
            current_qp_ += config_.b_offset;
        }
    }

    current_qp_ = std::clamp(current_qp_, config_.min_qp, config_.max_qp);
    return current_qp_;
}

void H264RateControl::end_frame(H264FrameType /*type*/, size_t bits_spent) {
    if (vbv_buffer_size_bits_ > 0) {
        int64_t new_fullness = vbv_fullness_bits_ + static_cast<int64_t>(bits_spent) - target_bits_per_frame_;
        vbv_fullness_bits_ = std::clamp<int64_t>(new_fullness, 0, vbv_buffer_size_bits_);
    }
    total_bits_ += bits_spent;
    total_frames_++;
}

double H264RateControl::buffer_fullness_ratio() const noexcept {
    if (vbv_buffer_size_bits_ <= 0) return 0.0;
    return static_cast<double>(vbv_fullness_bits_) / vbv_buffer_size_bits_;
}

double H264RateControl::average_bitrate_kbps() const noexcept {
    if (total_frames_ == 0 || config_.fps == 0) return 0.0;
    double duration_sec = static_cast<double>(total_frames_) / config_.fps;
    return (total_bits_ / duration_sec) / 1000.0;
}

} // namespace atum

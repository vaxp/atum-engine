#include "atum/resilience/error_concealer.hpp"
#include <chrono>

namespace atum {

ErrorConcealer::ErrorConcealer(ErrorConcealmentMode mode) noexcept
    : mode_(mode) {}

MediaResult<void> ErrorConcealer::conceal(
    VideoFrame& frame,
    ErrorConcealmentMask& mask,
    const DecodedPictureBuffer* dpb,
    const std::vector<ConcealmentMv>& collocated_mvs
) {
    if (mode_ == ErrorConcealmentMode::Disabled || !mask.has_damaged_blocks()) {
        return {};
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    size_t damaged_before = mask.damaged_count();
    stats_.total_damaged_blocks += damaged_before;

    MediaResult<void> result;

    if (mode_ == ErrorConcealmentMode::SpatialOnly || dpb == nullptr || dpb->empty()) {
        result = spatial_.conceal_frame(frame, mask);
    } else if (mode_ == ErrorConcealmentMode::TemporalOnly) {
        result = temporal_.conceal_frame(frame, mask, *dpb, collocated_mvs);
    } else { // ErrorConcealmentMode::Auto
        if (frame.is_keyframe() || dpb->empty()) {
            result = spatial_.conceal_frame(frame, mask);
        } else {
            result = temporal_.conceal_frame(frame, mask, *dpb, collocated_mvs);
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    stats_.total_concealment_time_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.concealed_frames++;

    // Tally block concealment types
    for (uint32_t r = 0; r < mask.rows(); ++r) {
        for (uint32_t c = 0; c < mask.cols(); ++c) {
            auto s = mask.get(c, r);
            if (s == BlockStatus::ConcealedSpatial) {
                stats_.spatially_concealed_blocks++;
            } else if (s == BlockStatus::ConcealedTemporal) {
                stats_.temporally_concealed_blocks++;
            }
        }
    }

    return result;
}

} // namespace atum

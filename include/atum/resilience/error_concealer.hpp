#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/resilience/error_mask.hpp"
#include "atum/resilience/spatial_concealment.hpp"
#include "atum/resilience/temporal_concealment.hpp"
#include "atum/resilience/resilient_nal_parser.hpp"

namespace atum {

/// Concealment strategy mode.
enum class ErrorConcealmentMode : uint8_t {
    Auto,         ///< Temporal concealment for inter frames with spatial fallback, spatial for intra
    SpatialOnly,  ///< Always use spatial boundary extrapolation
    TemporalOnly, ///< Always use temporal motion copy from DPB (no spatial fallback)
    Disabled      ///< Bypass error concealment (leave damaged pixels as-is)
};

/// Telemetry metrics for error concealment performance.
struct ErrorConcealmentStats {
    size_t concealed_frames{0};
    size_t spatially_concealed_blocks{0};
    size_t temporally_concealed_blocks{0};
    size_t total_damaged_blocks{0};
    double total_concealment_time_ms{0.0};

    void reset() noexcept {
        concealed_frames = 0;
        spatially_concealed_blocks = 0;
        temporally_concealed_blocks = 0;
        total_damaged_blocks = 0;
        total_concealment_time_ms = 0.0;
    }
};

/// Unified Autonomous Error Concealment Coordinator.
/// Coordinates spatial, temporal, and hybrid concealment on decoded video frames
/// in conjunction with the Decoded Picture Buffer (DPB) and resilient bitstream parser.
class ErrorConcealer {
public:
    explicit ErrorConcealer(ErrorConcealmentMode mode = ErrorConcealmentMode::Auto) noexcept;

    /// Conceals damaged regions in `frame` using `mask` and available `dpb` references.
    MediaResult<void> conceal(VideoFrame& frame,
                              ErrorConcealmentMask& mask,
                              const DecodedPictureBuffer* dpb = nullptr,
                              const std::vector<ConcealmentMv>& collocated_mvs = {});

    [[nodiscard]] ErrorConcealmentMode mode() const noexcept { return mode_; }
    void set_mode(ErrorConcealmentMode mode) noexcept { mode_ = mode; }

    [[nodiscard]] SpatialErrorConcealer& spatial_concealer() noexcept { return spatial_; }
    [[nodiscard]] const SpatialErrorConcealer& spatial_concealer() const noexcept { return spatial_; }

    [[nodiscard]] TemporalErrorConcealer& temporal_concealer() noexcept { return temporal_; }
    [[nodiscard]] const TemporalErrorConcealer& temporal_concealer() const noexcept { return temporal_; }

    [[nodiscard]] const ErrorConcealmentStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_.reset(); }

private:
    ErrorConcealmentMode mode_{ErrorConcealmentMode::Auto};
    SpatialErrorConcealer spatial_;
    TemporalErrorConcealer temporal_;
    ErrorConcealmentStats stats_;
};

} // namespace atum

#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/resilience/error_mask.hpp"
#include "atum/resilience/spatial_concealment.hpp"

namespace atum {

/// 2D Integer Motion Vector for Temporal Error Concealment.
struct ConcealmentMv {
    int16_t x{0};
    int16_t y{0};

    constexpr bool operator==(const ConcealmentMv& o) const noexcept {
        return x == o.x && y == o.y;
    }
};

/// Temporal Error Concealment Engine.
/// Reconstructs damaged macroblocks in inter-coded P and B frames using
/// Decoded Picture Buffer (DPB) references, collocated motion vector inheritance,
/// the Boundary Matching Algorithm (BMA / BMAD), and perimeter seam alpha feathering.
class TemporalErrorConcealer {
public:
    struct Config {
        uint32_t bmad_max_threshold{64};         ///< Maximum acceptable average pixel error before fallback
        bool enable_boundary_matching{true};     ///< Test candidate neighbor MVs with BMAD
        bool enable_seam_feathering{true};       ///< Alpha-blend outer borders with valid neighbors
        bool enable_spatial_fallback{true};      ///< Fall back to spatial concealment if DPB empty or BMAD too high
    };

    TemporalErrorConcealer() noexcept = default;
    explicit TemporalErrorConcealer(Config config) noexcept : config_(config) {}

    /// Conceals all damaged blocks in the given frame using available DPB reference frames.
    MediaResult<void> conceal_frame(VideoFrame& frame,
                                    ErrorConcealmentMask& mask,
                                    const DecodedPictureBuffer& dpb,
                                    const std::vector<ConcealmentMv>& collocated_mvs = {});

    /// Conceals a single block using a specified reference frame.
    MediaResult<void> conceal_block(VideoFrame& frame,
                                    uint32_t col, uint32_t row,
                                    ErrorConcealmentMask& mask,
                                    const VideoFrame& ref_frame,
                                    ConcealmentMv collocated_mv = ConcealmentMv{},
                                    uint32_t block_size = 16);

    /// Computes the Boundary Mean Absolute Difference (BMAD) for candidate vector (mv_x, mv_y).
    [[nodiscard]] double compute_bmad(const VideoFrame& cur_frame,
                                      const VideoFrame& ref_frame,
                                      uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
                                      int16_t mv_x, int16_t mv_y,
                                      bool has_top, bool has_bottom, bool has_left, bool has_right) const noexcept;

    /// Evaluates candidate motion vectors and selects the one minimizing BMAD.
    [[nodiscard]] ConcealmentMv select_best_mv(const VideoFrame& cur_frame,
                                               const VideoFrame& ref_frame,
                                               uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
                                               const std::vector<ConcealmentMv>& candidates,
                                               bool has_top, bool has_bottom, bool has_left, bool has_right) const noexcept;

    [[nodiscard]] const Config& config() const noexcept { return config_; }
    void set_config(const Config& config) noexcept { config_ = config; }

private:
    Config config_;
    SpatialErrorConcealer spatial_fallback_;

    void copy_motion_compensated_block(VideoFrame& cur_frame,
                                       const VideoFrame& ref_frame,
                                       uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
                                       int16_t mv_x, int16_t mv_y);

    void apply_seam_feathering(VideoFrame& frame,
                               uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
                               bool has_top, bool has_bottom, bool has_left, bool has_right);
};

} // namespace atum

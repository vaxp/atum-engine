#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/resilience/error_mask.hpp"

namespace atum {

/// Quantized boundary edge orientation detected across valid neighboring pixels.
enum class SpatialEdgeDirection : uint8_t {
    None = 0,          ///< Flat, isotropic, or no dominant gradient detected
    Horizontal = 1,    ///< 0° dominant horizontal edge (top/bottom boundary continuation)
    Vertical = 2,      ///< 90° dominant vertical edge (left/right boundary continuation)
    DiagonalUp = 3,    ///< 45° diagonal up-right edge (bottom-left to top-right)
    DiagonalDown = 4   ///< 135° diagonal down-right edge (top-left to bottom-right)
};

/// Spatial Error Concealment Engine.
/// Reconstructs damaged macroblocks and CTUs using multidirectional boundary edge gradient
/// detection, directional ray extrapolation, and distance-weighted bilinear interpolation.
class SpatialErrorConcealer {
public:
    struct Config {
        float edge_threshold{8.0f};              ///< Minimum gradient magnitude to trigger directional ray casting
        bool enable_directional{true};          ///< Enable directional edge extrapolation
        bool enable_bilinear{true};             ///< Enable distance-weighted bilinear interpolation
    };

    SpatialErrorConcealer() noexcept = default;
    explicit SpatialErrorConcealer(Config config) noexcept : config_(config) {}

    /// Conceals all damaged blocks in the given frame according to the damage mask.
    MediaResult<void> conceal_frame(VideoFrame& frame, ErrorConcealmentMask& mask);

    /// Conceals a single damaged block at (col, row).
    MediaResult<void> conceal_block(VideoFrame& frame, uint32_t col, uint32_t row,
                                    const ErrorConcealmentMask& mask, uint32_t block_size = 16);

    /// Detects dominant edge direction along available valid borders around (col, row).
    [[nodiscard]] SpatialEdgeDirection detect_edge_direction(
        const uint8_t* plane, size_t stride,
        uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
        uint32_t frame_w, uint32_t frame_h,
        const ErrorConcealmentMask& mask, uint32_t col, uint32_t row
    ) const noexcept;

    [[nodiscard]] const Config& config() const noexcept { return config_; }
    void set_config(const Config& config) noexcept { config_ = config; }

private:
    Config config_;

    void conceal_luma_block(uint8_t* plane, size_t stride,
                            uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
                            uint32_t frame_w, uint32_t frame_h,
                            SpatialEdgeDirection dir,
                            bool has_top, bool has_bottom, bool has_left, bool has_right);

    void conceal_chroma_block(uint8_t* plane, size_t stride,
                              uint32_t cpx, uint32_t cpy, uint32_t cbw, uint32_t cbh,
                              uint32_t cw, uint32_t ch,
                              SpatialEdgeDirection dir,
                              bool has_top, bool has_bottom, bool has_left, bool has_right);
};

} // namespace atum

#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"

namespace atum {

/// AV1 Loop Restoration Types (Section 5.11.57 / 7.17).
enum class Av1RestoreType : uint8_t {
    None       = 0,
    Wiener     = 1,
    Sgrproj    = 2,
    Switchable = 3
};

/// Sovereign AV1 Dual Loop Restoration Filter Engine (Wiener & SGRproj).
class Av1LoopRestoration {
public:
    /// Applies 7-tap separable symmetric Wiener restoration filter (Section 7.17.3).
    /// h_taps and v_taps contain the 3 half-symmetric coefficients [w0, w1, w2].
    /// w3 is derived as 128 - 2*(w0 + w1 + w2).
    static void apply_wiener(
        uint8_t* plane,
        size_t stride,
        uint32_t width,
        uint32_t height,
        const int16_t h_taps[3],
        const int16_t v_taps[3]
    ) noexcept;

    /// Applies Self-Guided Restoration (SGRproj) with dual box filters and projective weights (Section 7.17.4).
    static void apply_sgrproj(
        uint8_t* plane,
        size_t stride,
        uint32_t width,
        uint32_t height,
        int32_t x0,
        int32_t x1,
        uint8_t r0,
        uint8_t r1
    ) noexcept;

    /// Applies restoration filter to all planes of the frame.
    static void apply_frame_restoration(
        VideoFrame& frame,
        Av1RestoreType type
    ) noexcept;
};

} // namespace atum

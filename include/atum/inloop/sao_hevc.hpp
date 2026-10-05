#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>

namespace atum {

/// HEVC Sample Adaptive Offset (SAO) mode types.
enum class SaoType : uint8_t {
    None = 0,
    BandOffset = 1,
    EdgeOffset0 = 2,  ///< 0 degrees (Horizontal)
    EdgeOffset90 = 3, ///< 90 degrees (Vertical)
    EdgeOffset135 = 4,///< 135 degrees (Diagonal)
    EdgeOffset45 = 5  ///< 45 degrees (Diagonal)
};

/// Parameters for applying SAO onto a Coding Tree Unit (CTU).
struct SaoParams {
    SaoType type{SaoType::None};
    int8_t offsets[4]{0, 0, 0, 0};
    uint8_t band_position{0}; ///< For BandOffset: starting band (0..31)
};

/// Sovereign HEVC / H.265 Sample Adaptive Offset (SAO) Filter Engine.
/// Implements ITU-T H.265 Section 8.7.3.
class SaoHevc {
public:
    /// Applies SAO filtering to an arbitrary rectangular block of reconstructed samples.
    static void apply_sao(
        uint8_t* pix,
        size_t stride,
        uint32_t width,
        uint32_t height,
        const SaoParams& params,
        int32_t bit_depth = 8
    ) noexcept;
};

} // namespace atum

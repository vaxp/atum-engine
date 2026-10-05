#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include "atum/core/media_types.hpp"
#include "atum/codec/audio/aac/aac_types.hpp"
#include "atum/codec/audio/aac/aac_entropy.hpp"

namespace atum {

/// Fast Sovereign IMDCT and Overlap-Add Engine for MPEG-4 AAC-LC.
/// Implements ISO/IEC 14496-3 Section 4.5.3 (Filterbank and Time-Frequency Mapping).
class AacImdct {
public:
    AacImdct();
    ~AacImdct() = default;

    /// Resets overlap buffers and filterbank state (e.g. after seek).
    void reset() noexcept;

    /// Synthesizes 1024 PCM audio samples from 1024 de-quantized spectral bins.
    /// Handles long, short, and transition window sequences (LongStart, LongStop).
    /// Applies Sine and KBD windows and performs Overlap-Add (OLA).
    void process_channel(
        const AacIcsInfo& ics,
        const float* spectrum,
        size_t channel_idx,
        float* pcm_output
    ) noexcept;

    /// Direct 2048-point IMDCT transform.
    static void imdct2048(const float* in_spec, float* out_time) noexcept;

    /// Direct 256-point IMDCT transform.
    static void imdct256(const float* in_spec, float* out_time) noexcept;

private:
    void init_windows() noexcept;

    // Overlap buffers: 1024 samples per channel (support up to 8 channels)
    std::vector<float> overlap_buffers_[8];
    AacWindowShape prev_window_shape_[8]{AacWindowShape::SineWindow};

    // Precomputed window tables
    std::vector<float> sine_window_long_;  // 2048
    std::vector<float> sine_window_short_; // 256
    std::vector<float> kbd_window_long_;   // 2048
    std::vector<float> kbd_window_short_;  // 256
};

} // namespace atum

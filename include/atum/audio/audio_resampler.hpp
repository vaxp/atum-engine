#pragma once

#include <cstdint>
#include <cstddef>
#include "atum/audio/audio_frame.hpp"

namespace atum {

/// Sovereign audio resampler, channel mixer, and sample format converter.
class AudioResampler {
public:
    /// Resamples and converts an AudioFrame to a target rate, channel layout, and sample format.
    static AudioFrame resample(
        const AudioFrame& src,
        uint32_t dst_sample_rate,
        uint32_t dst_channels,
        AudioSampleFormat dst_format
    );
};

} // namespace atum

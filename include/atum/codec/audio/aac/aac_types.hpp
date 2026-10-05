#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include "atum/core/media_types.hpp"
#include "atum/audio/audio_types.hpp"

namespace atum {

/// MPEG-4 Audio Object Types (ISO/IEC 14496-3 Table 1.1).
enum class AacAot : uint8_t {
    Null        = 0,
    AacMain     = 1,
    AacLc       = 2,  ///< Advanced Audio Coding Low Complexity (industry standard)
    AacSsr      = 3,
    AacLtp      = 4,
    Sbr         = 5,  ///< Spectral Band Replication (HE-AAC v1)
    AacScalable = 6,
    TwinVq      = 7,
    Ps          = 29  ///< Parametric Stereo (HE-AAC v2)
};

/// AAC Window Sequence Type (Section 4.5.2).
enum class AacWindowSequence : uint8_t {
    OnlyLongSequence  = 0, ///< 2048-sample transform window
    LongStartSequence = 1, ///< Transition from long to short
    EightShortSequence= 2, ///< Eight 256-sample transform windows
    LongStopSequence  = 3  ///< Transition from short to long
};

/// AAC Window Shape Type.
enum class AacWindowShape : uint8_t {
    SineWindow = 0,
    KbdWindow  = 1   ///< Kaiser-Bessel Derived (KBD) Window
};

/// Standard MPEG-4 AAC Sampling Rate Lookup Table (Table 1.13).
inline constexpr uint32_t AAC_SAMPLE_RATES[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025,  8000,  7350,     0,     0,     0
};

[[nodiscard]] constexpr uint32_t aac_sample_rate_from_index(uint8_t idx) noexcept {
    return idx < 13 ? AAC_SAMPLE_RATES[idx] : 0;
}

[[nodiscard]] constexpr ChannelLayout aac_channel_layout_from_config(uint8_t config) noexcept {
    switch (config) {
        case 1: return ChannelLayout::Mono;
        case 2: return ChannelLayout::Stereo;
        case 3: return ChannelLayout::Surround2_1;
        case 4: return ChannelLayout::Quad;
        case 5: return ChannelLayout::Surround5_1;
        case 6: return ChannelLayout::Surround5_1;
        case 7: return ChannelLayout::Surround7_1;
        default: return ChannelLayout::Stereo;
    }
}

} // namespace atum

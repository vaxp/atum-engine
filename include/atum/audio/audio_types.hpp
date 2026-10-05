#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace atum {

/// Raw audio PCM sample encoding format.
enum class AudioSampleFormat : uint8_t {
    None = 0,
    U8,         ///< Unsigned 8-bit integer
    S16LE,      ///< Signed 16-bit integer, Little-Endian
    S24LE,      ///< Signed 24-bit integer packed into 3 bytes
    S32LE,      ///< Signed 32-bit integer, Little-Endian
    F32LE,      ///< 32-bit IEEE float [-1.0f .. 1.0f]
    F64LE,      ///< 64-bit IEEE double
};

/// Speaker channel spatial configuration.
enum class ChannelLayout : uint8_t {
    Unknown = 0,
    Mono = 1,           ///< 1 channel: Center
    Stereo = 2,         ///< 2 channels: Left, Right
    Surround2_1 = 3,    ///< 3 channels: Left, Right, LFE
    Quad = 4,           ///< 4 channels: Front L, Front R, Back L, Back R
    Surround5_1 = 6,    ///< 6 channels: Front L, R, Center, LFE, Surround L, R
    Surround7_1 = 8,    ///< 8 channels: Front L, R, Center, LFE, Surround L, R, Rear L, R
};

constexpr std::string_view audio_sample_format_to_string(AudioSampleFormat fmt) noexcept {
    switch (fmt) {
        case AudioSampleFormat::U8:    return "U8";
        case AudioSampleFormat::S16LE: return "S16LE";
        case AudioSampleFormat::S24LE: return "S24LE";
        case AudioSampleFormat::S32LE: return "S32LE";
        case AudioSampleFormat::F32LE: return "F32LE";
        case AudioSampleFormat::F64LE: return "F64LE";
        default:                       return "None/Unknown";
    }
}

constexpr size_t audio_sample_format_bytes(AudioSampleFormat fmt) noexcept {
    switch (fmt) {
        case AudioSampleFormat::U8:    return 1;
        case AudioSampleFormat::S16LE: return 2;
        case AudioSampleFormat::S24LE: return 3;
        case AudioSampleFormat::S32LE: return 4;
        case AudioSampleFormat::F32LE: return 4;
        case AudioSampleFormat::F64LE: return 8;
        default:                       return 0;
    }
}

constexpr uint32_t channel_layout_count(ChannelLayout layout) noexcept {
    return static_cast<uint32_t>(layout);
}

} // namespace atum

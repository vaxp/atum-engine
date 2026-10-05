#pragma once

#include <cstdint>
#include <string_view>
#include "atum/core/media_types.hpp"

namespace atum {

/// Unified enumeration of supported audio and video codec formats.
enum class CodecId : uint32_t {
    None = 0,
    Unknown = None,

    // --- Video Codecs ---
    H264 = 100,        ///< Advanced Video Coding (AVC / ISO 14496-10)
    H265,              ///< High Efficiency Video Coding (HEVC / ISO 23008-2)
    HEVC = H265,       ///< Alias for H.265
    VP8,               ///< VP8 Video
    VP9,               ///< VP9 Video
    AV1,               ///< AOMedia Video 1
    MJPEG,             ///< Motion JPEG (zero-dependency sovereign implementation)
    QOI_VIDEO,         ///< Sovereign Motion-QOI ultra-fast lossless video
    RAW_VIDEO,         ///< Uncompressed Raw Video Frame Stream

    // --- Audio Codecs ---
    PCM_S16LE = 200,   ///< Raw PCM Signed 16-bit
    PCM_S24LE,         ///< Raw PCM Signed 24-bit
    PCM_S32LE,         ///< Raw PCM Signed 32-bit
    PCM_F32LE,         ///< Raw PCM IEEE 32-bit Float
    AAC,               ///< Advanced Audio Coding (MPEG-4 Audio)
    OPUS,              ///< Opus Audio
    Opus = OPUS,       ///< Alias for Opus
    MP3,               ///< MPEG Audio Layer III
    FLAC,              ///< Free Lossless Audio Codec
    Ac3 = 210,         ///< Dolby Digital AC-3
    Vorbis = 211,      ///< Ogg Vorbis
};

constexpr std::string_view codec_id_to_string(CodecId id) noexcept {
    switch (id) {
        case CodecId::H264:      return "H.264 / AVC";
        case CodecId::H265:      return "H.265 / HEVC";
        case CodecId::VP8:       return "VP8";
        case CodecId::VP9:       return "VP9";
        case CodecId::AV1:       return "AV1";
        case CodecId::MJPEG:     return "Motion JPEG";
        case CodecId::QOI_VIDEO: return "Motion QOI";
        case CodecId::RAW_VIDEO: return "Raw Video";
        case CodecId::PCM_S16LE: return "PCM S16LE";
        case CodecId::PCM_S24LE: return "PCM S24LE";
        case CodecId::PCM_S32LE: return "PCM S32LE";
        case CodecId::PCM_F32LE: return "PCM F32LE";
        case CodecId::AAC:       return "AAC";
        case CodecId::OPUS:      return "Opus";
        case CodecId::MP3:       return "MP3";
        case CodecId::FLAC:      return "FLAC";
        default:                 return "Unknown";
    }
}

constexpr MediaType codec_id_media_type(CodecId id) noexcept {
    uint32_t val = static_cast<uint32_t>(id);
    if (val >= 100 && val < 200) return MediaType::Video;
    if (val >= 200 && val < 300) return MediaType::Audio;
    return MediaType::Unknown;
}

} // namespace atum

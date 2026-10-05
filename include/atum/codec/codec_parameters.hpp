#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include "atum/codec/codec_id.hpp"
#include "atum/surface/pixel_format.hpp"
#include "atum/audio/audio_types.hpp"

namespace atum {

/// Complete descriptor of media codec configuration parameters for an elementary stream.
struct CodecParameters {
    CodecId codec_id{CodecId::None};
    MediaType media_type{MediaType::Unknown};
    uint64_t bit_rate{0};
    std::vector<uint8_t> extradata; // SPS/PPS for H.264, AudioSpecificConfig for AAC, etc.

    // Video stream properties
    uint32_t width{0};
    uint32_t height{0};
    PixelFormat pixel_format{PixelFormat::Unknown};
    ColorRange color_range{ColorRange::Studio};
    ColorSpace color_space{ColorSpace::BT709};
    Rational framerate{30, 1};
    Rational sample_aspect_ratio{1, 1};

    // Audio stream properties
    uint32_t sample_rate{0};
    uint32_t channels{0};
    ChannelLayout channel_layout{ChannelLayout::Stereo};
    AudioSampleFormat audio_format{AudioSampleFormat::None};
    uint32_t frame_size{0}; // Number of samples per frame if fixed

    [[nodiscard]] bool is_video() const noexcept { return media_type == MediaType::Video; }
    [[nodiscard]] bool is_audio() const noexcept { return media_type == MediaType::Audio; }

    static CodecParameters create_video(
        CodecId id,
        uint32_t w,
        uint32_t h,
        PixelFormat fmt,
        Rational fps = {30, 1}
    ) {
        CodecParameters p;
        p.codec_id = id;
        p.media_type = MediaType::Video;
        p.width = w;
        p.height = h;
        p.pixel_format = fmt;
        p.framerate = fps;
        return p;
    }

    static CodecParameters create_audio(
        CodecId id,
        uint32_t s_rate,
        uint32_t ch,
        AudioSampleFormat fmt,
        ChannelLayout layout = ChannelLayout::Stereo
    ) {
        CodecParameters p;
        p.codec_id = id;
        p.media_type = MediaType::Audio;
        p.sample_rate = s_rate;
        p.channels = ch;
        p.audio_format = fmt;
        p.channel_layout = layout;
        return p;
    }
};

} // namespace atum

#pragma once

#include <memory>
#include "atum/core/media_types.hpp"
#include "atum/core/packet.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/audio/audio_frame.hpp"
#include "atum/codec/codec_parameters.hpp"

namespace atum {

/// Abstract video encoder interface.
class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;

    /// Initializes and configures the encoder with target resolution, bitrate, and framerate.
    virtual MediaResult<void> init(const CodecParameters& params) = 0;

    /// Feeds an uncompressed raw video frame for encoding.
    virtual MediaResult<void> send_frame(const VideoFrame& frame) = 0;

    /// Retrieves an encoded compressed packet. Returns MediaError::NeedMoreData if ready.
    virtual MediaResult<void> receive_packet(Packet& packet) = 0;

    /// Signals end of stream to flush remaining delayed frames.
    virtual MediaResult<void> flush() = 0;

    [[nodiscard]] virtual const CodecParameters& parameters() const noexcept = 0;
};

/// Abstract audio encoder interface.
class AudioEncoder {
public:
    virtual ~AudioEncoder() = default;

    /// Initializes the audio encoder with sample rate, channels, and bitrate.
    virtual MediaResult<void> init(const CodecParameters& params) = 0;

    /// Feeds uncompressed PCM audio samples.
    virtual MediaResult<void> send_frame(const AudioFrame& frame) = 0;

    /// Retrieves an encoded compressed audio packet.
    virtual MediaResult<void> receive_packet(Packet& packet) = 0;

    /// Signals end of stream.
    virtual MediaResult<void> flush() = 0;

    [[nodiscard]] virtual const CodecParameters& parameters() const noexcept = 0;
};

} // namespace atum

#pragma once

#include <memory>
#include "atum/core/media_types.hpp"
#include "atum/core/packet.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/audio/audio_frame.hpp"
#include "atum/codec/codec_parameters.hpp"

namespace atum {

/// Abstract video decoder interface.
class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;

    /// Configures the decoder with stream parameters and extradata.
    virtual MediaResult<void> init(const CodecParameters& params) = 0;

    /// Feeds an encoded packet to the decoder.
    virtual MediaResult<void> send_packet(const Packet& packet) = 0;

    /// Retrieves a decoded video frame. Returns MediaError::NeedMoreData if more packets are required.
    virtual MediaResult<void> receive_frame(VideoFrame& frame) = 0;

    /// Flushes any buffered/delayed frames (e.g. at end of stream).
    virtual MediaResult<void> flush() = 0;

    /// Resets decoder state (e.g. after a seek).
    virtual MediaResult<void> reset() = 0;

    [[nodiscard]] virtual const CodecParameters& parameters() const noexcept = 0;
};

/// Abstract audio decoder interface.
class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;

    /// Configures the audio decoder with stream parameters.
    virtual MediaResult<void> init(const CodecParameters& params) = 0;

    /// Feeds an encoded audio packet to the decoder.
    virtual MediaResult<void> send_packet(const Packet& packet) = 0;

    /// Retrieves a decoded audio frame. Returns MediaError::NeedMoreData if more packets are required.
    virtual MediaResult<void> receive_frame(AudioFrame& frame) = 0;

    /// Flushes any buffered audio samples.
    virtual MediaResult<void> flush() = 0;

    /// Resets audio decoder state.
    virtual MediaResult<void> reset() = 0;

    [[nodiscard]] virtual const CodecParameters& parameters() const noexcept = 0;
};

} // namespace atum

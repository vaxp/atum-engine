#pragma once

#include <memory>
#include <functional>
#include "atum/codec/codec_id.hpp"
#include "atum/decode/decoder.hpp"
#include "atum/encode/encoder.hpp"

namespace atum {

/// Dynamic factory registry for video and audio decoders and encoders.
class CodecRegistry {
public:
    /// Creates a video decoder for the specified CodecId.
    static std::unique_ptr<VideoDecoder> create_video_decoder(CodecId id);

    /// Creates an audio decoder for the specified CodecId.
    static std::unique_ptr<AudioDecoder> create_audio_decoder(CodecId id);

    /// Creates a video encoder for the specified CodecId.
    static std::unique_ptr<VideoEncoder> create_video_encoder(CodecId id);

    /// Creates an audio encoder for the specified CodecId.
    static std::unique_ptr<AudioEncoder> create_audio_encoder(CodecId id);
};

} // namespace atum

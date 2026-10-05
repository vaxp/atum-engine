#pragma once

#include <memory>
#include <queue>
#include <vector>
#include "atum/core/media_types.hpp"
#include "atum/decode/decoder.hpp"
#include "atum/codec/audio/opus/opus_parser.hpp"
#include "atum/codec/audio/opus/opus_range_decoder.hpp"
#include "atum/codec/audio/opus/celt_decoder.hpp"

namespace atum {

/// Sovereign Opus RFC 6716 Audio Decoder.
/// 100% pure C++20 zero-dependency implementation.
class OpusDecoder : public AudioDecoder {
public:
    OpusDecoder();
    ~OpusDecoder() override = default;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_packet(const Packet& packet) override;
    MediaResult<void> receive_frame(AudioFrame& frame) override;
    MediaResult<void> flush() override;
    MediaResult<void> reset() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override {
        return params_;
    }

private:
    CodecParameters params_{};
    CeltDecoder celt_engine_;
    std::queue<AudioFrame> frame_queue_;

    uint32_t sample_rate_{48000};
    uint32_t channels_{2};
    ChannelLayout layout_{ChannelLayout::Stereo};
};

} // namespace atum

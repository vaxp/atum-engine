#pragma once

#include <memory>
#include <queue>
#include <vector>
#include "atum/core/media_types.hpp"
#include "atum/decode/decoder.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/codec/audio/flac/flac_parser.hpp"

namespace atum {

/// Sovereign FLAC (Free Lossless Audio Codec) Audio Decoder.
/// 100% pure C++20 zero-dependency implementation of IETF RFC FLAC.
class FlacDecoder : public AudioDecoder {
public:
    FlacDecoder();
    ~FlacDecoder() override = default;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_packet(const Packet& packet) override;
    MediaResult<void> receive_frame(AudioFrame& frame) override;
    MediaResult<void> flush() override;
    MediaResult<void> reset() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override {
        return params_;
    }

private:
    MediaResult<void> decode_subframe(
        BitReader& reader,
        uint8_t bps,
        uint16_t block_size,
        std::vector<int32_t>& samples
    );

    MediaResult<void> decode_residual(
        BitReader& reader,
        uint8_t predictor_order,
        uint16_t block_size,
        std::vector<int32_t>& residual
    );

    CodecParameters params_{};
    FlacStreamInfo streaminfo_{};
    std::queue<AudioFrame> frame_queue_;

    uint32_t sample_rate_{44100};
    uint8_t channels_{2};
    uint8_t bits_per_sample_{16};
    ChannelLayout layout_{ChannelLayout::Stereo};
};

} // namespace atum

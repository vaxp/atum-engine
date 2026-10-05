#pragma once

#include <memory>
#include <queue>
#include <vector>
#include "atum/core/media_types.hpp"
#include "atum/decode/decoder.hpp"
#include "atum/codec/audio/aac/aac_types.hpp"
#include "atum/codec/audio/aac/aac_adts.hpp"
#include "atum/codec/audio/aac/aac_entropy.hpp"
#include "atum/codec/audio/aac/aac_tools.hpp"
#include "atum/codec/audio/aac/aac_imdct.hpp"

namespace atum {

/// Sovereign MPEG-4 AAC-LC Audio Decoder.
/// 100% pure C++20 zero-dependency implementation of ISO/IEC 14496-3.
class AacDecoder : public AudioDecoder {
public:
    AacDecoder();
    ~AacDecoder() override = default;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_packet(const Packet& packet) override;
    MediaResult<void> receive_frame(AudioFrame& frame) override;
    MediaResult<void> flush() override;
    MediaResult<void> reset() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override {
        return params_;
    }

private:
    MediaResult<void> decode_raw_block(BitReader& reader, int64_t pts, int64_t dts);
    MediaResult<void> decode_sce(BitReader& reader, std::vector<float>& pcm_out);
    MediaResult<void> decode_cpe(BitReader& reader, std::vector<float>& pcm_out);

    CodecParameters params_{};
    AacImdct imdct_engine_;
    std::queue<AudioFrame> frame_queue_;

    uint32_t sample_rate_{44100};
    uint32_t channels_{2};
    ChannelLayout layout_{ChannelLayout::Stereo};
};

} // namespace atum

#pragma once

#include "atum/decode/decoder.hpp"
#include <deque>

namespace atum {

/// Sovereign Motion-QOI ultra-fast lossless video decoder.
class QoiVideoDecoder : public VideoDecoder {
public:
    QoiVideoDecoder() = default;
    ~QoiVideoDecoder() override = default;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_packet(const Packet& packet) override;
    MediaResult<void> receive_frame(VideoFrame& frame) override;
    MediaResult<void> flush() override;
    MediaResult<void> reset() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override { return params_; }

    /// Static single-frame decode helper.
    static MediaResult<VideoFrame> decode_frame_bytes(std::span<const uint8_t> qoi_bytes);

private:
    CodecParameters params_;
    std::deque<Packet> packet_queue_;
};

} // namespace atum

#pragma once

#include "atum/encode/encoder.hpp"
#include <deque>

namespace atum {

/// Sovereign Motion-QOI video encoder.
class QoiVideoEncoder : public VideoEncoder {
public:
    QoiVideoEncoder() = default;
    ~QoiVideoEncoder() override = default;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_frame(const VideoFrame& frame) override;
    MediaResult<void> receive_packet(Packet& packet) override;
    MediaResult<void> flush() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override { return params_; }

    /// Static single-frame encode helper.
    static MediaResult<std::vector<uint8_t>> encode_frame_bytes(const VideoFrame& frame);

private:
    CodecParameters params_;
    std::deque<Packet> ready_packets_;
};

} // namespace atum

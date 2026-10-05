#pragma once

#include "atum/decode/decoder.hpp"
#include <deque>

namespace atum {

/// Raw PCM audio decoder.
class PcmDecoder : public AudioDecoder {
public:
    PcmDecoder() = default;
    ~PcmDecoder() override = default;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_packet(const Packet& packet) override;
    MediaResult<void> receive_frame(AudioFrame& frame) override;
    MediaResult<void> flush() override;
    MediaResult<void> reset() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override { return params_; }

private:
    CodecParameters params_;
    std::deque<Packet> packet_queue_;
};

} // namespace atum

#pragma once

#include "atum/encode/encoder.hpp"
#include <deque>

namespace atum {

/// Raw PCM audio encoder.
class PcmEncoder : public AudioEncoder {
public:
    PcmEncoder() = default;
    ~PcmEncoder() override = default;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_frame(const AudioFrame& frame) override;
    MediaResult<void> receive_packet(Packet& packet) override;
    MediaResult<void> flush() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override { return params_; }

private:
    CodecParameters params_;
    std::deque<Packet> ready_packets_;
};

} // namespace atum

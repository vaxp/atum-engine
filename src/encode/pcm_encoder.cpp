#include "atum/encode/pcm_encoder.hpp"

namespace atum {

MediaResult<void> PcmEncoder::init(const CodecParameters& params) {
    if (params.sample_rate == 0 || params.channels == 0 || params.audio_format == AudioSampleFormat::None) {
        return MediaError::UnsupportedFormat;
    }
    params_ = params;
    ready_packets_.clear();
    return MediaResult<void>::ok();
}

MediaResult<void> PcmEncoder::send_frame(const AudioFrame& frame) {
    if (!frame.is_valid()) return MediaError::InvalidData;

    Packet pkt = Packet::from_bytes({frame.data(), frame.size_bytes()});
    pkt.set_pts(frame.pts());
    pkt.set_dts(frame.dts());
    pkt.set_duration(frame.duration());
    pkt.set_keyframe(true);

    ready_packets_.push_back(std::move(pkt));
    return MediaResult<void>::ok();
}

MediaResult<void> PcmEncoder::receive_packet(Packet& packet) {
    if (ready_packets_.empty()) {
        return MediaError::NeedMoreData;
    }
    packet = std::move(ready_packets_.front());
    ready_packets_.pop_front();
    return MediaResult<void>::ok();
}

MediaResult<void> PcmEncoder::flush() {
    return MediaResult<void>::ok();
}

} // namespace atum

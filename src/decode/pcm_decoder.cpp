#include "atum/decode/pcm_decoder.hpp"
#include <cstring>

namespace atum {

MediaResult<void> PcmDecoder::init(const CodecParameters& params) {
    if (params.sample_rate == 0 || params.channels == 0 || params.audio_format == AudioSampleFormat::None) {
        return MediaError::UnsupportedFormat;
    }
    params_ = params;
    packet_queue_.clear();
    return MediaResult<void>::ok();
}

MediaResult<void> PcmDecoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::InvalidData;
    packet_queue_.push_back(packet);
    return MediaResult<void>::ok();
}

MediaResult<void> PcmDecoder::receive_frame(AudioFrame& frame) {
    if (packet_queue_.empty()) {
        return MediaError::NeedMoreData;
    }

    Packet pkt = std::move(packet_queue_.front());
    packet_queue_.pop_front();

    size_t bps = audio_sample_format_bytes(params_.audio_format);
    size_t bytes_per_full_sample = bps * params_.channels;
    if (bytes_per_full_sample == 0) return MediaError::InvalidData;

    size_t nb_samples = pkt.size() / bytes_per_full_sample;
    if (nb_samples == 0) return MediaError::InvalidData;

    frame = AudioFrame::allocate(
        params_.sample_rate,
        params_.channels,
        params_.audio_format,
        nb_samples,
        params_.channel_layout
    );
    frame.set_pts(pkt.pts());
    frame.set_dts(pkt.dts());
    frame.set_duration(pkt.duration());
    frame.set_keyframe(true);

    std::memcpy(frame.data_mut(), pkt.data(), nb_samples * bytes_per_full_sample);
    return MediaResult<void>::ok();
}

MediaResult<void> PcmDecoder::flush() {
    packet_queue_.clear();
    return MediaResult<void>::ok();
}

MediaResult<void> PcmDecoder::reset() {
    packet_queue_.clear();
    return MediaResult<void>::ok();
}

} // namespace atum

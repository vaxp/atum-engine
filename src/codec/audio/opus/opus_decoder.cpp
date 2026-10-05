#include "atum/codec/audio/opus/opus_decoder.hpp"
#include <cstring>

namespace atum {

OpusDecoder::OpusDecoder() {
    params_.codec_id = CodecId::OPUS;
    params_.sample_rate = 48000;
    params_.channels = 2;
    params_.channel_layout = ChannelLayout::Stereo;
    params_.audio_format = AudioSampleFormat::F32LE;
}

MediaResult<void> OpusDecoder::init(const CodecParameters& params) {
    params_ = params;
    if (params.sample_rate > 0) sample_rate_ = params.sample_rate;
    if (params.channels > 0) channels_ = params.channels;
    layout_ = (channels_ == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;

    celt_engine_.reset();
    while (!frame_queue_.empty()) frame_queue_.pop();
    return {};
}

MediaResult<void> OpusDecoder::flush() {
    celt_engine_.reset();
    while (!frame_queue_.empty()) frame_queue_.pop();
    return {};
}

MediaResult<void> OpusDecoder::reset() {
    return flush();
}

MediaResult<void> OpusDecoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    auto pkt_res = OpusParser::parse_packet(std::span<const uint8_t>(packet.data(), packet.size()));
    if (!pkt_res) return pkt_res.error();

    const auto& opus_pkt = *pkt_res;
    channels_ = opus_pkt.toc.stereo ? 2 : 1;
    layout_ = (channels_ == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;

    int64_t current_pts = packet.pts();

    for (const auto& frame_info : opus_pkt.frames) {
        if (frame_info.data.empty()) continue;

        OpusRangeDecoder range_dec;
        range_dec.init(frame_info.data);

        size_t samples_to_decode = frame_info.frame_samples;
        AudioFrame out_frame = AudioFrame::allocate(
            48000,
            channels_,
            AudioSampleFormat::F32LE,
            samples_to_decode,
            layout_
        );
        out_frame.set_pts(current_pts);
        out_frame.set_dts(packet.dts());

        float* pcm_data = reinterpret_cast<float*>(out_frame.data_mut());
        auto dec_res = celt_engine_.decode_frame(range_dec, opus_pkt.toc, samples_to_decode, pcm_data);
        if (!dec_res) return dec_res;

        frame_queue_.push(std::move(out_frame));

        if (current_pts != NOPTS) {
            current_pts += static_cast<int64_t>(samples_to_decode);
        }
    }

    return {};
}

MediaResult<void> OpusDecoder::receive_frame(AudioFrame& frame) {
    if (frame_queue_.empty()) {
        return MediaError::NeedMoreData;
    }
    frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    return {};
}

} // namespace atum

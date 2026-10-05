#include "atum/audio/audio_frame.hpp"
#include <cstring>

namespace atum {

AudioFrame::AudioFrame() noexcept
    : Frame(MediaType::Audio) {}

AudioFrame::AudioFrame(const AudioFrame& other)
    : Frame(MediaType::Audio)
    , sample_rate_(other.sample_rate_)
    , channels_(other.channels_)
    , layout_(other.layout_)
    , format_(other.format_)
    , nb_samples_(other.nb_samples_)
    , is_owned_(other.is_owned_) {
    pts_ = other.pts_;
    dts_ = other.dts_;
    duration_ = other.duration_;
    timebase_ = other.timebase_;
    is_keyframe_ = other.is_keyframe_;

    if (other.is_owned_) {
        owned_buffer_ = other.owned_buffer_;
        data_ptr_ = owned_buffer_.data();
    } else {
        data_ptr_ = other.data_ptr_;
    }
}

AudioFrame& AudioFrame::operator=(const AudioFrame& other) {
    if (this == &other) return *this;

    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
    layout_ = other.layout_;
    format_ = other.format_;
    nb_samples_ = other.nb_samples_;
    is_owned_ = other.is_owned_;

    pts_ = other.pts_;
    dts_ = other.dts_;
    duration_ = other.duration_;
    timebase_ = other.timebase_;
    is_keyframe_ = other.is_keyframe_;

    if (other.is_owned_) {
        owned_buffer_ = other.owned_buffer_;
        data_ptr_ = owned_buffer_.data();
    } else {
        owned_buffer_.clear();
        data_ptr_ = other.data_ptr_;
    }
    return *this;
}

AudioFrame AudioFrame::allocate(
    uint32_t sample_rate,
    uint32_t channels,
    AudioSampleFormat format,
    size_t nb_samples,
    ChannelLayout layout
) {
    AudioFrame frame;
    frame.sample_rate_ = sample_rate;
    frame.channels_ = channels;
    frame.layout_ = layout;
    frame.format_ = format;
    frame.nb_samples_ = nb_samples;

    size_t total_bytes = nb_samples * channels * audio_sample_format_bytes(format);
    frame.owned_buffer_.resize(total_bytes, 0);
    frame.data_ptr_ = frame.owned_buffer_.data();
    frame.is_owned_ = true;
    return frame;
}

AudioFrame AudioFrame::from_raw_samples(
    uint8_t* data,
    size_t /*size_bytes*/,
    uint32_t sample_rate,
    uint32_t channels,
    AudioSampleFormat format,
    size_t nb_samples,
    ChannelLayout layout
) noexcept {
    AudioFrame frame;
    frame.sample_rate_ = sample_rate;
    frame.channels_ = channels;
    frame.layout_ = layout;
    frame.format_ = format;
    frame.nb_samples_ = nb_samples;
    frame.data_ptr_ = data;
    frame.is_owned_ = false;
    return frame;
}

void AudioFrame::ensure_owned() {
    if (is_owned_ || !is_valid()) return;
    auto cloned = clone();
    *this = std::move(cloned);
}

AudioFrame AudioFrame::clone() const {
    if (!is_valid()) return AudioFrame();
    auto new_frame = allocate(sample_rate_, channels_, format_, nb_samples_, layout_);
    new_frame.set_pts(pts_);
    new_frame.set_dts(dts_);
    new_frame.set_duration(duration_);
    new_frame.set_timebase(timebase_);
    new_frame.set_keyframe(is_keyframe_);

    std::memcpy(new_frame.data_mut(), data(), size_bytes());
    return new_frame;
}

} // namespace atum

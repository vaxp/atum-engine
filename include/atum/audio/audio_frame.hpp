#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include "atum/core/frame.hpp"
#include "atum/audio/audio_types.hpp"

namespace atum {

/// Decoded uncompressed PCM audio frame.
class AudioFrame : public Frame {
public:
    AudioFrame() noexcept;
    ~AudioFrame() override = default;

    AudioFrame(const AudioFrame& other);
    AudioFrame& operator=(const AudioFrame& other);

    AudioFrame(AudioFrame&& other) noexcept = default;
    AudioFrame& operator=(AudioFrame&& other) noexcept = default;

    /// Allocates an owned audio buffer for `nb_samples` per channel.
    static AudioFrame allocate(
        uint32_t sample_rate,
        uint32_t channels,
        AudioSampleFormat format,
        size_t nb_samples,
        ChannelLayout layout = ChannelLayout::Stereo
    );

    /// Wraps external PCM memory without copying (borrowed zero-copy view).
    static AudioFrame from_raw_samples(
        uint8_t* data,
        size_t size_bytes,
        uint32_t sample_rate,
        uint32_t channels,
        AudioSampleFormat format,
        size_t nb_samples,
        ChannelLayout layout = ChannelLayout::Stereo
    ) noexcept;

    [[nodiscard]] bool is_valid() const noexcept override {
        return sample_rate_ > 0 && channels_ > 0 && nb_samples_ > 0 && data_ptr_ != nullptr;
    }

    [[nodiscard]] uint32_t sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] ChannelLayout channel_layout() const noexcept { return layout_; }
    [[nodiscard]] AudioSampleFormat format() const noexcept { return format_; }
    [[nodiscard]] size_t nb_samples() const noexcept { return nb_samples_; }

    [[nodiscard]] const uint8_t* data() const noexcept { return data_ptr_; }
    [[nodiscard]] uint8_t* data_mut() noexcept {
        ensure_owned();
        return data_ptr_;
    }

    [[nodiscard]] size_t bytes_per_sample() const noexcept {
        return audio_sample_format_bytes(format_);
    }

    [[nodiscard]] size_t size_bytes() const noexcept {
        return nb_samples_ * channels_ * bytes_per_sample();
    }

    [[nodiscard]] double duration_seconds() const noexcept {
        return sample_rate_ > 0 ? static_cast<double>(nb_samples_) / static_cast<double>(sample_rate_) : 0.0;
    }

    void ensure_owned();
    [[nodiscard]] AudioFrame clone() const;

private:
    uint32_t sample_rate_{0};
    uint32_t channels_{0};
    ChannelLayout layout_{ChannelLayout::Stereo};
    AudioSampleFormat format_{AudioSampleFormat::None};
    size_t nb_samples_{0};

    uint8_t* data_ptr_{nullptr};
    std::vector<uint8_t> owned_buffer_;
    bool is_owned_{false};
};

} // namespace atum

#include "atum/audio/audio_resampler.hpp"
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace atum {

static inline float sample_to_float(const uint8_t* ptr, AudioSampleFormat fmt) noexcept {
    switch (fmt) {
        case AudioSampleFormat::U8: {
            uint8_t u = *ptr;
            return (static_cast<float>(u) - 128.0f) / 128.0f;
        }
        case AudioSampleFormat::S16LE: {
            int16_t s = static_cast<int16_t>(ptr[0] | (ptr[1] << 8));
            return static_cast<float>(s) / 32768.0f;
        }
        case AudioSampleFormat::S32LE: {
            int32_t s = static_cast<int32_t>(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24));
            return static_cast<float>(s) / 2147483648.0f;
        }
        case AudioSampleFormat::F32LE: {
            float f = 0.0f;
            std::memcpy(&f, ptr, sizeof(float));
            return f;
        }
        default:
            return 0.0f;
    }
}

static inline void float_to_sample(float val, uint8_t* ptr, AudioSampleFormat fmt) noexcept {
    val = std::clamp(val, -1.0f, 1.0f);
    switch (fmt) {
        case AudioSampleFormat::U8: {
            int32_t u = static_cast<int32_t>(std::round(val * 128.0f + 128.0f));
            *ptr = static_cast<uint8_t>(std::clamp(u, 0, 255));
            break;
        }
        case AudioSampleFormat::S16LE: {
            int32_t s = static_cast<int32_t>(std::round(val * 32767.0f));
            s = std::clamp(s, -32768, 32767);
            ptr[0] = static_cast<uint8_t>(s & 0xFF);
            ptr[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
            break;
        }
        case AudioSampleFormat::S32LE: {
            double d = static_cast<double>(val) * 2147483647.0;
            int64_t s = static_cast<int64_t>(std::round(d));
            s = std::clamp<int64_t>(s, -2147483648LL, 2147483647LL);
            int32_t i32 = static_cast<int32_t>(s);
            ptr[0] = static_cast<uint8_t>(i32 & 0xFF);
            ptr[1] = static_cast<uint8_t>((i32 >> 8) & 0xFF);
            ptr[2] = static_cast<uint8_t>((i32 >> 16) & 0xFF);
            ptr[3] = static_cast<uint8_t>((i32 >> 24) & 0xFF);
            break;
        }
        case AudioSampleFormat::F32LE: {
            std::memcpy(ptr, &val, sizeof(float));
            break;
        }
        default:
            break;
    }
}

AudioFrame AudioResampler::resample(
    const AudioFrame& src,
    uint32_t dst_sample_rate,
    uint32_t dst_channels,
    AudioSampleFormat dst_format
) {
    if (!src.is_valid() || dst_sample_rate == 0 || dst_channels == 0 || dst_format == AudioSampleFormat::None) {
        return AudioFrame();
    }

    size_t src_samples = src.nb_samples();
    uint32_t src_rate = src.sample_rate();
    uint32_t src_channels = src.channels();
    AudioSampleFormat src_fmt = src.format();
    size_t src_bps = src.bytes_per_sample();

    // 1. Calculate destination sample count
    size_t dst_samples = static_cast<size_t>(
        std::round(static_cast<double>(src_samples) * dst_sample_rate / src_rate)
    );
    if (dst_samples == 0) dst_samples = 1;

    ChannelLayout dst_layout = dst_channels == 1 ? ChannelLayout::Mono : ChannelLayout::Stereo;
    auto dst_frame = AudioFrame::allocate(dst_sample_rate, dst_channels, dst_format, dst_samples, dst_layout);
    dst_frame.set_pts(src.pts());
    dst_frame.set_dts(src.dts());
    dst_frame.set_timebase(src.timebase());
    dst_frame.set_keyframe(src.is_keyframe());

    const uint8_t* src_ptr = src.data();
    uint8_t* dst_ptr = dst_frame.data_mut();
    size_t dst_bps = dst_frame.bytes_per_sample();

    double rate_ratio = static_cast<double>(src_rate) / static_cast<double>(dst_sample_rate);

    for (size_t di = 0; di < dst_samples; ++di) {
        double src_pos = static_cast<double>(di) * rate_ratio;
        size_t idx0 = static_cast<size_t>(src_pos);
        if (idx0 >= src_samples) idx0 = src_samples - 1;
        size_t idx1 = std::min(idx0 + 1, src_samples - 1);
        float frac = static_cast<float>(src_pos - idx0);

        for (uint32_t dch = 0; dch < dst_channels; ++dch) {
            float s0 = 0.0f;
            float s1 = 0.0f;

            if (src_channels == dst_channels) {
                // Direct channel mapping
                const uint8_t* p0 = src_ptr + (idx0 * src_channels + dch) * src_bps;
                const uint8_t* p1 = src_ptr + (idx1 * src_channels + dch) * src_bps;
                s0 = sample_to_float(p0, src_fmt);
                s1 = sample_to_float(p1, src_fmt);
            } else if (src_channels == 1 && dst_channels >= 2) {
                // Mono to Stereo: replicate
                const uint8_t* p0 = src_ptr + idx0 * src_bps;
                const uint8_t* p1 = src_ptr + idx1 * src_bps;
                s0 = sample_to_float(p0, src_fmt);
                s1 = sample_to_float(p1, src_fmt);
            } else if (src_channels >= 2 && dst_channels == 1) {
                // Stereo to Mono: average L & R
                const uint8_t* p0_l = src_ptr + (idx0 * src_channels + 0) * src_bps;
                const uint8_t* p0_r = src_ptr + (idx0 * src_channels + 1) * src_bps;
                const uint8_t* p1_l = src_ptr + (idx1 * src_channels + 0) * src_bps;
                const uint8_t* p1_r = src_ptr + (idx1 * src_channels + 1) * src_bps;
                s0 = 0.5f * (sample_to_float(p0_l, src_fmt) + sample_to_float(p0_r, src_fmt));
                s1 = 0.5f * (sample_to_float(p1_l, src_fmt) + sample_to_float(p1_r, src_fmt));
            }

            float interp = (1.0f - frac) * s0 + frac * s1;
            uint8_t* out_p = dst_ptr + (di * dst_channels + dch) * dst_bps;
            float_to_sample(interp, out_p, dst_format);
        }
    }

    return dst_frame;
}

} // namespace atum

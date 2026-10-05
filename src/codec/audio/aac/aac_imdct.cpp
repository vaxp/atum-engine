#include "atum/codec/audio/aac/aac_imdct.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numbers>

namespace atum {

namespace {

// Precomputed 8192-point cosine lookup table for exact fast IMDCT
struct CosTable8192 {
    float cos_lut[8192];
    CosTable8192() {
        constexpr double TWO_PI = 2.0 * std::numbers::pi_v<double>;
        for (int i = 0; i < 8192; ++i) {
            cos_lut[i] = static_cast<float>(std::cos(TWO_PI * i / 8192.0));
        }
    }
};

const CosTable8192& get_cos_table() {
    static const CosTable8192 table;
    return table;
}

double bessel_i0(double x) noexcept {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= (x * 0.5) / k;
        double term_sq = term * term;
        sum += term_sq;
        if (term_sq < 1e-16 * sum) break;
    }
    return sum;
}

void compute_kbd_window(float* win, size_t n, double alpha) noexcept {
    size_t m = n / 2;
    std::vector<double> bessel_vals(m + 1);
    double sum = 0.0;
    for (size_t j = 0; j <= m; ++j) {
        double v = 2.0 * j / static_cast<double>(m) - 1.0;
        double arg = std::numbers::pi_v<double> * alpha * std::sqrt(std::max(0.0, 1.0 - v * v));
        sum += bessel_i0(arg);
        bessel_vals[j] = sum;
    }

    double total_sum = bessel_vals[m];
    for (size_t j = 0; j < m; ++j) {
        float w = static_cast<float>(std::sqrt(bessel_vals[j] / total_sum));
        win[j] = w;
        win[n - 1 - j] = w;
    }
}

} // namespace

AacImdct::AacImdct() {
    init_windows();
    reset();
}

void AacImdct::reset() noexcept {
    for (size_t ch = 0; ch < 8; ++ch) {
        overlap_buffers_[ch].assign(1024, 0.0f);
        prev_window_shape_[ch] = AacWindowShape::SineWindow;
    }
}

void AacImdct::init_windows() noexcept {
    // 1. Sine Windows
    sine_window_long_.resize(2048);
    for (size_t n = 0; n < 2048; ++n) {
        sine_window_long_[n] = static_cast<float>(
            std::sin(std::numbers::pi_v<double> / 2048.0 * (n + 0.5))
        );
    }

    sine_window_short_.resize(256);
    for (size_t n = 0; n < 256; ++n) {
        sine_window_short_[n] = static_cast<float>(
            std::sin(std::numbers::pi_v<double> / 256.0 * (n + 0.5))
        );
    }

    // 2. KBD Windows (alpha = 4.0 for long, alpha = 6.0 for short)
    kbd_window_long_.resize(2048);
    compute_kbd_window(kbd_window_long_.data(), 2048, 4.0);

    kbd_window_short_.resize(256);
    compute_kbd_window(kbd_window_short_.data(), 256, 6.0);
}

void AacImdct::imdct2048(const float* in_spec, float* out_time) noexcept {
    const auto& cos_lut = get_cos_table().cos_lut;
    constexpr float scale = 2.0f / 2048.0f;

    for (size_t n = 0; n < 2048; ++n) {
        // angle = (2*pi / 2048) * (n + 512 + 0.5) * (k + 0.5)
        //       = (2*pi / 8192) * (2*n + 1025) * (2*k + 1)
        uint32_t step = (2 * static_cast<uint32_t>(n) + 1025) * 2;
        uint32_t angle = (2 * static_cast<uint32_t>(n) + 1025);

        float sum = 0.0f;
        for (size_t k = 0; k < 1024; ++k) {
            sum += in_spec[k] * cos_lut[angle & 8191];
            angle += step;
        }
        out_time[n] = sum * scale;
    }
}

void AacImdct::imdct256(const float* in_spec, float* out_time) noexcept {
    const auto& cos_lut = get_cos_table().cos_lut;
    constexpr float scale = 2.0f / 256.0f;

    for (size_t n = 0; n < 256; ++n) {
        // angle = (2*pi / 256) * (n + 64 + 0.5) * (k + 0.5)
        //       = (2*pi / 8192) * 32 * (2*n + 129) * (2*k + 1)
        uint32_t base = 32 * (2 * static_cast<uint32_t>(n) + 129);
        uint32_t step = base * 2;
        uint32_t angle = base;

        float sum = 0.0f;
        for (size_t k = 0; k < 128; ++k) {
            sum += in_spec[k] * cos_lut[angle & 8191];
            angle += step;
        }
        out_time[n] = sum * scale;
    }
}

void AacImdct::process_channel(
    const AacIcsInfo& ics,
    const float* spectrum,
    size_t channel_idx,
    float* pcm_output
) noexcept {
    if (channel_idx >= 8) channel_idx = 0;

    std::vector<float> windowed(2048, 0.0f);

    const float* win_prev_long = (prev_window_shape_[channel_idx] == AacWindowShape::KbdWindow)
        ? kbd_window_long_.data() : sine_window_long_.data();
    const float* win_curr_long = (ics.window_shape == AacWindowShape::KbdWindow)
        ? kbd_window_long_.data() : sine_window_long_.data();

    const float* win_prev_short = (prev_window_shape_[channel_idx] == AacWindowShape::KbdWindow)
        ? kbd_window_short_.data() : sine_window_short_.data();
    const float* win_curr_short = (ics.window_shape == AacWindowShape::KbdWindow)
        ? kbd_window_short_.data() : sine_window_short_.data();

    switch (ics.window_sequence) {
        case AacWindowSequence::OnlyLongSequence: {
            std::vector<float> raw_time(2048);
            imdct2048(spectrum, raw_time.data());

            for (size_t n = 0; n < 1024; ++n) {
                windowed[n] = raw_time[n] * win_prev_long[n];
            }
            for (size_t n = 1024; n < 2048; ++n) {
                windowed[n] = raw_time[n] * win_curr_long[n];
            }
            break;
        }

        case AacWindowSequence::LongStartSequence: {
            std::vector<float> raw_time(2048);
            imdct2048(spectrum, raw_time.data());

            // Left half: 0..1023 long window
            for (size_t n = 0; n < 1024; ++n) {
                windowed[n] = raw_time[n] * win_prev_long[n];
            }
            // Mid flat: 1024..1471 (448 samples) = 1.0f
            for (size_t n = 1024; n < 1472; ++n) {
                windowed[n] = raw_time[n];
            }
            // Transition down: 1472..1599 (128 samples) using right half of short window
            for (size_t n = 0; n < 128; ++n) {
                windowed[1472 + n] = raw_time[1472 + n] * win_curr_short[128 + n];
            }
            // Right zeros: 1600..2047 = 0.0f
            for (size_t n = 1600; n < 2048; ++n) {
                windowed[n] = 0.0f;
            }
            break;
        }

        case AacWindowSequence::EightShortSequence: {
            // Overlap-add eight 256-point IMDCTs
            for (size_t s = 0; s < 8; ++s) {
                std::vector<float> short_raw(256);
                imdct256(spectrum + s * 128, short_raw.data());

                const float* w_left = (s == 0) ? win_prev_short : win_curr_short;
                const float* w_right = win_curr_short;

                size_t offset = 448 + s * 128;
                for (size_t n = 0; n < 128; ++n) {
                    windowed[offset + n] += short_raw[n] * w_left[n];
                }
                for (size_t n = 0; n < 128; ++n) {
                    windowed[offset + 128 + n] += short_raw[128 + n] * w_right[128 + n];
                }
            }
            break;
        }

        case AacWindowSequence::LongStopSequence: {
            std::vector<float> raw_time(2048);
            imdct2048(spectrum, raw_time.data());

            // Left zeros: 0..447 = 0.0f
            for (size_t n = 0; n < 448; ++n) {
                windowed[n] = 0.0f;
            }
            // Transition up: 448..575 (128 samples) using left half of short window
            for (size_t n = 0; n < 128; ++n) {
                windowed[448 + n] = raw_time[448 + n] * win_prev_short[n];
            }
            // Mid flat: 576..1023 (448 samples) = 1.0f
            for (size_t n = 576; n < 1024; ++n) {
                windowed[n] = raw_time[n];
            }
            // Right half: 1024..2047 long window
            for (size_t n = 1024; n < 2048; ++n) {
                windowed[n] = raw_time[n] * win_curr_long[n];
            }
            break;
        }
    }

    // Overlap-Add with channel history buffer
    auto& obuf = overlap_buffers_[channel_idx];
    for (size_t n = 0; n < 1024; ++n) {
        pcm_output[n] = obuf[n] + windowed[n];
        obuf[n] = windowed[1024 + n];
    }

    prev_window_shape_[channel_idx] = ics.window_shape;
}

} // namespace atum

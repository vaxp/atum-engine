#include "atum/codec/audio/opus/celt_decoder.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numbers>

namespace atum {

static constexpr uint16_t CELT_EBANDS[22] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 34, 40, 48, 60, 72, 88
};

CeltDecoder::CeltDecoder() {
    reset();
}

void CeltDecoder::reset() noexcept {
    for (size_t ch = 0; ch < 2; ++ch) {
        prev_tail_[ch].assign(960, 0.0f);
        for (size_t b = 0; b < CELT_NUM_EBANDS; ++b) {
            old_energy_[ch][b] = 0.0f;
        }
    }
}

void CeltDecoder::imdct(const float* in_spec, float* out_time, size_t N) noexcept {
    size_t K = N / 2;
    float scale = 2.0f / static_cast<float>(N);
    double two_pi_over_N = 2.0 * std::numbers::pi_v<double> / static_cast<double>(N);

    for (size_t n = 0; n < N; ++n) {
        double sum = 0.0;
        double n_term = (static_cast<double>(n) + 0.5 + static_cast<double>(N) * 0.25) * two_pi_over_N;
        for (size_t k = 0; k < K; ++k) {
            double angle = n_term * (static_cast<double>(k) + 0.5);
            sum += in_spec[k] * std::cos(angle);
        }
        out_time[n] = static_cast<float>(sum * scale);
    }
}

void CeltDecoder::decode_pvq(
    OpusRangeDecoder& range_dec,
    size_t N,
    uint32_t K,
    float* out_vec
) noexcept {
    if (N == 0) return;

    if (K == 0) {
        // Pseudo-random noise excitation on unit sphere
        float sum_sq = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            // Uniform bits from range decoder
            uint32_t b = range_dec.decode_bits(8);
            float val = (static_cast<float>(b) - 128.0f) / 128.0f;
            out_vec[i] = val;
            sum_sq += val * val;
        }
        float inv_norm = (sum_sq > 1e-12f) ? (1.0f / std::sqrt(sum_sq)) : 0.0f;
        for (size_t i = 0; i < N; ++i) {
            out_vec[i] *= inv_norm;
        }
        return;
    }

    // Algebraic pulse vector decoding on S^{N-1}
    std::vector<int32_t> y(N, 0);
    uint32_t pulses_left = K;

    for (size_t i = 0; i < N && pulses_left > 0; ++i) {
        uint32_t p = range_dec.decode_uint(pulses_left + 1);
        if (p > 0) {
            uint32_t sign = range_dec.decode_bits(1);
            y[i] = sign ? -static_cast<int32_t>(p) : static_cast<int32_t>(p);
            pulses_left -= p;
        }
    }

    // Normalize to unit norm
    float sum_sq = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        sum_sq += static_cast<float>(y[i] * y[i]);
    }
    float inv_norm = (sum_sq > 1e-12f) ? (1.0f / std::sqrt(sum_sq)) : 0.0f;
    for (size_t i = 0; i < N; ++i) {
        out_vec[i] = static_cast<float>(y[i]) * inv_norm;
    }
}

MediaResult<void> CeltDecoder::decode_frame(
    OpusRangeDecoder& range_dec,
    const OpusToc& toc,
    size_t frame_samples,
    float* pcm_out
) noexcept {
    size_t channels = toc.stereo ? 2 : 1;
    size_t transform_size = frame_samples * 2;
    size_t num_bins = frame_samples;

    std::vector<float> spectrum(num_bins * channels, 0.0f);

    // Map 21 Bark bands onto num_bins
    std::vector<size_t> band_offsets(CELT_NUM_EBANDS + 1);
    for (size_t b = 0; b <= CELT_NUM_EBANDS; ++b) {
        band_offsets[b] = (CELT_EBANDS[b] * num_bins) / CELT_EBANDS[CELT_NUM_EBANDS];
    }

    for (size_t ch = 0; ch < channels; ++ch) {
        float* ch_spec = spectrum.data() + ch * num_bins;

        for (size_t b = 0; b < CELT_NUM_EBANDS; ++b) {
            size_t start = band_offsets[b];
            size_t end = band_offsets[b + 1];
            size_t band_len = (end > start) ? (end - start) : 0;
            if (band_len == 0) continue;

            // Decode coarse energy (Laplace delta from old energy)
            uint32_t coarse_sym = range_dec.decode_uint(16);
            float coarse_db = static_cast<float>(coarse_sym) * 1.5f;
            float energy_db = old_energy_[ch][b] * 0.8f + coarse_db;
            old_energy_[ch][b] = energy_db;

            // Convert dB to linear amplitude
            float amp = std::pow(10.0f, energy_db / 20.0f);

            // Decode normalized pulse vector
            std::vector<float> pvq_vec(band_len);
            uint32_t pulses = range_dec.decode_uint(8);
            decode_pvq(range_dec, band_len, pulses, pvq_vec.data());

            // Synthesize spectral coefficients
            for (size_t i = 0; i < band_len; ++i) {
                ch_spec[start + i] = pvq_vec[i] * amp;
            }
        }
    }

    // IMDCT and Windowed Overlap-Add
    std::vector<float> time_buf(transform_size);
    std::vector<float> window(transform_size);

    for (size_t n = 0; n < transform_size; ++n) {
        window[n] = static_cast<float>(
            std::sin(std::numbers::pi_v<double> / static_cast<double>(transform_size) * (n + 0.5))
        );
    }

    for (size_t ch = 0; ch < channels; ++ch) {
        const float* ch_spec = spectrum.data() + ch * num_bins;
        imdct(ch_spec, time_buf.data(), transform_size);

        if (prev_tail_[ch].size() < frame_samples) {
            prev_tail_[ch].assign(frame_samples, 0.0f);
        }

        for (size_t n = 0; n < frame_samples; ++n) {
            float win_val = time_buf[n] * window[n];
            float out_sample = prev_tail_[ch][n] + win_val;
            prev_tail_[ch][n] = time_buf[n + frame_samples] * window[n + frame_samples];

            if (channels == 1) {
                pcm_out[n] = out_sample;
            } else {
                pcm_out[n * 2 + ch] = out_sample;
            }
        }
    }

    return {};
}

} // namespace atum

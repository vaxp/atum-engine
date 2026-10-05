#include "atum/codec/audio/aac/aac_tools.hpp"
#include <cmath>
#include <algorithm>
#include <array>

namespace atum {

// Precomputed reflection coefficient mapping tables (ISO/IEC 14496-3 Table 4.148)
static const float TNS_COEF_0_3[8] = {
    0.00000000f, -0.43388373f, -0.78183150f, -0.97492790f,
    0.98480773f,  0.86602539f,  0.64278758f,  0.34202015f
};

static const float TNS_COEF_1_3[8] = {
    0.00000000f, -0.43388373f,  0.64278758f,  0.34202015f,
    0.00000000f,  0.00000000f,  0.00000000f,  0.00000000f
};

static const float TNS_COEF_0_4[16] = {
    0.00000000f, -0.20791170f, -0.40673664f, -0.58778524f,
   -0.74314481f, -0.86602539f, -0.95105654f, -0.99452192f,
    0.99573416f,  0.96126170f,  0.89516330f,  0.79801720f,
    0.67369562f,  0.52643216f,  0.36124167f,  0.18374951f
};

static const float TNS_COEF_1_4[16] = {
    0.00000000f, -0.20791170f, -0.40673664f, -0.58778524f,
    0.67369562f,  0.52643216f,  0.36124167f,  0.18374951f,
    0.00000000f,  0.00000000f,  0.00000000f,  0.00000000f,
    0.00000000f,  0.00000000f,  0.00000000f,  0.00000000f
};

void AacTools::apply_ms_stereo(
    const AacIcsInfo& ics,
    const uint8_t* ms_mask,
    float* left_spectrum,
    float* right_spectrum,
    uint32_t sample_rate
) noexcept {
    if (!ms_mask || !left_spectrum || !right_spectrum) return;

    bool is_short = (ics.window_sequence == AacWindowSequence::EightShortSequence);
    auto sfb_offsets = AacEntropyDecoder::get_sfb_offsets(sample_rate, is_short);

    size_t bin_offset = 0;
    for (uint8_t g = 0; g < ics.num_window_groups; ++g) {
        for (uint8_t w = 0; w < ics.window_group_length[g]; ++w) {
            for (uint8_t sfb = 0; sfb < ics.max_sfb; ++sfb) {
                size_t start = sfb_offsets[sfb];
                size_t end = (static_cast<size_t>(sfb + 1) < sfb_offsets.size()) ? sfb_offsets[sfb + 1] : start;
                size_t len = (end > start) ? (end - start) : 0;

                bool ms_active = (ms_mask[g * AAC_MAX_SFB + sfb] != 0);

                if (ms_active) {
                    for (size_t i = 0; i < len && (bin_offset + i) < AAC_MAX_BINS; ++i) {
                        float m = left_spectrum[bin_offset + i];
                        float s = right_spectrum[bin_offset + i];
                        left_spectrum[bin_offset + i] = m + s;
                        right_spectrum[bin_offset + i] = m - s;
                    }
                }
                bin_offset += len;
            }
        }
    }
}

MediaResult<AacTnsData> AacTools::decode_tns_data(
    BitReader& reader,
    const AacIcsInfo& ics
) noexcept {
    AacTnsData tns{};
    auto present_res = reader.read_bit();
    if (!present_res) return present_res.error();
    tns.present = (*present_res != 0);

    if (!tns.present) {
        return tns;
    }

    bool is_short = (ics.window_sequence == AacWindowSequence::EightShortSequence);
    uint32_t n_filt_bits = is_short ? 1 : 2;
    uint32_t len_bits = is_short ? 4 : 6;
    uint32_t order_bits = is_short ? 3 : 5;

    for (uint8_t w = 0; w < ics.num_windows; ++w) {
        auto nf_res = reader.read_bits(n_filt_bits);
        if (!nf_res) return nf_res.error();
        tns.n_filt[w] = static_cast<uint8_t>(*nf_res);

        if (tns.n_filt[w] > 0) {
            auto res_bit = reader.read_bit();
            if (!res_bit) return res_bit.error();
            uint32_t coef_res = *res_bit; // 0: 3 bits, 1: 4 bits
            uint32_t coef_bits = coef_res ? 4 : 3;

            for (uint8_t f = 0; f < tns.n_filt[w] && f < AAC_MAX_TNS_FILTERS; ++f) {
                auto& filt = tns.filters[w][f];
                auto len_res = reader.read_bits(len_bits);
                if (!len_res) return len_res.error();
                uint8_t length = static_cast<uint8_t>(*len_res);

                auto ord_res = reader.read_bits(order_bits);
                if (!ord_res) return ord_res.error();
                filt.order = static_cast<uint8_t>(*ord_res);

                if (filt.order > 0) {
                    auto dir_res = reader.read_bit();
                    if (!dir_res) return dir_res.error();
                    filt.direction = (*dir_res != 0);

                    auto comp_res = reader.read_bit();
                    if (!comp_res) return comp_res.error();
                    // coef_compress
                    (void)comp_res;

                    for (uint8_t i = 0; i < filt.order && i < 12; ++i) {
                        auto c_res = reader.read_bits(coef_bits);
                        if (!c_res) return c_res.error();
                        filt.coef[i] = static_cast<int8_t>(*c_res);
                    }
                }
                filt.stop_band = ics.max_sfb;
                filt.start_band = (filt.stop_band > length) ? (filt.stop_band - length) : 0;
            }
        }
    }

    return tns;
}

void AacTools::apply_tns(
    const AacIcsInfo& ics,
    const AacTnsData& tns,
    float* spectrum,
    uint32_t sample_rate
) noexcept {
    if (!tns.present || !spectrum) return;

    bool is_short = (ics.window_sequence == AacWindowSequence::EightShortSequence);
    auto sfb_offsets = AacEntropyDecoder::get_sfb_offsets(sample_rate, is_short);
    size_t window_size = is_short ? 128 : 1024;

    for (uint8_t w = 0; w < ics.num_windows; ++w) {
        float* win_spec = spectrum + w * window_size;

        for (uint8_t f = 0; f < tns.n_filt[w] && f < AAC_MAX_TNS_FILTERS; ++f) {
            const auto& filt = tns.filters[w][f];
            if (filt.order == 0) continue;

            size_t start_bin = sfb_offsets[std::min<size_t>(filt.start_band, sfb_offsets.size() - 1)];
            size_t stop_bin = sfb_offsets[std::min<size_t>(filt.stop_band, sfb_offsets.size() - 1)];
            if (start_bin >= stop_bin || stop_bin > window_size) continue;

            // Compute LPC coefficients a[k] from reflection coefficients r[k] via Levinson recursion
            float a[13]{1.0f};
            float r[13]{0.0f};
            for (uint8_t i = 0; i < filt.order && i < 12; ++i) {
                uint8_t idx = static_cast<uint8_t>(filt.coef[i]) & 0x0F;
                r[i + 1] = TNS_COEF_0_4[idx];
            }

            for (uint8_t m = 1; m <= filt.order && m <= 12; ++m) {
                float b[13];
                for (uint8_t i = 1; i < m; ++i) {
                    b[i] = a[i] + r[m] * a[m - i];
                }
                for (uint8_t i = 1; i < m; ++i) {
                    a[i] = b[i];
                }
                a[m] = r[m];
            }

            // All-pole IIR inverse filter: y[n] = x[n] - sum_{k=1}^P a_k * y[n - k]
            std::array<float, 16> state{};
            if (!filt.direction) {
                // Filter forwards: start_bin to stop_bin - 1
                for (size_t n = start_bin; n < stop_bin; ++n) {
                    float y = win_spec[n];
                    for (size_t k = 1; k <= filt.order && k <= 12; ++k) {
                        y -= a[k] * state[k - 1];
                    }
                    for (size_t k = 12; k > 0; --k) {
                        state[k] = state[k - 1];
                    }
                    state[0] = y;
                    win_spec[n] = y;
                }
            } else {
                // Filter backwards: stop_bin - 1 down to start_bin
                for (size_t n = stop_bin; n > start_bin; --n) {
                    size_t idx = n - 1;
                    float y = win_spec[idx];
                    for (size_t k = 1; k <= filt.order && k <= 12; ++k) {
                        y -= a[k] * state[k - 1];
                    }
                    for (size_t k = 12; k > 0; --k) {
                        state[k] = state[k - 1];
                    }
                    state[0] = y;
                    win_spec[idx] = y;
                }
            }
        }
    }
}

} // namespace atum

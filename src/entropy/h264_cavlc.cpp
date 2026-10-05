#include "atum/entropy/h264_cavlc.hpp"
#include <algorithm>
#include <cstdlib>
#include "atum/codec/video/h264/h264_residual_cabac.hpp"

namespace atum {

int32_t H264CavlcDecoder::compute_nc(
    bool left_avail,
    int32_t na,
    bool top_avail,
    int32_t nb
) noexcept {
    if (!left_avail && !top_avail) return 0;
    if (left_avail && !top_avail)  return std::max(na, 0);
    if (!left_avail && top_avail)  return std::max(nb, 0);
    return (std::max(na, 0) + std::max(nb, 0) + 1) >> 1;
}

MediaResult<H264CoeffToken> H264CavlcDecoder::decode_coeff_token(int32_t nc) noexcept {
    if (nc >= 8) {
        if (reader_.bits_left() < 6) return MediaError::NeedMoreData;
        auto code_res = reader_.read_bits(6);
        if (!code_res) return code_res.error();
        uint32_t code = *code_res;
        if (code == 0) return H264CoeffToken{0, 0};
        if (code > 60) return H264CoeffToken{16, 0};

        uint8_t total_coeff = static_cast<uint8_t>((code >> 2) + 1);
        uint8_t trailing_ones = static_cast<uint8_t>(code & 3);
        return H264CoeffToken{total_coeff, trailing_ones};
    }

    // Unary prefix decoding for VLC Table 0 (nc < 2), Table 1 (2 <= nc < 4), Table 2 (4 <= nc < 8)
    uint32_t prefix = 0;
    while (reader_.bits_left() > 0) {
        auto b = reader_.read_bit();
        if (!b) return b.error();
        if (*b != 0) break;
        prefix++;
        if (prefix > 16) break;
    }

    if (prefix == 0) {
        // High-probability single trailing one or zero
        if (nc < 2) return H264CoeffToken{1, 1};
        if (nc < 4) return H264CoeffToken{0, 0};
        return H264CoeffToken{0, 0};
    }

    if (prefix == 1) {
        if (reader_.bits_left() < 1) return MediaError::NeedMoreData;
        auto b = reader_.read_bit();
        if (!b) return b.error();
        uint32_t bit = *b;
        if (nc < 2) return (bit == 0) ? H264CoeffToken{0, 0} : H264CoeffToken{2, 2};
        return (bit == 0) ? H264CoeffToken{1, 1} : H264CoeffToken{2, 2};
    }

    if (prefix == 2) {
        if (reader_.bits_left() < 1) return MediaError::NeedMoreData;
        auto b = reader_.read_bit();
        if (!b) return b.error();
        uint32_t bit = *b;
        if (nc < 2) return (bit == 0) ? H264CoeffToken{1, 0} : H264CoeffToken{3, 3};
        return (bit == 0) ? H264CoeffToken{2, 1} : H264CoeffToken{3, 3};
    }

    uint8_t tc = static_cast<uint8_t>(std::min<uint32_t>(prefix, 16));
    uint8_t to = 0;
    if (reader_.bits_left() > 0) {
        auto b = reader_.read_bit();
        if (b) to = static_cast<uint8_t>(*b);
    }
    return H264CoeffToken{tc, to};
}

MediaResult<H264CoeffToken> H264CavlcDecoder::decode_chroma_dc_coeff_token() noexcept {
    if (reader_.bits_left() < 1) return MediaError::NeedMoreData;
    auto b0 = reader_.read_bit();
    if (!b0) return b0.error();
    if (*b0 == 1) return H264CoeffToken{0, 0}; // Code '1' -> 0 coeffs

    if (reader_.bits_left() < 1) return MediaError::NeedMoreData;
    auto b1 = reader_.read_bit();
    if (!b1) return b1.error();
    if (*b1 == 1) return H264CoeffToken{1, 1}; // Code '01' -> 1 coeff, 1 trailing one

    if (reader_.bits_left() < 2) return MediaError::NeedMoreData;
    auto b2 = reader_.read_bits(2);
    if (!b2) return b2.error();
    uint32_t bits = *b2;
    if (bits == 0) return H264CoeffToken{2, 2};
    if (bits == 1) return H264CoeffToken{2, 1};
    if (bits == 2) return H264CoeffToken{3, 3};
    return H264CoeffToken{4, 2};
}

MediaResult<uint32_t> H264CavlcDecoder::decode_total_zeros(uint32_t total_coeff) noexcept {
    if (total_coeff == 0 || total_coeff >= 16) return 0;

    uint32_t prefix = 0;
    while (reader_.bits_left() > 0) {
        auto b = reader_.read_bit();
        if (!b) return b.error();
        if (*b != 0) break;
        prefix++;
        if (prefix >= 16 - total_coeff) break;
    }
    return prefix;
}

MediaResult<uint32_t> H264CavlcDecoder::decode_chroma_dc_total_zeros(uint32_t total_coeff) noexcept {
    if (total_coeff == 0 || total_coeff >= 4) return 0;
    if (total_coeff == 3) {
        auto b = reader_.read_bit();
        if (!b) return b.error();
        return (*b == 0) ? 1 : 0;
    }
    if (total_coeff == 2) {
        auto b1 = reader_.read_bit();
        if (!b1) return b1.error();
        if (*b1 == 1) return 0;
        auto b2 = reader_.read_bit();
        if (!b2) return b2.error();
        return (*b2 == 0) ? 2 : 1;
    }
    // total_coeff == 1
    auto b1 = reader_.read_bit();
    if (!b1) return b1.error();
    if (*b1 == 1) return 0;
    auto b2 = reader_.read_bit();
    if (!b2) return b2.error();
    if (*b2 == 1) return 1;
    auto b3 = reader_.read_bit();
    if (!b3) return b3.error();
    return (*b3 == 0) ? 3 : 2;
}

MediaResult<uint32_t> H264CavlcDecoder::decode_run_before(uint32_t zeros_left) noexcept {
    if (zeros_left == 0) return 0;
    if (zeros_left == 1) {
        auto b = reader_.read_bit();
        if (!b) return b.error();
        return (*b == 1) ? 0 : 1;
    }
    if (zeros_left == 2) {
        auto b1 = reader_.read_bit();
        if (!b1) return b1.error();
        if (*b1 == 1) return 0;
        auto b2 = reader_.read_bit();
        if (!b2) return b2.error();
        return (*b2 == 1) ? 1 : 2;
    }

    uint32_t prefix = 0;
    while (reader_.bits_left() > 0) {
        auto b = reader_.read_bit();
        if (!b) return b.error();
        if (*b != 0) break;
        prefix++;
        if (prefix >= zeros_left) break;
    }
    return prefix;
}

MediaResult<void> H264CavlcDecoder::decode_levels(
    uint32_t total_coeff,
    uint32_t trailing_ones,
    int32_t* levels
) noexcept {
    // 1. Trailing ones signs (1 bit each, scanned in reverse)
    for (uint32_t i = 0; i < trailing_ones; ++i) {
        if (reader_.bits_left() < 1) return MediaError::NeedMoreData;
        auto s = reader_.read_bit();
        if (!s) return s.error();
        levels[total_coeff - 1 - i] = (*s != 0) ? -1 : 1;
    }

    // 2. Remaining non-zero levels
    uint32_t suffix_length = (total_coeff > 10 && trailing_ones < 3) ? 1 : 0;

    for (int32_t i = static_cast<int32_t>(total_coeff) - 1 - static_cast<int32_t>(trailing_ones); i >= 0; --i) {
        uint32_t prefix = 0;
        while (reader_.bits_left() > 0) {
            auto b = reader_.read_bit();
            if (!b) return b.error();
            if (*b != 0) break;
            prefix++;
            if (prefix >= 15) break;
        }

        uint32_t level_code = 0;
        if (prefix < 14) {
            uint32_t suffix = 0;
            if (suffix_length > 0) {
                auto s = reader_.read_bits(suffix_length);
                if (!s) return s.error();
                suffix = *s;
            }
            level_code = (prefix << suffix_length) + suffix;
        } else if (prefix == 14) {
            auto s = reader_.read_bits(4);
            if (!s) return s.error();
            level_code = (14 << suffix_length) + *s;
        } else {
            auto s = reader_.read_bits(12);
            if (!s) return s.error();
            level_code = (14 << suffix_length) + 16 + *s;
        }

        if (static_cast<uint32_t>(i) == total_coeff - 1 - trailing_ones && trailing_ones < 3) {
            level_code += 2;
        }

        int32_t level = 0;
        if (level_code % 2 == 0) {
            level = static_cast<int32_t>((level_code + 2) >> 1);
        } else {
            level = -static_cast<int32_t>((level_code + 3) >> 1);
        }

        levels[i] = level;

        // Adapt suffix length
        if (suffix_length == 0) {
            suffix_length = 1;
        }
        if (std::abs(level) > (3 << (suffix_length - 1)) && suffix_length < 6) {
            suffix_length++;
        }
    }
    return {};
}

MediaResult<void> H264CavlcDecoder::decode_runs(
    uint32_t total_coeff,
    uint32_t total_zeros,
    int32_t* runs
) noexcept {
    uint32_t zeros_left = total_zeros;

    for (int32_t i = static_cast<int32_t>(total_coeff) - 1; i > 0; --i) {
        if (zeros_left == 0) {
            runs[i] = 0;
        } else {
            auto run_res = decode_run_before(zeros_left);
            if (!run_res) return run_res.error();
            runs[i] = *run_res;
            zeros_left -= *run_res;
        }
    }
    runs[0] = zeros_left;
    return {};
}

MediaResult<uint32_t> H264CavlcDecoder::decode_residual_block_4x4(
    int32_t nc,
    int32_t* coeffs,
    uint32_t max_coeffs
) noexcept {
    std::fill_n(coeffs, max_coeffs, 0);

    auto token_res = decode_coeff_token(nc);
    if (!token_res) return token_res.error();

    uint32_t total_coeff = token_res->total_coeff;
    uint32_t trailing_ones = token_res->trailing_ones;

    if (total_coeff == 0) {
        return 0;
    }

    int32_t levels[16]{};
    auto lev_res = decode_levels(total_coeff, trailing_ones, levels);
    if (!lev_res) return lev_res.error();

    uint32_t total_zeros = 0;
    if (total_coeff < max_coeffs) {
        auto tz_res = decode_total_zeros(total_coeff);
        if (!tz_res) return tz_res.error();
        total_zeros = *tz_res;
    }

    int32_t runs[16]{};
    auto run_res = decode_runs(total_coeff, total_zeros, runs);
    if (!run_res) return run_res.error();

    // Place coefficients into zig-zag scan positions
    int32_t coeff_idx = -1;
    for (int32_t i = static_cast<int32_t>(total_coeff) - 1; i >= 0; --i) {
        coeff_idx += runs[i] + 1;
        if (coeff_idx < static_cast<int32_t>(max_coeffs)) {
            coeffs[H264ResidualCabacDecoder::zig_zag_4x4[coeff_idx]] = levels[i];
        }
    }

    return total_coeff;
}

MediaResult<uint32_t> H264CavlcDecoder::decode_chroma_dc_block(int32_t* coeffs) noexcept {
    std::fill_n(coeffs, 4, 0);

    auto token_res = decode_chroma_dc_coeff_token();
    if (!token_res) return token_res.error();

    uint32_t total_coeff = token_res->total_coeff;
    uint32_t trailing_ones = token_res->trailing_ones;

    if (total_coeff == 0) return 0;

    int32_t levels[4]{};
    auto lev_res = decode_levels(total_coeff, trailing_ones, levels);
    if (!lev_res) return lev_res.error();

    uint32_t total_zeros = 0;
    if (total_coeff < 4) {
        auto tz_res = decode_chroma_dc_total_zeros(total_coeff);
        if (!tz_res) return tz_res.error();
        total_zeros = *tz_res;
    }

    int32_t runs[4]{};
    auto run_res = decode_runs(total_coeff, total_zeros, runs);
    if (!run_res) return run_res.error();

    int32_t coeff_idx = -1;
    for (int32_t i = static_cast<int32_t>(total_coeff) - 1; i >= 0; --i) {
        coeff_idx += runs[i] + 1;
        if (coeff_idx < 4) {
            coeffs[coeff_idx] = levels[i];
        }
    }
    return total_coeff;
}

} // namespace atum

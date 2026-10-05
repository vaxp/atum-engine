#include "atum/entropy/cabac.hpp"
#include <algorithm>

namespace atum {

// ITU-T H.264 Table 9-45 / ITU-T H.265 Table 9-42: State transition table transIdxMPS
const uint8_t CabacDecoder::trans_idx_mps[64] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 62, 63
};

// ITU-T H.264 Table 9-45 / ITU-T H.265 Table 9-42: State transition table transIdxLPS
const uint8_t CabacDecoder::trans_idx_lps[64] = {
    0,  0,  1,  2,  2,  4,  4,  5,  6,  7,  8,  9,  9,  11, 11, 12,
    13, 13, 15, 15, 16, 16, 18, 18, 19, 19, 21, 21, 22, 22, 23, 24,
    24, 25, 26, 26, 27, 27, 28, 29, 29, 30, 30, 30, 31, 32, 32, 33,
    33, 33, 34, 34, 35, 35, 35, 35, 36, 36, 36, 37, 37, 37, 38, 38
};

// ITU-T H.264 Table 9-44 / ITU-T H.265 Table 9-41: rangeTabLPS[64][4]
const uint8_t CabacDecoder::range_tab_lps[64][4] = {
    { 128, 176, 208, 240 },
    { 128, 167, 197, 227 },
    { 128, 158, 187, 216 },
    { 123, 150, 178, 205 },
    { 116, 142, 169, 195 },
    { 111, 135, 160, 185 },
    { 105, 128, 152, 175 },
    { 100, 122, 144, 166 },
    {  95, 116, 137, 158 },
    {  90, 110, 130, 150 },
    {  85, 104, 123, 142 },
    {  81,  99, 117, 135 },
    {  77,  94, 111, 128 },
    {  73,  89, 105, 122 },
    {  69,  85, 100, 116 },
    {  66,  80,  95, 110 },
    {  62,  76,  90, 104 },
    {  59,  72,  86,  99 },
    {  56,  69,  81,  94 },
    {  53,  65,  77,  89 },
    {  51,  62,  73,  85 },
    {  48,  59,  69,  80 },
    {  46,  56,  66,  76 },
    {  43,  53,  63,  72 },
    {  41,  50,  59,  69 },
    {  39,  48,  56,  65 },
    {  37,  45,  54,  62 },
    {  35,  43,  51,  59 },
    {  33,  41,  48,  56 },
    {  32,  39,  46,  53 },
    {  30,  37,  43,  50 },
    {  29,  35,  41,  48 },
    {  27,  33,  39,  45 },
    {  26,  31,  37,  43 },
    {  24,  30,  35,  41 },
    {  23,  28,  33,  39 },
    {  22,  27,  32,  37 },
    {  21,  26,  30,  35 },
    {  20,  24,  29,  33 },
    {  19,  23,  27,  31 },
    {  18,  22,  26,  30 },
    {  17,  21,  25,  28 },
    {  16,  20,  23,  27 },
    {  15,  19,  22,  25 },
    {  14,  18,  21,  24 },
    {  14,  17,  20,  23 },
    {  13,  16,  19,  22 },
    {  12,  15,  18,  21 },
    {  12,  14,  17,  20 },
    {  11,  14,  16,  19 },
    {  11,  13,  15,  18 },
    {  10,  12,  15,  17 },
    {  10,  12,  14,  16 },
    {   9,  11,  13,  15 },
    {   9,  11,  12,  14 },
    {   8,  10,  12,  14 },
    {   8,   9,  11,  13 },
    {   7,   9,  11,  12 },
    {   7,   9,  10,  12 },
    {   7,   8,  10,  11 },
    {   6,   8,   9,  11 },
    {   6,   7,   9,  10 },
    {   6,   7,   8,   9 },
    {   2,   2,   2,   2 }
};

void CabacContext::init(int32_t qp, int32_t m, int32_t n) noexcept {
    int32_t pre_ctx_state = std::clamp(((m * std::clamp(qp, 0, 51)) >> 4) + n, 1, 126);
    if (pre_ctx_state <= 63) {
        state = static_cast<uint8_t>(63 - pre_ctx_state);
        mps = 0;
    } else {
        state = static_cast<uint8_t>(pre_ctx_state - 64);
        mps = 1;
    }
}

MediaResult<void> CabacDecoder::init(std::span<const uint8_t> data) noexcept {
    data_ = data;
    byte_offset_ = 0;
    bits_left_ = 0;
    curr_byte_ = 0;
    range_ = 0x01FE; // 510

    if (data_.empty()) {
        offset_ = 0;
        return MediaError::NeedMoreData;
    }

    // Read first 9 bits into offset (standard H.264 9.3.1.2)
    offset_ = 0;
    for (int i = 0; i < 9; ++i) {
        offset_ = (offset_ << 1) | read_bit();
    }

    return MediaResult<void>::ok();
}

uint32_t CabacDecoder::decode_bin(CabacContext& ctx) noexcept {
    uint32_t q_index = (range_ >> 6) & 3;
    uint32_t lps_range = range_tab_lps[ctx.state][q_index];
    range_ -= lps_range;

    uint32_t bin;
    if (offset_ < range_) {
        // MPS path
        bin = ctx.mps;
        ctx.state = trans_idx_mps[ctx.state];
    } else {
        // LPS path
        bin = 1 - ctx.mps;
        offset_ -= range_;
        range_ = lps_range;
        if (ctx.state == 0) {
            ctx.mps = 1 - ctx.mps;
        }
        ctx.state = trans_idx_lps[ctx.state];
    }

    // Renormalization
    while (range_ < 256) {
        range_ <<= 1;
        offset_ = (offset_ << 1) | read_bit();
    }

    return bin;
}

uint32_t CabacDecoder::decode_bypass_bin() noexcept {
    offset_ = (offset_ << 1) | read_bit();
    if (offset_ >= range_) {
        offset_ -= range_;
        return 1;
    }
    return 0;
}

uint32_t CabacDecoder::decode_bypass_bins(uint32_t count) noexcept {
    uint32_t value = 0;
    for (uint32_t i = 0; i < count; ++i) {
        value = (value << 1) | decode_bypass_bin();
    }
    return value;
}

uint32_t CabacDecoder::decode_terminate() noexcept {
    range_ -= 2;
    if (offset_ >= range_) {
        return 1;
    }
    while (range_ < 256) {
        range_ <<= 1;
        offset_ = (offset_ << 1) | read_bit();
    }
    return 0;
}

uint32_t CabacDecoder::decode_exp_golomb(uint32_t k) noexcept {
    uint32_t prefix = 0;
    while (decode_bypass_bin() != 0 && prefix < 31) {
        prefix++;
        if (is_end_of_substream()) break;
    }
    if (prefix == 0) {
        return (k == 0) ? 0 : decode_bypass_bins(k);
    }
    uint32_t suffix = decode_bypass_bins(prefix + k);
    if (prefix + k >= 31) return suffix;
    return ((1U << (prefix + k)) - (1U << k)) + suffix;
}

} // namespace atum

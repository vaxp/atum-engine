#include "atum/entropy/vp9_bool_decoder.hpp"
#include <bit>

namespace atum {

namespace {

// Normalization shift table for range in 1..255 (number of shifts until range >= 128)
constexpr uint8_t compute_norm(uint32_t range) noexcept {
    if (range == 0) return 8;
    return static_cast<uint8_t>(std::countl_zero(static_cast<uint8_t>(range)));
}

} // anonymous namespace

MediaResult<void> Vp9BoolDecoder::init(std::span<const uint8_t> data) noexcept {
    data_ = data;
    byte_offset_ = 0;
    value_ = 0;
    range_ = 255;
    count_ = -8;

    if (data_.empty()) {
        return MediaError::NeedMoreData;
    }

    // Prefill the 16-bit window with first 2 bytes (or 1 byte if single byte stream)
    uint32_t b0 = data_[byte_offset_++];
    uint32_t b1 = (byte_offset_ < data_.size()) ? data_[byte_offset_++] : 0;
    value_ = (b0 << 8) | b1;

    return MediaResult<void>::ok();
}

uint32_t Vp9BoolDecoder::decode_bool(uint8_t prob) noexcept {
    uint32_t split = 1 + (((range_ - 1) * static_cast<uint32_t>(prob)) >> 8);
    uint32_t big_split = split << 8;
    uint32_t bit;

    if (static_cast<uint32_t>(value_) >= big_split) {
        range_ -= split;
        value_ -= big_split;
        bit = 1;
    } else {
        range_ = split;
        bit = 0;
    }

    if (range_ < 128) {
        uint32_t shift = compute_norm(range_);
        range_ <<= shift;
        value_ <<= shift;
        count_ += static_cast<int32_t>(shift);

        if (count_ >= 0) {
            uint32_t next_byte = (byte_offset_ < data_.size()) ? data_[byte_offset_++] : 0;
            value_ |= (next_byte << count_);
            count_ -= 8;
        }
    }

    return bit;
}

uint32_t Vp9BoolDecoder::decode_literal(size_t count) noexcept {
    uint32_t val = 0;
    for (size_t i = 0; i < count; ++i) {
        val = (val << 1) | decode_bool(128);
    }
    return val;
}

int32_t Vp9BoolDecoder::decode_signed_literal(size_t count) noexcept {
    uint32_t mag = decode_literal(count);
    uint32_t sign = decode_bool(128);
    return sign ? -static_cast<int32_t>(mag) : static_cast<int32_t>(mag);
}

int32_t Vp9BoolDecoder::decode_tree(const int8_t* tree, const uint8_t* probs) noexcept {
    int8_t i = 0;
    while ((i = tree[i + decode_bool(probs[i >> 1])]) > 0) {}
    return -i;
}

} // namespace atum

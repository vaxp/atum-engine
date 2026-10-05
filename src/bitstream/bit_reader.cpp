#include "atum/bitstream/bit_reader.hpp"
#include <algorithm>

namespace atum {

MediaResult<uint32_t> BitReader::read_bit() noexcept {
    if (byte_offset_ >= size_bytes_) {
        return MediaError::EndOfStream;
    }

    uint32_t bit = (data_[byte_offset_] >> (7 - bit_offset_)) & 1;
    bit_offset_++;
    if (bit_offset_ == 8) {
        bit_offset_ = 0;
        byte_offset_++;
    }
    return bit;
}

MediaResult<uint32_t> BitReader::read_bits(size_t count) noexcept {
    if (count == 0) return 0u;
    if (count > 32) return MediaError::InvalidData;
    if (bits_left() < count) return MediaError::EndOfStream;

    uint32_t result = 0;
    while (count > 0) {
        size_t bits_in_current_byte = 8 - bit_offset_;
        size_t to_take = std::min(count, bits_in_current_byte);

        uint32_t mask = (1u << to_take) - 1u;
        size_t shift = bits_in_current_byte - to_take;
        uint32_t bits = (data_[byte_offset_] >> shift) & mask;

        result = (result << to_take) | bits;
        count -= to_take;
        bit_offset_ += to_take;

        if (bit_offset_ == 8) {
            bit_offset_ = 0;
            byte_offset_++;
        }
    }

    return result;
}

MediaResult<uint32_t> BitReader::peek_bits(size_t count) noexcept {
    size_t saved_byte = byte_offset_;
    size_t saved_bit = bit_offset_;
    auto res = read_bits(count);
    byte_offset_ = saved_byte;
    bit_offset_ = saved_bit;
    return res;
}

MediaResult<void> BitReader::skip_bits(size_t count) noexcept {
    if (bits_left() < count) return MediaError::EndOfStream;

    size_t total_bit = bit_offset_ + count;
    byte_offset_ += total_bit / 8;
    bit_offset_ = total_bit % 8;
    return MediaResult<void>::ok();
}

MediaResult<uint32_t> BitReader::read_ue() noexcept {
    size_t leading_zeros = 0;
    while (true) {
        auto bit = read_bit();
        if (!bit.is_ok()) return bit.error();
        if (*bit == 1) break;
        leading_zeros++;
        if (leading_zeros > 31) return MediaError::InvalidData;
    }

    if (leading_zeros == 0) return 0u;

    auto info = read_bits(leading_zeros);
    if (!info.is_ok()) return info.error();

    return ((1u << leading_zeros) - 1u) + *info;
}

MediaResult<int32_t> BitReader::read_se() noexcept {
    auto ue = read_ue();
    if (!ue.is_ok()) return ue.error();

    uint32_t val = *ue;
    if (val == 0) return 0;

    int32_t sign = (val & 1) ? 1 : -1;
    int32_t mag = static_cast<int32_t>((val + 1) / 2);
    return mag * sign;
}

MediaResult<uint32_t> BitReader::read_te(uint32_t range) noexcept {
    if (range == 0) return 0u;
    if (range == 1) {
        auto b = read_bit();
        if (!b.is_ok()) return b.error();
        return 1u - *b;
    }
    return read_ue();
}

} // namespace atum

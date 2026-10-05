#include "atum/entropy/av1_symbol_decoder.hpp"
#include <algorithm>

namespace atum {

MediaResult<void> Av1SymbolDecoder::init(std::span<const uint8_t> data) noexcept {
    data_ = data;
    byte_offset_ = 0;
    bit_buffer_ = 0;
    bits_in_buffer_ = 0;
    range_ = 32768; // 2^15

    if (data_.empty()) {
        diff_ = 0;
        return MediaError::NeedMoreData;
    }

    // Prefill 15 bits into difference register
    diff_ = 0;
    for (int i = 0; i < 15; ++i) {
        diff_ = (diff_ << 1) | read_bit();
    }

    return MediaResult<void>::ok();
}

uint32_t Av1SymbolDecoder::read_bit() noexcept {
    if (bits_in_buffer_ == 0) {
        if (byte_offset_ < data_.size()) {
            bit_buffer_ = data_[byte_offset_++];
            bits_in_buffer_ = 8;
        } else {
            bit_buffer_ = 0;
            bits_in_buffer_ = 8;
        }
    }
    bits_in_buffer_--;
    return (bit_buffer_ >> bits_in_buffer_) & 1;
}

void Av1SymbolDecoder::renormalize() noexcept {
    while (range_ < 16384) {
        range_ <<= 1;
        diff_ = (diff_ << 1) | read_bit();
    }
}

uint32_t Av1SymbolDecoder::decode_symbol(uint16_t* cdf, size_t n_symbols, bool adapt) noexcept {
    if (n_symbols < 2) return 0;

    uint32_t prev_range = range_;
    uint32_t cur_subrange = range_;
    uint32_t symbol = 0;

    for (size_t s = 0; s < n_symbols - 1; ++s) {
        prev_range = cur_subrange;
        cur_subrange = (range_ * (static_cast<uint32_t>(cdf[s]) >> 6)) >> 9;

        if (diff_ >= cur_subrange) {
            symbol = static_cast<uint32_t>(s);
            range_ = prev_range - cur_subrange;
            diff_ -= cur_subrange;
            goto symbol_found;
        }
    }

    // Final symbol in alphabet
    symbol = static_cast<uint32_t>(n_symbols - 1);
    range_ = cur_subrange;

symbol_found:
    renormalize();

    if (adapt) {
        update_cdf(cdf, n_symbols, symbol);
    }

    return symbol;
}

uint32_t Av1SymbolDecoder::decode_bit() noexcept {
    uint32_t split = range_ >> 1;
    uint32_t bit = 0;

    if (diff_ >= split) {
        bit = 1;
        diff_ -= split;
        range_ -= split;
    } else {
        range_ = split;
    }

    renormalize();
    return bit;
}

uint32_t Av1SymbolDecoder::decode_literal(size_t count) noexcept {
    uint32_t val = 0;
    for (size_t i = 0; i < count; ++i) {
        val = (val << 1) | decode_bit();
    }
    return val;
}

void Av1SymbolDecoder::update_cdf(uint16_t* cdf, size_t n_symbols, uint32_t symbol) noexcept {
    if (n_symbols < 2) return;

    // Count is stored immediately following the CDF array
    uint16_t count = cdf[n_symbols];
    uint32_t rate = 3 + (count > 15) + (count > 31) + std::min(count >> 5, 2);

    for (size_t i = 0; i < n_symbols - 1; ++i) {
        if (i < symbol) {
            cdf[i] += static_cast<uint16_t>((32768 - cdf[i]) >> rate);
        } else {
            cdf[i] -= static_cast<uint16_t>(cdf[i] >> rate);
        }
    }

    if (count < 32) {
        cdf[n_symbols] = count + 1;
    }
}

} // namespace atum

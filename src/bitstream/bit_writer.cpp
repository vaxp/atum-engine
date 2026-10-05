#include "atum/bitstream/bit_writer.hpp"
#include <bit>

namespace atum {

void BitWriter::write_bit(uint32_t bit) {
    current_byte_ = static_cast<uint8_t>((current_byte_ << 1) | (bit & 1));
    bit_offset_++;
    if (bit_offset_ == 8) {
        buffer_.push_back(current_byte_);
        current_byte_ = 0;
        bit_offset_ = 0;
    }
}

void BitWriter::write_bits(uint32_t value, size_t count) {
    if (count == 0) return;
    for (size_t i = count; i > 0; --i) {
        uint32_t bit = (value >> (i - 1)) & 1;
        write_bit(bit);
    }
}

void BitWriter::write_ue(uint32_t val) {
    if (val == 0) {
        write_bit(1);
        return;
    }

    uint64_t v = static_cast<uint64_t>(val) + 1;
    size_t m = std::bit_width(v) - 1;

    // Write M leading zeros
    for (size_t i = 0; i < m; ++i) {
        write_bit(0);
    }
    // Write (M + 1) bits of v
    write_bits(static_cast<uint32_t>(v), m + 1);
}

void BitWriter::write_se(int32_t val) {
    uint32_t ue = 0;
    if (val <= 0) {
        ue = static_cast<uint32_t>(-2 * val);
    } else {
        ue = static_cast<uint32_t>(2 * val - 1);
    }
    write_ue(ue);
}

void BitWriter::align(uint32_t pad_bit) {
    while (bit_offset_ > 0) {
        write_bit(pad_bit & 1);
    }
}

} // namespace atum

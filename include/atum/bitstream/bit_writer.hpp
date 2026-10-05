#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include "atum/core/media_types.hpp"

namespace atum {

/// High-performance bitstream writer for variable length codes and Exp-Golomb serialization.
class BitWriter {
public:
    BitWriter() = default;
    explicit BitWriter(size_t initial_capacity_bytes) {
        buffer_.reserve(initial_capacity_bytes);
    }

    /// Writes 1 bit (0 or 1) to the bitstream.
    void write_bit(uint32_t bit);

    /// Writes `count` bits (1 to 32) of `value` to the bitstream (MSB first).
    void write_bits(uint32_t value, size_t count);

    /// Writes an unsigned Exponential-Golomb integer (ue(v)).
    void write_ue(uint32_t val);

    /// Writes a signed Exponential-Golomb integer (se(v)).
    void write_se(int32_t val);

    /// Pads the bitstream to the next byte boundary with `pad_bit` (0 or 1).
    void align(uint32_t pad_bit = 0);

    /// Flushes any pending bits (padded with 0).
    void flush() {
        align(0);
    }

    /// Total bits written.
    [[nodiscard]] size_t bit_count() const noexcept {
        return buffer_.size() * 8 + bit_offset_;
    }

    /// Returns written byte buffer (aligned).
    [[nodiscard]] const std::vector<uint8_t>& buffer() const noexcept { return buffer_; }
    [[nodiscard]] std::span<const uint8_t> as_span() const noexcept { return buffer_; }
    [[nodiscard]] size_t byte_size() const noexcept { return buffer_.size(); }

    void clear() noexcept {
        buffer_.clear();
        current_byte_ = 0;
        bit_offset_ = 0;
    }

private:
    std::vector<uint8_t> buffer_;
    uint8_t current_byte_{0};
    size_t bit_offset_{0}; // Number of valid bits in current_byte_ (0 to 7)
};

} // namespace atum

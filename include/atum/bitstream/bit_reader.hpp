#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <optional>
#include "atum/core/media_types.hpp"

namespace atum {

/// High-throughput zero-allocation bitstream reader supporting arbitrary bit-width and Exp-Golomb decoding.
class BitReader {
public:
    constexpr BitReader() noexcept = default;
    constexpr BitReader(const uint8_t* data, size_t size_in_bytes) noexcept
        : data_(data), size_bytes_(size_in_bytes) {}
    constexpr explicit BitReader(std::span<const uint8_t> bytes) noexcept
        : data_(bytes.data()), size_bytes_(bytes.size()) {}

    /// Reads 1 bit from the stream. Returns 0 or 1, or error if EOF.
    MediaResult<uint32_t> read_bit() noexcept;

    /// Reads `count` bits (1 to 32) from the stream (MSB first).
    MediaResult<uint32_t> read_bits(size_t count) noexcept;

    /// Peeks `count` bits (1 to 32) without advancing the stream cursor.
    MediaResult<uint32_t> peek_bits(size_t count) noexcept;

    /// Skips `count` bits in the stream.
    MediaResult<void> skip_bits(size_t count) noexcept;

    /// Reads an unsigned Exponential-Golomb coded integer (ue(v) as used in H.264/H.265).
    MediaResult<uint32_t> read_ue() noexcept;

    /// Reads a signed Exponential-Golomb coded integer (se(v) as used in H.264/H.265).
    MediaResult<int32_t> read_se() noexcept;

    /// Reads a truncated Exponential-Golomb coded integer (te(v)).
    MediaResult<uint32_t> read_te(uint32_t range) noexcept;

    /// Aligns read cursor to the next byte boundary, discarding any leftover bits in the current byte.
    void align_to_byte() noexcept {
        if (bit_offset_ != 0) {
            byte_offset_++;
            bit_offset_ = 0;
        }
    }

    /// Returns true if more bits are available.
    [[nodiscard]] bool has_bits(size_t count) const noexcept {
        return bits_left() >= count;
    }

    /// Checks whether there is more data in the RBSP before the rbsp_stop_one_bit (H.264 / H.265 Section 7.2).
    [[nodiscard]] bool more_rbsp_data() const noexcept {
        if (byte_offset_ >= size_bytes_) return false;
        size_t last_byte = size_bytes_;
        while (last_byte > byte_offset_ && data_[last_byte - 1] == 0) {
            last_byte--;
        }
        if (last_byte == 0 || last_byte < byte_offset_ + 1) {
            return false;
        }
        uint8_t b = data_[last_byte - 1];
        size_t last_bit_pos = (last_byte - 1) * 8 + (7 - __builtin_ctz(b));
        size_t current_pos = byte_offset_ * 8 + bit_offset_;
        return current_pos < last_bit_pos;
    }

    /// Total bits remaining in the bitstream.
    [[nodiscard]] size_t bits_left() const noexcept {
        if (byte_offset_ >= size_bytes_) return 0;
        size_t total_bits = size_bytes_ * 8;
        size_t consumed = byte_offset_ * 8 + bit_offset_;
        return total_bits > consumed ? total_bits - consumed : 0;
    }

    /// Total bytes remaining in the stream (rounded down).
    [[nodiscard]] size_t bytes_left() const noexcept {
        return bits_left() / 8;
    }

    /// Total bits read so far.
    [[nodiscard]] size_t bits_read() const noexcept {
        return byte_offset_ * 8 + bit_offset_;
    }

    /// Current byte offset.
    [[nodiscard]] size_t byte_offset() const noexcept { return byte_offset_; }

    /// Current bit offset within current byte (0 to 7).
    [[nodiscard]] size_t bit_offset() const noexcept { return bit_offset_; }

    /// Returns a direct span to the remaining unconsumed bytes (aligned).
    [[nodiscard]] std::span<const uint8_t> remaining_bytes() const noexcept {
        size_t offset = byte_offset_ + (bit_offset_ > 0 ? 1 : 0);
        if (offset >= size_bytes_) return {};
        return {data_ + offset, size_bytes_ - offset};
    }

private:
    const uint8_t* data_{nullptr};
    size_t size_bytes_{0};
    size_t byte_offset_{0};
    size_t bit_offset_{0}; // 0 = MSB, 7 = LSB
};

} // namespace atum

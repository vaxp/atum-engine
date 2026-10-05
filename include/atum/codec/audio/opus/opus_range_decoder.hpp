#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include "atum/core/media_types.hpp"

namespace atum {

/// 8-bit Arithmetic Range Decoder for Opus / CELT (RFC 6716 Section 4.1).
class OpusRangeDecoder {
public:
    OpusRangeDecoder() = default;

    /// Initializes the range decoder with packet payload bytes.
    void init(std::span<const uint8_t> data) noexcept;

    /// Computes the cumulative frequency for the next symbol given total frequency `ft`.
    [[nodiscard]] uint32_t decode(uint32_t ft) noexcept;

    /// Advances the range decoder state for symbol with range [fl, fh) over total frequency `ft`.
    void update(uint32_t fl, uint32_t fh, uint32_t ft) noexcept;

    /// Decodes an integer uniformly distributed in [0, 2^nbits - 1].
    [[nodiscard]] uint32_t decode_bits(uint32_t nbits) noexcept;

    /// Decodes an integer uniformly distributed in [0, total - 1].
    [[nodiscard]] uint32_t decode_uint(uint32_t total) noexcept;

    /// Returns the number of whole bits consumed so far.
    [[nodiscard]] uint32_t tell_bits() const noexcept;

    /// Checks if any error or premature end of buffer was encountered.
    [[nodiscard]] bool has_error() const noexcept { return error_; }

private:
    void normalize() noexcept;
    uint8_t read_byte() noexcept;

    std::span<const uint8_t> buf_{};
    size_t offs_{0};
    uint32_t val_{0};
    uint32_t rng_{0};
    uint32_t ext_{0};
    int32_t rem_{0};
    uint32_t bits_consumed_{0};
    bool error_{false};
};

} // namespace atum

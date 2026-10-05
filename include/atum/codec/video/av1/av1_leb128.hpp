#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>
#include <span>
#include "atum/core/media_types.hpp"

namespace atum {

/// Zero-allocation LEB128 (Little Endian Base 128) variable length integer codec.
class Av1Leb128 {
public:
    /// Decodes an unsigned LEB128 integer from `data`.
    /// Returns a pair of [decoded_value, bytes_consumed] or an error.
    static MediaResult<std::pair<uint64_t, size_t>> decode(const uint8_t* data, size_t size) noexcept;

    /// Decodes an unsigned LEB128 integer from a byte span.
    static MediaResult<std::pair<uint64_t, size_t>> decode(std::span<const uint8_t> bytes) noexcept {
        return decode(bytes.data(), bytes.size());
    }

    /// Encodes an unsigned integer to LEB128 format into `out_buffer`.
    /// `out_buffer` must have capacity of at least 8 bytes.
    /// Returns the number of bytes written.
    static size_t encode(uint64_t value, uint8_t* out_buffer) noexcept;
};

} // namespace atum

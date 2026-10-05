#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include "atum/core/media_types.hpp"

namespace atum {

/// Sovereign VP9 Boolean Arithmetic Entropy Decoder (BoolDecoder).
/// Implements WebM VP9 Bitstream & Decoding Process Specification, Section 9.2.
class Vp9BoolDecoder {
public:
    Vp9BoolDecoder() noexcept = default;
    explicit Vp9BoolDecoder(std::span<const uint8_t> data) noexcept {
        (void)init(data);
    }

    /// Initializes the BoolDecoder with uncompressed bitstream partition payload.
    MediaResult<void> init(std::span<const uint8_t> data) noexcept;

    /// Decodes a single boolean bit with probability `prob` (1..255) that the bit is 0.
    uint32_t decode_bool(uint8_t prob) noexcept;

    /// Decodes an equiprobable bit (prob = 128).
    uint32_t decode_bit() noexcept {
        return decode_bool(128);
    }

    /// Decodes `count` literal equiprobable bits (MSB first).
    uint32_t decode_literal(size_t count) noexcept;

    /// Decodes a signed literal (magnitude with `count` bits, plus 1 sign bit).
    int32_t decode_signed_literal(size_t count) noexcept;

    /// Decodes a symbol through a VP9 binary tree.
    /// `tree` is formatted as standard VP9 binary tree array.
    /// `probs` is array of probabilities corresponding to branch nodes.
    int32_t decode_tree(const int8_t* tree, const uint8_t* probs) noexcept;

    /// Number of bytes consumed from input so far.
    [[nodiscard]] size_t bytes_consumed() const noexcept {
        return byte_offset_;
    }

    /// Checks if decoder has exhausted input stream.
    [[nodiscard]] bool is_exhausted() const noexcept {
        return byte_offset_ >= data_.size() && count_ < 0;
    }

private:
    std::span<const uint8_t> data_{};
    size_t byte_offset_{0};
    uint64_t value_{0};  ///< Shift register holding buffered compressed bits
    uint32_t range_{255}; ///< Active interval range (128..255)
    int32_t count_{0};   ///< Bits remaining before next byte fetch
};

} // namespace atum

#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <array>
#include "atum/core/media_types.hpp"

namespace atum {

/// Sovereign AV1 Multi-Symbol CDF Arithmetic Entropy Decoder.
/// Implements Alliance for Open Media AV1 Bitstream & Decoding Process Specification, Section 8.2.
class Av1SymbolDecoder {
public:
    Av1SymbolDecoder() noexcept = default;
    explicit Av1SymbolDecoder(std::span<const uint8_t> data) noexcept {
        (void)init(data);
    }

    /// Initializes the symbol decoder with an uncompressed tile / OBU payload.
    MediaResult<void> init(std::span<const uint8_t> data) noexcept;

    /// Decodes a multi-symbol from a CDF array of size `n_symbols`.
    /// If `adapt` is true, automatically updates the CDF model in-place according to AV1 rules.
    uint32_t decode_symbol(uint16_t* cdf, size_t n_symbols, bool adapt = true) noexcept;

    /// Decodes a single binary decision with a 2-symbol CDF.
    uint32_t decode_bool(uint16_t* cdf, bool adapt = true) noexcept {
        return decode_symbol(cdf, 2, adapt);
    }

    /// Decodes an equiprobable bit (probability = 0.5).
    uint32_t decode_bit() noexcept;

    /// Decodes `count` equiprobable bits (MSB first).
    uint32_t decode_literal(size_t count) noexcept;

    /// Adapts the CDF table in-place based on the observed symbol (Section 8.2.5).
    static void update_cdf(uint16_t* cdf, size_t n_symbols, uint32_t symbol) noexcept;

    /// Number of bytes consumed from input so far.
    [[nodiscard]] size_t bytes_consumed() const noexcept {
        return byte_offset_;
    }

    /// Current 15-bit arithmetic range (16384..32767).
    [[nodiscard]] uint32_t range() const noexcept {
        return range_;
    }

private:
    std::span<const uint8_t> data_{};
    size_t byte_offset_{0};
    uint32_t range_{32768}; ///< 15-bit active range
    uint32_t diff_{0};      ///< Windowed difference value
    uint32_t bit_buffer_{0};
    int32_t bits_in_buffer_{0};

    uint32_t read_bit() noexcept;
    void renormalize() noexcept;
};

} // namespace atum

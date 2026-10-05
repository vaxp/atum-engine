#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include <vector>
#include "atum/core/media_types.hpp"

namespace atum {

/// Context model representing state and MPS for CABAC arithmetic decoding.
struct CabacContext {
    uint8_t state{0}; ///< 6-bit probability state index (0..62 for H.264, 0..63 for HEVC)
    uint8_t mps{0};   ///< Most Probable Symbol (0 or 1)

    constexpr CabacContext() noexcept = default;
    constexpr CabacContext(uint8_t st, uint8_t m) noexcept : state(st), mps(m) {}

    /// Initializes context state from slice QP and model tables (H.264 / HEVC spec).
    void init(int32_t qp, int32_t m, int32_t n) noexcept;
};

/// High-performance sovereign CABAC Arithmetic Decoder.
/// Implements ISO/IEC 14496-10 (H.264) Section 9.3 and ISO/IEC 23008-2 (HEVC) Section 9.3.
class CabacDecoder {
public:
    CabacDecoder() noexcept = default;
    explicit CabacDecoder(std::span<const uint8_t> data) noexcept {
        (void)init(data);
    }

    /// Initializes or resets the CABAC engine with raw bitstream payload (SODB/RBSP).
    MediaResult<void> init(std::span<const uint8_t> data) noexcept;

    /// Decodes a regular decision bin utilizing a given context model.
    /// Updates the context model's state and MPS in-place.
    uint32_t decode_bin(CabacContext& ctx) noexcept;

    /// Decodes a bypass bin (equiprobable bin with p = 0.5, e.g. for signs or suffixes).
    uint32_t decode_bypass_bin() noexcept;

    /// Decodes multiple bypass bins (up to 32 bits).
    uint32_t decode_bypass_bins(uint32_t count) noexcept;

    /// Decodes a terminating bin (e.g. end_of_slice_flag).
    uint32_t decode_terminate() noexcept;

    /// Decodes a truncated Exp-Golomb (EGk) codeword in bypass mode.
    uint32_t decode_exp_golomb(uint32_t k) noexcept;

    /// Checks if there are more bits/bytes in the stream or if end of slice is reached.
    [[nodiscard]] bool is_end_of_substream() const noexcept {
        return byte_offset_ >= data_.size();
    }

    [[nodiscard]] size_t bytes_consumed() const noexcept {
        return byte_offset_;
    }

    [[nodiscard]] uint32_t range() const noexcept { return range_; }
    [[nodiscard]] uint32_t offset() const noexcept { return offset_; }

    // --- Standard H.264 & HEVC CABAC Tables ---
    static const uint8_t trans_idx_lps[64];
    static const uint8_t trans_idx_mps[64];
    static const uint8_t range_tab_lps[64][4];

private:
    std::span<const uint8_t> data_{};
    size_t byte_offset_{0};
    uint8_t curr_byte_{0};
    uint8_t bits_left_{0};
    uint32_t range_{0x01FE}; // codIRange (initially 510)
    uint32_t offset_{0};     // codIOffset (holds buffered arithmetic state)

    inline uint32_t read_bit() noexcept {
        if (bits_left_ == 0) {
            if (byte_offset_ < data_.size()) {
                curr_byte_ = data_[byte_offset_++];
                bits_left_ = 8;
            } else {
                return 0;
            }
        }
        bits_left_--;
        return (curr_byte_ >> bits_left_) & 1;
    }
};

} // namespace atum

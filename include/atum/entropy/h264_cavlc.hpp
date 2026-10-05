#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"

namespace atum {

/// Structure holding parsed CAVLC coefficient token.
struct H264CoeffToken {
    uint8_t total_coeff{0};   ///< Total non-zero coefficients (0..16)
    uint8_t trailing_ones{0}; ///< Number of trailing +/-1 coefficients (0..3)
};

/// Sovereign H.264 / AVC Context-Adaptive Variable-Length Coding (CAVLC) Engine.
/// Implements ISO/IEC 14496-10 Section 9.2.
class H264CavlcDecoder {
public:
    explicit H264CavlcDecoder(BitReader& reader) noexcept
        : reader_(reader) {}

    /// Computes context nC from left block nA and top block nB (Section 9.2.1).
    [[nodiscard]] static int32_t compute_nc(
        bool left_avail,
        int32_t na,
        bool top_avail,
        int32_t nb
    ) noexcept;

    /// Decodes coeff_token for standard 4x4 luma/chroma AC block (Table 9-5).
    MediaResult<H264CoeffToken> decode_coeff_token(int32_t nc) noexcept;

    /// Decodes coeff_token for 2x2 Chroma DC block (Table 9-6).
    MediaResult<H264CoeffToken> decode_chroma_dc_coeff_token() noexcept;

    /// Decodes total_zeros for a 4x4 block given total_coeff (Table 9-7).
    MediaResult<uint32_t> decode_total_zeros(uint32_t total_coeff) noexcept;

    /// Decodes total_zeros for a 2x2 Chroma DC block (Table 9-8).
    MediaResult<uint32_t> decode_chroma_dc_total_zeros(uint32_t total_coeff) noexcept;

    /// Decodes run_before for a coefficient given remaining zeros (Table 9-9 / 9-10).
    MediaResult<uint32_t> decode_run_before(uint32_t zeros_left) noexcept;

    /// Decodes a complete 4x4 CAVLC residual block.
    /// Returns the number of non-zero coefficients decoded.
    MediaResult<uint32_t> decode_residual_block_4x4(
        int32_t nc,
        int32_t* coeffs,
        uint32_t max_coeffs = 16
    ) noexcept;

    /// Decodes a 2x2 Chroma DC block (4 coefficients).
    MediaResult<uint32_t> decode_chroma_dc_block(int32_t* coeffs) noexcept;

private:
    BitReader& reader_;

    MediaResult<void> decode_levels(
        uint32_t total_coeff,
        uint32_t trailing_ones,
        int32_t* levels
    ) noexcept;

    MediaResult<void> decode_runs(
        uint32_t total_coeff,
        uint32_t total_zeros,
        int32_t* runs
    ) noexcept;
};

} // namespace atum

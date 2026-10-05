#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/entropy/cabac.hpp"
#include "atum/entropy/h264_cabac_contexts.hpp"

namespace atum {

/// H.264 residual block categorization for CABAC context mapping (Table 9-32).
enum class H264BlockCategory : uint8_t {
    LumaDc = 0,         ///< Intra 16x16 Luma DC (16 coeffs)
    LumaAc = 1,         ///< Intra 16x16 Luma AC (15 coeffs)
    Luma4x4 = 2,        ///< Standard 4x4 Luma residual (16 coeffs)
    ChromaDc = 3,       ///< Chroma DC (4 coeffs for 4:2:0)
    ChromaAc = 4,       ///< Chroma AC (15 coeffs for 4:2:0)
    Luma8x8 = 5,        ///< High profile 8x8 Luma residual (64 coeffs)
    Cb8x8 = 6,          ///< High profile 8x8 Cb residual (64 coeffs)
    Cr8x8 = 7           ///< High profile 8x8 Cr residual (64 coeffs)
};

/// Sovereign H.264 / AVC CABAC Residual Syntax Engine.
/// Implements ISO/IEC 14496-10 Section 9.3.3.1.3 (Residual coefficient parsing).
class H264ResidualCabacDecoder {
public:
    H264ResidualCabacDecoder(CabacDecoder& cabac, H264CabacContextSet& contexts) noexcept
        : cabac_(cabac), contexts_(contexts) {}

    /// Decodes coded_block_flag for a given block category.
    bool decode_coded_block_flag(
        H264BlockCategory cat,
        bool left_avail,
        bool left_cbf,
        bool top_avail,
        bool top_cbf
    ) noexcept;

    /// Decodes a 4x4 residual block (16 coefficients, or 15 for AC).
    /// Returns number of non-zero coefficients decoded.
    uint32_t decode_residual_block_4x4(
        H264BlockCategory cat,
        int32_t* coeffs,
        uint32_t max_num_coeffs = 16
    ) noexcept;

    /// Decodes an 8x8 residual block (64 coefficients) for High Profile.
    uint32_t decode_residual_block_8x8(
        H264BlockCategory cat,
        int32_t* coeffs
    ) noexcept;

    /// Decodes all residual blocks of a 16x16 macroblock into output coefficient buffers.
    /// `luma_coeffs`: 16 blocks of 16 coeffs = 256 coeffs.
    /// `cb_coeffs`, `cr_coeffs`: 4 blocks of 16 coeffs = 64 coeffs each.
    MediaResult<void> decode_macroblock_residual(
        bool is_intra16x16,
        uint8_t cbp_luma,
        uint8_t cbp_chroma,
        int32_t* luma_coeffs,
        int32_t* cb_coeffs,
        int32_t* cr_coeffs
    ) noexcept;

    // Standard Zig-Zag 4x4 and 8x8 scan order tables
    static const uint8_t zig_zag_4x4[16];
    static const uint8_t zig_zag_8x8[64];

private:
    CabacDecoder& cabac_;
    H264CabacContextSet& contexts_;

    uint32_t decode_significant_map_4x4(
        H264BlockCategory cat,
        bool* sig_flags,
        uint32_t max_coeffs
    ) noexcept;

    void decode_coeff_levels(
        H264BlockCategory cat,
        const bool* sig_flags,
        uint32_t num_coeffs,
        int32_t* coeffs
    ) noexcept;
};

} // namespace atum

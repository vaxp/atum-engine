#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include "atum/entropy/cabac.hpp"
#include "atum/codec/video/h264/h264_types.hpp"

namespace atum {

/// Total number of standard CABAC context models for H.264 / AVC (including High Profile 8x8).
constexpr size_t H264_NUM_CABAC_CONTEXTS = 1024;

/// Standard (m, n) linear initialization constants for an H.264 CABAC context model (Table 9-12..9-24).
struct H264CabacInitCoeffs {
    int8_t m;
    int8_t n;
};

/// Sovereign H.264 / AVC CABAC Context Table Manager.
/// Implements ISO/IEC 14496-10 Section 9.3.1.1 (Context model initialization).
class H264CabacContextSet {
public:
    H264CabacContextSet() noexcept {
        reset();
    }

    /// Resets all context models to neutral state (pStateIdx=0, valMPS=0).
    void reset() noexcept;

    /// Initializes all context models for a given slice type, cabac_init_idc, and slice QP.
    /// Implements equations 9-12, 9-13, and 9-14 from ITU-T H.264.
    void init_slice(H264SliceType slice_type, uint32_t cabac_init_idc, int32_t slice_qp) noexcept;

    /// Direct access to a context model by index.
    [[nodiscard]] CabacContext& context(size_t ctx_idx) noexcept {
        return contexts_[ctx_idx < H264_NUM_CABAC_CONTEXTS ? ctx_idx : 0];
    }

    [[nodiscard]] const CabacContext& context(size_t ctx_idx) const noexcept {
        return contexts_[ctx_idx < H264_NUM_CABAC_CONTEXTS ? ctx_idx : 0];
    }

    /// Array access operator.
    [[nodiscard]] CabacContext& operator[](size_t ctx_idx) noexcept {
        return context(ctx_idx);
    }

    [[nodiscard]] const CabacContext& operator[](size_t ctx_idx) const noexcept {
        return context(ctx_idx);
    }

    // Context Index Constants (Table 9-11)
    static constexpr size_t CTX_MB_SKIP_FLAG_P = 11;       // 11..13
    static constexpr size_t CTX_MB_SKIP_FLAG_B = 24;       // 24..26
    static constexpr size_t CTX_MB_TYPE_I = 3;             // 3..10
    static constexpr size_t CTX_MB_TYPE_P = 14;            // 14..20
    static constexpr size_t CTX_MB_TYPE_B = 27;            // 27..35
    static constexpr size_t CTX_SUB_MB_TYPE_P = 21;        // 21..23
    static constexpr size_t CTX_SUB_MB_TYPE_B = 36;        // 36..39
    static constexpr size_t CTX_MVD_L0_HORIZ = 40;         // 40..46
    static constexpr size_t CTX_MVD_L0_VERT = 47;          // 47..53
    static constexpr size_t CTX_REF_IDX_L0 = 54;           // 54..59
    static constexpr size_t CTX_MB_QP_DELTA = 60;          // 60..63
    static constexpr size_t CTX_INTRA_CHROMA_PRED = 64;    // 64..67
    static constexpr size_t CTX_PREV_INTRA4X4_FLAG = 68;   // 68
    static constexpr size_t CTX_REM_INTRA4X4_MODE = 69;    // 69
    static constexpr size_t CTX_MB_FIELD_DECODING = 70;    // 70..72
    static constexpr size_t CTX_CBP_LUMA = 73;             // 73..76
    static constexpr size_t CTX_CBP_CHROMA = 77;           // 77..84
    static constexpr size_t CTX_CBF_BASE = 85;             // 85..104 (4x4)
    static constexpr size_t CTX_SIG_COEFF_BASE = 105;      // 105..165 (4x4)
    static constexpr size_t CTX_LAST_SIG_COEFF_BASE = 166; // 166..226 (4x4)
    static constexpr size_t CTX_COEFF_ABS_G1_BASE = 227;   // 227..275 (4x4)
    static constexpr size_t CTX_COEFF_ABS_G2_BASE = 276;   // 276..337
    static constexpr size_t CTX_8X8_CBF_BASE = 460;        // 460..483
    static constexpr size_t CTX_8X8_SIG_COEFF_BASE = 484;  // 484..547

private:
    std::array<CabacContext, H264_NUM_CABAC_CONTEXTS> contexts_{};
};

} // namespace atum

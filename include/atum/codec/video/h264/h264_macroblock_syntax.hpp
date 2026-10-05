#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/entropy/cabac.hpp"
#include "atum/entropy/h264_cabac_contexts.hpp"
#include "atum/codec/video/h264/h264_types.hpp"

namespace atum {

/// Parsed Macroblock Header syntax information.
struct H264MbInfo {
    uint32_t mb_type{0};
    bool is_skip{false};
    bool is_intra{true};
    bool is_intra16x16{false};
    uint8_t intra16x16_pred_mode{0};
    uint8_t intra_chroma_pred_mode{0};
    uint8_t cbp_luma{0};     ///< 4-bit coded block pattern for luma 8x8 blocks
    uint8_t cbp_chroma{0};   ///< 0: none, 1: DC only, 2: DC+AC
    int32_t mb_qp_delta{0};
    int16_t mvd[2][2]{};     ///< [list 0/1][0=dx, 1=dy]
    int8_t ref_idx[2]{0, 0}; ///< [list 0/1]
    uint8_t sub_mb_type[4]{};///< for P8x8 / B8x8 partitions
    bool transform_size_8x8_flag{false};
    uint8_t intra4x4_pred_mode[16]{};

    [[nodiscard]] uint32_t total_cbp() const noexcept {
        return (static_cast<uint32_t>(cbp_chroma) << 4) | (cbp_luma & 0x0F);
    }
};

/// Spatial neighbor context availability and parameters for Macroblock CABAC parsing.
struct H264NeighborMbInfo {
    bool available{false};
    bool is_skip{false};
    bool is_intra{false};
    uint8_t cbp_luma_bit{0};
    uint8_t cbp_chroma{0};
    int16_t mvd[2][2]{};
    int8_t ref_idx[2]{-1, -1};
    uint8_t intra4x4_pred_mode[16]{};
    bool transform_size_8x8_flag{false};
};

/// Sovereign H.264 / AVC CABAC Macroblock Syntax Decoder.
/// Implements ISO/IEC 14496-10 Section 9.3.2 and Section 9.3.3.
class H264MacroblockDecoder {
public:
    H264MacroblockDecoder(CabacDecoder& cabac, H264CabacContextSet& contexts, bool transform_8x8_mode_flag = false) noexcept
        : cabac_(cabac), contexts_(contexts), transform_8x8_mode_flag_(transform_8x8_mode_flag) {}

    /// Decodes mb_skip_flag for P or B slice.
    bool decode_mb_skip_flag(
        H264SliceType slice_type,
        const H264NeighborMbInfo& left,
        const H264NeighborMbInfo& top
    ) noexcept;

    /// Decodes macroblock type (mb_type) for I, P, or B slice.
    uint32_t decode_mb_type(
        H264SliceType slice_type,
        const H264NeighborMbInfo& left,
        const H264NeighborMbInfo& top
    ) noexcept;

    /// Decodes sub_mb_type for P8x8 or B8x8 macroblock.
    uint32_t decode_sub_mb_type(H264SliceType slice_type) noexcept;

    /// Decodes motion vector difference (mvd_l0 or mvd_l1).
    int32_t decode_mvd(
        uint32_t list_idx,
        uint32_t comp_idx, // 0 = horizontal, 1 = vertical
        int16_t mvd_left,
        int16_t mvd_top
    ) noexcept;

    /// Decodes reference frame index (ref_idx_l0 or ref_idx_l1).
    int32_t decode_ref_idx(
        uint32_t num_ref_idx_active,
        int8_t ref_idx_left,
        int8_t ref_idx_top
    ) noexcept;

    /// Decodes intra_chroma_pred_mode (0=DC, 1=Horiz, 2=Vert, 3=Plane).
    uint32_t decode_intra_chroma_pred_mode(
        const H264NeighborMbInfo& left,
        const H264NeighborMbInfo& top
    ) noexcept;

    /// Decodes coded_block_pattern (CBP).
    void decode_cbp(
        H264MbInfo& mb,
        const H264NeighborMbInfo& left,
        const H264NeighborMbInfo& top
    ) noexcept;

    /// Decodes mb_qp_delta.
    int32_t decode_mb_qp_delta(bool prev_mb_has_delta) noexcept;

    /// Decodes a full macroblock header given left and top neighbor information.
    MediaResult<H264MbInfo> decode_macroblock_header(
        H264SliceType slice_type,
        const H264NeighborMbInfo& left,
        const H264NeighborMbInfo& top,
        uint32_t num_ref_idx_l0_active,
        uint32_t num_ref_idx_l1_active
    ) noexcept;

private:
    CabacDecoder& cabac_;
    H264CabacContextSet& contexts_;
    bool transform_8x8_mode_flag_{false};
};

} // namespace atum

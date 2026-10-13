#pragma once

#include <cstdint>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/codec/video/h264/h264_types.hpp"
#include "atum/codec/video/h264/h264_sps.hpp"
#include "atum/codec/video/h264/h264_pps.hpp"

namespace atum {

/// Parsed H.264 Slice Header.
struct H264SliceHeader {
    uint32_t first_mb_in_slice{0};
    uint32_t raw_slice_type{0};
    H264SliceType slice_type{H264SliceType::I};
    uint32_t pic_parameter_set_id{0};
    uint8_t colour_plane_id{0};
    uint32_t frame_num{0};

    bool field_pic_flag{false};
    bool bottom_field_flag{false};
    uint32_t idr_pic_id{0};

    uint32_t pic_order_cnt_lsb{0};
    int32_t delta_pic_order_cnt_bottom{0};
    int32_t delta_pic_order_cnt[2]{0, 0};

    uint32_t redundant_pic_cnt{0};
    bool direct_spatial_mv_pred_flag{false};
    bool num_ref_idx_active_override_flag{false};
    uint32_t num_ref_idx_l0_active_minus1{0};
    uint32_t num_ref_idx_l1_active_minus1{0};

    uint32_t cabac_init_idc{0};
    int32_t slice_qp_delta{0};
    uint32_t header_bit_size{0};

    uint8_t disable_deblocking_filter_idc{0};
    int8_t slice_alpha_c0_offset_div2{0};
    int8_t slice_beta_offset_div2{0};

    uint8_t luma_log2_weight_denom{0};
    uint8_t chroma_log2_weight_denom{0};
    uint8_t luma_weight_l0_flag{0};
    int16_t luma_weight_l0[32]{0};
    int16_t luma_offset_l0[32]{0};
    uint8_t chroma_weight_l0_flag{0};
    int16_t chroma_weight_l0[32][2]{{0}};
    int16_t chroma_offset_l0[32][2]{{0}};
    uint8_t luma_weight_l1_flag{0};
    int16_t luma_weight_l1[32]{0};
    int16_t luma_offset_l1[32]{0};
    uint8_t chroma_weight_l1_flag{0};
    int16_t chroma_weight_l1[32][2]{{0}};
    int16_t chroma_offset_l1[32][2]{{0}};

    [[nodiscard]] bool is_keyframe() const noexcept {
        return slice_type == H264SliceType::I || slice_type == H264SliceType::SI;
    }

    /// Parses slice header given an active SPS and PPS.
    static MediaResult<H264SliceHeader> parse(
        BitReader& reader,
        H264NalType nal_type,
        const H264Sps& sps,
        const H264Pps& pps,
        uint8_t nal_ref_idc = 1) noexcept;
};

} // namespace atum

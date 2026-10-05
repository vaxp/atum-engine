#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"

namespace atum {

/// H.264 Picture Parameter Set (PPS).
struct H264Pps {
    uint32_t pic_parameter_set_id{0};
    uint32_t seq_parameter_set_id{0};
    bool entropy_coding_mode_flag{false}; // false = CAVLC, true = CABAC
    bool bottom_field_pic_order_in_frame_present_flag{false};
    uint32_t num_slice_groups_minus1{0};

    uint32_t num_ref_idx_l0_default_active_minus1{0};
    uint32_t num_ref_idx_l1_default_active_minus1{0};
    bool weighted_pred_flag{false};
    uint8_t weighted_bipred_idc{0};
    int32_t pic_init_qp_minus26{0};
    int32_t pic_init_qs_minus26{0};
    int32_t chroma_qp_index_offset{0};
    bool deblocking_filter_control_present_flag{false};
    bool constrained_intra_pred_flag{false};
    bool redundant_pic_cnt_present_flag{false};

    // Extended PPS fields
    bool transform_8x8_mode_flag{false};
    bool pic_scaling_matrix_present_flag{false};
    int32_t second_chroma_qp_index_offset{0};

    /// Parses a PPS from an RBSP bitstream reader.
    static MediaResult<H264Pps> parse(BitReader& reader) noexcept;

    /// Parses a PPS directly from RBSP byte span.
    static MediaResult<H264Pps> parse_rbsp(std::span<const uint8_t> rbsp_bytes) noexcept;
};

} // namespace atum

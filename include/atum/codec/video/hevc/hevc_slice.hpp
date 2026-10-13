#pragma once

#include <cstdint>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/codec/video/hevc/hevc_types.hpp"
#include "atum/codec/video/hevc/hevc_parameter_sets.hpp"

namespace atum {

/// Parsed HEVC Slice Segment Header.
struct HevcSliceHeader {
    bool first_slice_segment_in_pic_flag{true};
    bool no_output_of_prior_pics_flag{false};
    uint32_t slice_pic_parameter_set_id{0};
    bool dependent_slice_segment_flag{false};
    uint32_t slice_segment_address{0};

    uint32_t raw_slice_type{2};
    HevcSliceType slice_type{HevcSliceType::I};
    bool pic_output_flag{true};
    uint8_t colour_plane_id{0};
    uint32_t slice_pic_order_cnt_lsb{0};
    int32_t slice_qp_delta{0};
    uint32_t data_byte_offset{0};
    HevcNalType nal_unit_type{HevcNalType::TRAIL_R};
    bool slice_sao_luma_flag{true};
    bool slice_sao_chroma_flag{true};
    bool slice_temporal_mvp_enabled_flag{false};
    bool collocated_from_l0_flag{true};
    uint8_t collocated_ref_idx{0xFF};
    uint8_t num_ref_idx_l0_active_minus1{0};
    uint8_t num_ref_idx_l1_active_minus1{0};
    bool mvd_l1_zero_flag{false};
    bool cabac_init_flag{false};
    bool slice_deblocking_filter_disabled_flag{false};
    uint8_t five_minus_max_num_merge_cand{0};
    bool slice_loop_filter_across_slices_enabled_flag{true};
    std::vector<int32_t> rps_curr_before;
    std::vector<int32_t> rps_curr_after;
    std::vector<int32_t> rps_foll;

    [[nodiscard]] bool is_keyframe() const noexcept {
        return slice_type == HevcSliceType::I;
    }

    [[nodiscard]] bool is_irap() const noexcept {
        return hevc_is_irap_nal(nal_unit_type);
    }

    [[nodiscard]] bool is_idr() const noexcept {
        return nal_unit_type == HevcNalType::IDR_W_RADL || nal_unit_type == HevcNalType::IDR_N_LP;
    }

    [[nodiscard]] bool is_reference() const noexcept {
        auto val = static_cast<uint8_t>(nal_unit_type);
        if (val > 31) return false;
        if (val >= 16) return true;
        return (val % 2) != 0;
    }

    static MediaResult<HevcSliceHeader> parse(
        BitReader& reader,
        HevcNalType nal_type,
        const HevcSps& sps,
        const HevcPps& pps,
        std::span<const uint8_t> raw_nal = {}) noexcept;
};

} // namespace atum

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

    [[nodiscard]] bool is_keyframe() const noexcept {
        return slice_type == HevcSliceType::I;
    }

    static MediaResult<HevcSliceHeader> parse(
        BitReader& reader,
        HevcNalType nal_type,
        const HevcSps& sps,
        const HevcPps& pps) noexcept;
};

} // namespace atum

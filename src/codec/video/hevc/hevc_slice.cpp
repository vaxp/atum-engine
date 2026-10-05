#include "atum/codec/video/hevc/hevc_slice.hpp"

namespace atum {

MediaResult<HevcSliceHeader> HevcSliceHeader::parse(
    BitReader& reader,
    HevcNalType nal_type,
    const HevcSps& sps,
    const HevcPps& pps) noexcept {

    HevcSliceHeader slice{};

    auto first_flag = reader.read_bit(); if (!first_flag) return first_flag.error();
    slice.first_slice_segment_in_pic_flag = (*first_flag != 0);

    if (hevc_is_irap_nal(nal_type)) {
        auto no_out = reader.read_bit(); if (!no_out) return no_out.error();
        slice.no_output_of_prior_pics_flag = (*no_out != 0);
    }

    auto pps_id = reader.read_ue(); if (!pps_id) return pps_id.error();
    slice.slice_pic_parameter_set_id = *pps_id;

    if (!slice.first_slice_segment_in_pic_flag) {
        if (pps.dependent_slice_segments_enabled_flag) {
            auto dep = reader.read_bit(); if (!dep) return dep.error();
            slice.dependent_slice_segment_flag = (*dep != 0);
        }
        // CTB size calculation
        uint32_t min_cb_size = 1u << (sps.log2_min_luma_coding_block_size_minus3 + 3);
        uint32_t ctb_size = min_cb_size << sps.log2_diff_max_min_luma_coding_block_size;
        uint32_t pic_width_in_ctbs = (sps.pic_width_in_luma_samples + ctb_size - 1) / ctb_size;
        uint32_t pic_height_in_ctbs = (sps.pic_height_in_luma_samples + ctb_size - 1) / ctb_size;
        uint32_t pic_size_in_ctbs = pic_width_in_ctbs * pic_height_in_ctbs;

        uint32_t bits = 1;
        while ((1u << bits) < pic_size_in_ctbs) bits++;
        auto addr = reader.read_bits(bits); if (!addr) return addr.error();
        slice.slice_segment_address = *addr;
    }

    if (!slice.dependent_slice_segment_flag) {
        if (pps.num_extra_slice_header_bits > 0) {
            auto sk = reader.skip_bits(pps.num_extra_slice_header_bits);
            if (!sk) return sk.error();
        }

        auto st = reader.read_ue(); if (!st) return st.error();
        slice.raw_slice_type = *st;
        slice.slice_type = static_cast<HevcSliceType>(slice.raw_slice_type % 3);

        if (pps.output_flag_present_flag) {
            auto pic_out = reader.read_bit(); if (!pic_out) return pic_out.error();
            slice.pic_output_flag = (*pic_out != 0);
        }

        if (sps.separate_colour_plane_flag) {
            auto plane = reader.read_bits(2); if (!plane) return plane.error();
            slice.colour_plane_id = static_cast<uint8_t>(*plane);
        }

        if (nal_type != HevcNalType::IDR_W_RADL && nal_type != HevcNalType::IDR_N_LP) {
            size_t poc_bits = sps.log2_max_pic_order_cnt_lsb_minus4 + 4;
            auto poc = reader.read_bits(poc_bits); if (!poc) return poc.error();
            slice.slice_pic_order_cnt_lsb = *poc;
        }

        // QP delta
        if (reader.has_bits(1)) {
            auto qp = reader.read_se();
            if (qp) slice.slice_qp_delta = *qp;
        }
    }

    return slice;
}

} // namespace atum

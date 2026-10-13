#include "atum/hwaccel/vaapi_param_builder.hpp"
#include <cstring>
#include <algorithm>

namespace atum {

#if defined(ATUM_HAS_VAAPI_HEADERS)

VaapiH264DecodeParams VaapiH264DecodeParams::build(
    const H264Sps& sps,
    const H264Pps& pps,
    const H264SliceHeader* slice,
    uint32_t curr_surface,
    uint32_t frame_num,
    int32_t poc,
    const DecodedPictureBuffer& dpb,
    size_t slice_data_size,
    bool is_reference_pic
) {
    VaapiH264DecodeParams params{};

    // Picture width and height in macroblocks directly from SPS
    params.pic_param.picture_width_in_mbs_minus1 = static_cast<uint16_t>(sps.pic_width_in_mbs_minus1);
    params.pic_param.picture_height_in_mbs_minus1 = static_cast<uint16_t>(
        sps.frame_mbs_only_flag ? sps.pic_height_in_map_units_minus1 : (2 * sps.pic_height_in_map_units_minus1 + 1)
    );
    params.pic_param.bit_depth_luma_minus8 = static_cast<uint8_t>(sps.bit_depth_luma_minus8);
    params.pic_param.bit_depth_chroma_minus8 = static_cast<uint8_t>(sps.bit_depth_chroma_minus8);
    params.pic_param.num_ref_frames = static_cast<uint8_t>(sps.max_num_ref_frames);

    params.pic_param.seq_fields.bits.chroma_format_idc = sps.chroma_format_idc;
    params.pic_param.seq_fields.bits.residual_colour_transform_flag = sps.separate_colour_plane_flag;
    params.pic_param.seq_fields.bits.gaps_in_frame_num_value_allowed_flag = sps.gaps_in_frame_num_value_allowed_flag ? 1 : 0;
    params.pic_param.seq_fields.bits.frame_mbs_only_flag = sps.frame_mbs_only_flag ? 1 : 0;
    params.pic_param.seq_fields.bits.mb_adaptive_frame_field_flag = sps.mb_adaptive_frame_field_flag ? 1 : 0;
    params.pic_param.seq_fields.bits.direct_8x8_inference_flag = sps.direct_8x8_inference_flag ? 1 : 0;
    params.pic_param.seq_fields.bits.MinLumaBiPredSize8x8 = (sps.level_idc >= 31) ? 1 : 0;
    params.pic_param.seq_fields.bits.log2_max_frame_num_minus4 = sps.log2_max_frame_num_minus4;
    params.pic_param.seq_fields.bits.pic_order_cnt_type = sps.pic_order_cnt_type;
    params.pic_param.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 = sps.log2_max_pic_order_cnt_lsb_minus4;
    params.pic_param.seq_fields.bits.delta_pic_order_always_zero_flag = sps.delta_pic_order_always_zero_flag ? 1 : 0;

    params.pic_param.pic_init_qp_minus26 = static_cast<int8_t>(pps.pic_init_qp_minus26);
    params.pic_param.pic_init_qs_minus26 = static_cast<int8_t>(pps.pic_init_qs_minus26);
    params.pic_param.chroma_qp_index_offset = static_cast<int8_t>(pps.chroma_qp_index_offset);
    params.pic_param.second_chroma_qp_index_offset = static_cast<int8_t>(pps.second_chroma_qp_index_offset);

    params.pic_param.pic_fields.bits.entropy_coding_mode_flag = pps.entropy_coding_mode_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.weighted_pred_flag = pps.weighted_pred_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.weighted_bipred_idc = pps.weighted_bipred_idc;
    params.pic_param.pic_fields.bits.transform_8x8_mode_flag = pps.transform_8x8_mode_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.field_pic_flag = 0;
    params.pic_param.pic_fields.bits.constrained_intra_pred_flag = pps.constrained_intra_pred_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.pic_order_present_flag = pps.bottom_field_pic_order_in_frame_present_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.deblocking_filter_control_present_flag = pps.deblocking_filter_control_present_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.redundant_pic_cnt_present_flag = pps.redundant_pic_cnt_present_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.reference_pic_flag = is_reference_pic ? 1 : 0;
    params.pic_param.frame_num = static_cast<uint16_t>(frame_num);

    params.pic_param.CurrPic.picture_id = static_cast<VASurfaceID>(curr_surface);
    params.pic_param.CurrPic.frame_idx = frame_num;
    params.pic_param.CurrPic.flags = is_reference_pic ? VA_PICTURE_H264_SHORT_TERM_REFERENCE : 0;
    params.pic_param.CurrPic.TopFieldOrderCnt = poc;
    params.pic_param.CurrPic.BottomFieldOrderCnt = poc;

    for (size_t i = 0; i < 16; ++i) {
        params.pic_param.ReferenceFrames[i].picture_id = VA_INVALID_ID;
        params.pic_param.ReferenceFrames[i].flags = VA_PICTURE_H264_INVALID;
        params.pic_param.ReferenceFrames[i].frame_idx = 0;
        params.pic_param.ReferenceFrames[i].TopFieldOrderCnt = 0;
        params.pic_param.ReferenceFrames[i].BottomFieldOrderCnt = 0;
    }

    size_t ref_count = 0;
    for (const auto& entry : dpb.entries()) {
        if (ref_count >= 16) break;
        if (entry.frame.is_hw_frame() && entry.frame.hw_surface_id() != 0xFFFFFFFF && entry.frame.hw_surface_id() != curr_surface) {
            params.pic_param.ReferenceFrames[ref_count].picture_id = static_cast<VASurfaceID>(entry.frame.hw_surface_id());
            params.pic_param.ReferenceFrames[ref_count].frame_idx = entry.frame_num;
            params.pic_param.ReferenceFrames[ref_count].flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
            params.pic_param.ReferenceFrames[ref_count].TopFieldOrderCnt = entry.poc;
            params.pic_param.ReferenceFrames[ref_count].BottomFieldOrderCnt = entry.poc;
            ref_count++;
        }
    }

    // Initialize flat scaling lists (standard default 16)
    std::memset(params.iq_matrix.ScalingList4x4, 16, sizeof(params.iq_matrix.ScalingList4x4));
    std::memset(params.iq_matrix.ScalingList8x8, 16, sizeof(params.iq_matrix.ScalingList8x8));

    if (slice) {
        params.has_slice = true;
        params.slice_param.slice_data_size = static_cast<uint32_t>(slice_data_size);
        params.slice_param.slice_data_offset = 0;
        params.slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
        params.slice_param.slice_data_bit_offset = static_cast<uint16_t>(8 + slice->header_bit_size);
        params.slice_param.first_mb_in_slice = static_cast<uint16_t>(slice->first_mb_in_slice);
        params.slice_param.slice_type = static_cast<uint8_t>(slice->raw_slice_type % 5);
        params.slice_param.direct_spatial_mv_pred_flag = slice->direct_spatial_mv_pred_flag ? 1 : 0;
        params.slice_param.cabac_init_idc = static_cast<uint8_t>(slice->cabac_init_idc);
        params.slice_param.slice_qp_delta = static_cast<int8_t>(slice->slice_qp_delta);
        params.slice_param.disable_deblocking_filter_idc = slice->disable_deblocking_filter_idc;
        params.slice_param.slice_alpha_c0_offset_div2 = slice->slice_alpha_c0_offset_div2;
        params.slice_param.slice_beta_offset_div2 = slice->slice_beta_offset_div2;

        params.slice_param.luma_log2_weight_denom = slice->luma_log2_weight_denom;
        params.slice_param.chroma_log2_weight_denom = slice->chroma_log2_weight_denom;
        params.slice_param.luma_weight_l0_flag = slice->luma_weight_l0_flag;
        params.slice_param.chroma_weight_l0_flag = slice->chroma_weight_l0_flag;
        std::memcpy(params.slice_param.luma_weight_l0, slice->luma_weight_l0, sizeof(params.slice_param.luma_weight_l0));
        std::memcpy(params.slice_param.luma_offset_l0, slice->luma_offset_l0, sizeof(params.slice_param.luma_offset_l0));
        std::memcpy(params.slice_param.chroma_weight_l0, slice->chroma_weight_l0, sizeof(params.slice_param.chroma_weight_l0));
        std::memcpy(params.slice_param.chroma_offset_l0, slice->chroma_offset_l0, sizeof(params.slice_param.chroma_offset_l0));

        params.slice_param.luma_weight_l1_flag = slice->luma_weight_l1_flag;
        params.slice_param.chroma_weight_l1_flag = slice->chroma_weight_l1_flag;
        std::memcpy(params.slice_param.luma_weight_l1, slice->luma_weight_l1, sizeof(params.slice_param.luma_weight_l1));
        std::memcpy(params.slice_param.luma_offset_l1, slice->luma_offset_l1, sizeof(params.slice_param.luma_offset_l1));
        std::memcpy(params.slice_param.chroma_weight_l1, slice->chroma_weight_l1, sizeof(params.slice_param.chroma_weight_l1));
        std::memcpy(params.slice_param.chroma_offset_l1, slice->chroma_offset_l1, sizeof(params.slice_param.chroma_offset_l1));

        // Build RefPicList0 and RefPicList1 according to H.264 POC order
        std::vector<VAPictureH264> past_refs;
        std::vector<VAPictureH264> future_refs;

        for (size_t i = 0; i < ref_count; ++i) {
            const auto& pic = params.pic_param.ReferenceFrames[i];
            if (pic.picture_id != VA_INVALID_ID) {
                if (pic.TopFieldOrderCnt < poc) {
                    past_refs.push_back(pic);
                } else {
                    future_refs.push_back(pic);
                }
            }
        }

        // Sort past_refs: descending POC (closest past first)
        std::sort(past_refs.begin(), past_refs.end(), [](const VAPictureH264& a, const VAPictureH264& b) {
            return a.TopFieldOrderCnt > b.TopFieldOrderCnt;
        });

        // Sort future_refs: ascending POC (closest future first)
        std::sort(future_refs.begin(), future_refs.end(), [](const VAPictureH264& a, const VAPictureH264& b) {
            return a.TopFieldOrderCnt < b.TopFieldOrderCnt;
        });

        // RefPicList0: past_refs followed by future_refs
        std::vector<VAPictureH264> list0 = past_refs;
        list0.insert(list0.end(), future_refs.begin(), future_refs.end());

        // RefPicList1: future_refs followed by past_refs (B slices only)
        std::vector<VAPictureH264> list1;
        if (slice->slice_type == H264SliceType::B) {
            list1 = future_refs;
            list1.insert(list1.end(), past_refs.begin(), past_refs.end());
        }

        for (size_t i = 0; i < 32; ++i) {
            params.slice_param.RefPicList0[i].picture_id = VA_INVALID_ID;
            params.slice_param.RefPicList0[i].flags = VA_PICTURE_H264_INVALID;
            params.slice_param.RefPicList0[i].frame_idx = 0;
            params.slice_param.RefPicList0[i].TopFieldOrderCnt = 0;
            params.slice_param.RefPicList0[i].BottomFieldOrderCnt = 0;

            params.slice_param.RefPicList1[i].picture_id = VA_INVALID_ID;
            params.slice_param.RefPicList1[i].flags = VA_PICTURE_H264_INVALID;
            params.slice_param.RefPicList1[i].frame_idx = 0;
            params.slice_param.RefPicList1[i].TopFieldOrderCnt = 0;
            params.slice_param.RefPicList1[i].BottomFieldOrderCnt = 0;
        }

        for (size_t i = 0; i < list0.size() && i < 32; ++i) {
            params.slice_param.RefPicList0[i] = list0[i];
        }

        if (slice->num_ref_idx_active_override_flag) {
            params.slice_param.num_ref_idx_l0_active_minus1 = static_cast<uint8_t>(slice->num_ref_idx_l0_active_minus1);
        } else {
            params.slice_param.num_ref_idx_l0_active_minus1 = list0.empty() ? 0 : static_cast<uint8_t>(
                std::min(static_cast<uint32_t>(list0.size() - 1), pps.num_ref_idx_l0_default_active_minus1)
            );
        }

        if (slice->slice_type == H264SliceType::B) {
            for (size_t i = 0; i < list1.size() && i < 32; ++i) {
                params.slice_param.RefPicList1[i] = list1[i];
            }
            if (slice->num_ref_idx_active_override_flag) {
                params.slice_param.num_ref_idx_l1_active_minus1 = static_cast<uint8_t>(slice->num_ref_idx_l1_active_minus1);
            } else {
                params.slice_param.num_ref_idx_l1_active_minus1 = list1.empty() ? 0 : static_cast<uint8_t>(
                    std::min(static_cast<uint32_t>(list1.size() - 1), pps.num_ref_idx_l1_default_active_minus1)
                );
            }
        } else {
            params.slice_param.num_ref_idx_l1_active_minus1 = 0;
        }
    }

    return params;
}

VaapiHevcDecodeParams VaapiHevcDecodeParams::build(
    const HevcSps& sps,
    const HevcPps& pps,
    const HevcSliceHeader* slice,
    uint32_t curr_surface,
    int32_t poc,
    const DecodedPictureBuffer& dpb,
    size_t slice_data_size
) {
    VaapiHevcDecodeParams params{};

    params.pic_param.pic_width_in_luma_samples = static_cast<uint16_t>(sps.raw_width());
    params.pic_param.pic_height_in_luma_samples = static_cast<uint16_t>(sps.raw_height());
    params.pic_param.bit_depth_luma_minus8 = static_cast<uint8_t>(sps.bit_depth_luma_minus8);
    params.pic_param.bit_depth_chroma_minus8 = static_cast<uint8_t>(sps.bit_depth_chroma_minus8);
    params.pic_param.log2_max_pic_order_cnt_lsb_minus4 = static_cast<uint8_t>(sps.log2_max_pic_order_cnt_lsb_minus4);
    params.pic_param.log2_min_luma_coding_block_size_minus3 = static_cast<uint8_t>(sps.log2_min_luma_coding_block_size_minus3);
    params.pic_param.log2_diff_max_min_luma_coding_block_size = static_cast<uint8_t>(sps.log2_diff_max_min_luma_coding_block_size);

    params.pic_param.pic_fields.bits.chroma_format_idc = sps.chroma_format_idc;
    params.pic_param.pic_fields.bits.separate_colour_plane_flag = sps.separate_colour_plane_flag;
    params.pic_param.pic_fields.bits.constrained_intra_pred_flag = pps.constrained_intra_pred_flag;
    params.pic_param.pic_fields.bits.weighted_pred_flag = pps.weighted_pred_flag;
    params.pic_param.pic_fields.bits.weighted_bipred_flag = pps.weighted_bipred_flag;
    params.pic_param.pic_fields.bits.transquant_bypass_enabled_flag = pps.transquant_bypass_enabled_flag;
    params.pic_param.pic_fields.bits.tiles_enabled_flag = pps.tiles_enabled_flag;
    params.pic_param.pic_fields.bits.entropy_coding_sync_enabled_flag = pps.entropy_coding_sync_enabled_flag;
    params.pic_param.pic_fields.bits.loop_filter_across_tiles_enabled_flag = 0;

    params.pic_param.CurrPic.picture_id = static_cast<VASurfaceID>(curr_surface);
    params.pic_param.CurrPic.pic_order_cnt = poc;
    params.pic_param.CurrPic.flags = 0;

    for (size_t i = 0; i < 15; ++i) {
        params.pic_param.ReferenceFrames[i].picture_id = VA_INVALID_ID;
        params.pic_param.ReferenceFrames[i].pic_order_cnt = 0;
        params.pic_param.ReferenceFrames[i].flags = VA_PICTURE_HEVC_INVALID;
    }

    size_t ref_count = 0;
    for (const auto& entry : dpb.entries()) {
        if (ref_count >= 15) break;
        if (entry.frame.is_hw_frame() && entry.frame.hw_surface_id() != 0 && entry.frame.hw_surface_id() != curr_surface) {
            params.pic_param.ReferenceFrames[ref_count].picture_id = static_cast<VASurfaceID>(entry.frame.hw_surface_id());
            params.pic_param.ReferenceFrames[ref_count].pic_order_cnt = entry.poc;
            params.pic_param.ReferenceFrames[ref_count].flags = 0;
            ref_count++;
        }
    }

    if (slice) {
        params.has_slice = true;
        params.slice_param.slice_data_size = static_cast<uint32_t>(slice_data_size);
        params.slice_param.slice_data_offset = 0;
        params.slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
        params.slice_param.slice_segment_address = slice->slice_segment_address;
        params.slice_param.LongSliceFlags.fields.slice_type = static_cast<uint32_t>(slice->raw_slice_type % 3);
        params.slice_param.LongSliceFlags.fields.color_plane_id = slice->colour_plane_id;
        params.slice_param.LongSliceFlags.fields.dependent_slice_segment_flag = slice->dependent_slice_segment_flag ? 1 : 0;
    }

    return params;
}

VaapiVp9DecodeParams VaapiVp9DecodeParams::build(
    const Vp9UncompressedHeader& hdr,
    uint32_t curr_surface
) {
    VaapiVp9DecodeParams params{};
    (void)curr_surface;

    params.pic_param.frame_width = static_cast<uint16_t>(hdr.width);
    params.pic_param.frame_height = static_cast<uint16_t>(hdr.height);

    params.pic_param.pic_fields.bits.subsampling_x = hdr.subsampling_x;
    params.pic_param.pic_fields.bits.subsampling_y = hdr.subsampling_y;
    params.pic_param.pic_fields.bits.frame_type = (hdr.frame_type == Vp9FrameType::KeyFrame) ? 0 : 1;
    params.pic_param.pic_fields.bits.show_frame = hdr.show_frame ? 1 : 0;
    params.pic_param.pic_fields.bits.error_resilient_mode = hdr.error_resilient_mode ? 1 : 0;
    params.pic_param.pic_fields.bits.intra_only = hdr.intra_only ? 1 : 0;

    params.pic_param.filter_level = hdr.filter_level;
    params.pic_param.sharpness_level = hdr.sharpness_level;
    params.pic_param.profile = static_cast<uint8_t>(hdr.profile);
    params.pic_param.bit_depth = hdr.bit_depth;

    return params;
}

VaapiAv1DecodeParams VaapiAv1DecodeParams::build(
    const Av1SequenceHeader& seq,
    const Av1FrameHeader* frame,
    uint32_t curr_surface
) {
    VaapiAv1DecodeParams params{};
    (void)curr_surface;

    uint32_t w = (frame && frame->frame_width > 0) ? frame->frame_width : seq.max_frame_width;
    uint32_t h = (frame && frame->frame_height > 0) ? frame->frame_height : seq.max_frame_height;

    params.pic_param.frame_width_minus1 = static_cast<uint16_t>(w > 0 ? (w - 1) : 0);
    params.pic_param.frame_height_minus1 = static_cast<uint16_t>(h > 0 ? (h - 1) : 0);

    if (frame) {
        params.pic_param.pic_info_fields.bits.frame_type = static_cast<uint32_t>(frame->frame_type);
        params.pic_param.pic_info_fields.bits.show_frame = frame->show_frame ? 1 : 0;
        params.pic_param.pic_info_fields.bits.error_resilient_mode = frame->error_resilient_mode ? 1 : 0;
    }

    params.pic_param.superres_scale_denominator = 8;
    params.pic_param.base_qindex = 0;
    params.pic_param.order_hint = 0;

    return params;
}

#endif

} // namespace atum

#include "atum/hwaccel/vaapi_param_builder.hpp"
#include <cstring>
#include <algorithm>
#include <unordered_map>

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

    params.pic_param.log2_min_transform_block_size_minus2 = static_cast<uint8_t>(sps.log2_min_transform_block_size_minus2);
    params.pic_param.log2_diff_max_min_transform_block_size = static_cast<uint8_t>(sps.log2_diff_max_min_transform_block_size);
    params.pic_param.max_transform_hierarchy_depth_inter = static_cast<uint8_t>(sps.max_transform_hierarchy_depth_inter);
    params.pic_param.max_transform_hierarchy_depth_intra = static_cast<uint8_t>(sps.max_transform_hierarchy_depth_intra);
    params.pic_param.sps_max_dec_pic_buffering_minus1 = static_cast<uint8_t>(sps.sps_max_dec_pic_buffering_minus1);
    params.pic_param.diff_cu_qp_delta_depth = static_cast<uint8_t>(pps.diff_cu_qp_delta_depth);

    params.pic_param.num_short_term_ref_pic_sets = static_cast<uint8_t>(sps.num_short_term_ref_pic_sets);
    params.pic_param.num_long_term_ref_pic_sps = static_cast<uint8_t>(sps.num_long_term_ref_pics_sps);
    params.pic_param.num_ref_idx_l0_default_active_minus1 = static_cast<uint8_t>(pps.num_ref_idx_l0_default_active_minus1);
    params.pic_param.num_ref_idx_l1_default_active_minus1 = static_cast<uint8_t>(pps.num_ref_idx_l1_default_active_minus1);
    params.pic_param.init_qp_minus26 = static_cast<int8_t>(pps.init_qp_minus26);
    params.pic_param.pps_cb_qp_offset = static_cast<int8_t>(pps.pps_cb_qp_offset);
    params.pic_param.pps_cr_qp_offset = static_cast<int8_t>(pps.pps_cr_qp_offset);
    params.pic_param.pps_beta_offset_div2 = static_cast<int8_t>(pps.pps_beta_offset_div2);
    params.pic_param.pps_tc_offset_div2 = static_cast<int8_t>(pps.pps_tc_offset_div2);
    params.pic_param.num_extra_slice_header_bits = pps.num_extra_slice_header_bits;
    params.pic_param.log2_parallel_merge_level_minus2 = static_cast<uint8_t>(pps.log2_parallel_merge_level_minus2);

    params.pic_param.pic_fields.bits.chroma_format_idc = sps.chroma_format_idc;
    params.pic_param.pic_fields.bits.separate_colour_plane_flag = sps.separate_colour_plane_flag;
    params.pic_param.pic_fields.bits.pcm_enabled_flag = sps.pcm_enabled_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.scaling_list_enabled_flag = sps.scaling_list_enabled_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.transform_skip_enabled_flag = pps.transform_skip_enabled_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.amp_enabled_flag = sps.amp_enabled_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.strong_intra_smoothing_enabled_flag = sps.strong_intra_smoothing_enabled_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.sign_data_hiding_enabled_flag = pps.sign_data_hiding_enabled_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.constrained_intra_pred_flag = pps.constrained_intra_pred_flag;
    params.pic_param.pic_fields.bits.cu_qp_delta_enabled_flag = pps.cu_qp_delta_enabled_flag ? 1 : 0;
    params.pic_param.pic_fields.bits.weighted_pred_flag = pps.weighted_pred_flag;
    params.pic_param.pic_fields.bits.weighted_bipred_flag = pps.weighted_bipred_flag;
    params.pic_param.pic_fields.bits.transquant_bypass_enabled_flag = pps.transquant_bypass_enabled_flag;
    params.pic_param.pic_fields.bits.tiles_enabled_flag = pps.tiles_enabled_flag;
    params.pic_param.pic_fields.bits.entropy_coding_sync_enabled_flag = pps.entropy_coding_sync_enabled_flag;
    params.pic_param.pic_fields.bits.pps_loop_filter_across_slices_enabled_flag = pps.pps_loop_filter_across_slices_enabled_flag;
    params.pic_param.pic_fields.bits.loop_filter_across_tiles_enabled_flag = pps.loop_filter_across_tiles_enabled_flag;
    params.pic_param.pic_fields.bits.pcm_loop_filter_disabled_flag = 0;

    params.pic_param.pcm_sample_bit_depth_luma_minus1 = 255;
    params.pic_param.pcm_sample_bit_depth_chroma_minus1 = 255;
    params.pic_param.log2_min_pcm_luma_coding_block_size_minus3 = 253;

    params.pic_param.slice_parsing_fields.bits.lists_modification_present_flag = pps.lists_modification_present_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.long_term_ref_pics_present_flag = sps.long_term_ref_pics_present_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.sps_temporal_mvp_enabled_flag = sps.sps_temporal_mvp_enabled_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.cabac_init_present_flag = pps.cabac_init_present_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.output_flag_present_flag = pps.output_flag_present_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.dependent_slice_segments_enabled_flag = pps.dependent_slice_segments_enabled_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.pps_slice_chroma_qp_offsets_present_flag = pps.pps_slice_chroma_qp_offsets_present_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.sample_adaptive_offset_enabled_flag = sps.sample_adaptive_offset_enabled_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.deblocking_filter_override_enabled_flag = pps.deblocking_filter_override_enabled_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.pps_disable_deblocking_filter_flag = pps.pps_deblocking_filter_disabled_flag ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.slice_segment_header_extension_present_flag = pps.slice_segment_header_extension_present_flag ? 1 : 0;

    bool is_irap = slice && slice->is_irap();
    bool is_idr = slice && slice->is_idr();
    params.pic_param.slice_parsing_fields.bits.RapPicFlag = is_irap ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.IdrPicFlag = is_idr ? 1 : 0;
    params.pic_param.slice_parsing_fields.bits.IntraPicFlag = (is_irap || (slice && slice->is_keyframe())) ? 1 : 0;

    params.pic_param.CurrPic.picture_id = static_cast<VASurfaceID>(curr_surface);
    params.pic_param.CurrPic.pic_order_cnt = poc;
    params.pic_param.CurrPic.flags = 0;

    for (size_t i = 0; i < 15; ++i) {
        params.pic_param.ReferenceFrames[i].picture_id = VA_INVALID_ID;
        params.pic_param.ReferenceFrames[i].pic_order_cnt = 0;
        params.pic_param.ReferenceFrames[i].flags = VA_PICTURE_HEVC_INVALID;
    }

    size_t ref_count = 0;
    std::unordered_map<int32_t, uint8_t> poc_to_ref_idx;

    if (slice && !slice->is_idr()) {
        // 1. CurrBefore
        for (int32_t ref_poc : slice->rps_curr_before) {
            if (ref_count >= 15) break;
            const auto* f = dpb.find_by_poc(ref_poc);
            if (f && f->is_hw_frame() && f->hw_surface_id() != 0xFFFFFFFF) {
                params.pic_param.ReferenceFrames[ref_count].picture_id = static_cast<VASurfaceID>(f->hw_surface_id());
                params.pic_param.ReferenceFrames[ref_count].pic_order_cnt = ref_poc;
                params.pic_param.ReferenceFrames[ref_count].flags = 0x10; // VA_PICTURE_HEVC_RPS_ST_CURR_BEFORE
                poc_to_ref_idx[ref_poc] = static_cast<uint8_t>(ref_count);
                ref_count++;
            }
        }
        // 2. CurrAfter
        for (int32_t ref_poc : slice->rps_curr_after) {
            if (ref_count >= 15) break;
            const auto* f = dpb.find_by_poc(ref_poc);
            if (f && f->is_hw_frame() && f->hw_surface_id() != 0xFFFFFFFF) {
                params.pic_param.ReferenceFrames[ref_count].picture_id = static_cast<VASurfaceID>(f->hw_surface_id());
                params.pic_param.ReferenceFrames[ref_count].pic_order_cnt = ref_poc;
                params.pic_param.ReferenceFrames[ref_count].flags = 0x20; // VA_PICTURE_HEVC_RPS_ST_CURR_AFTER
                poc_to_ref_idx[ref_poc] = static_cast<uint8_t>(ref_count);
                ref_count++;
            }
        }
        // 3. Foll
        for (int32_t ref_poc : slice->rps_foll) {
            if (ref_count >= 15) break;
            const auto* f = dpb.find_by_poc(ref_poc);
            if (f && f->is_hw_frame() && f->hw_surface_id() != 0xFFFFFFFF) {
                params.pic_param.ReferenceFrames[ref_count].picture_id = static_cast<VASurfaceID>(f->hw_surface_id());
                params.pic_param.ReferenceFrames[ref_count].pic_order_cnt = ref_poc;
                params.pic_param.ReferenceFrames[ref_count].flags = 0; // Follower
                poc_to_ref_idx[ref_poc] = static_cast<uint8_t>(ref_count);
                ref_count++;
            }
        }
    }

    // Fallback if RPS wasn't available
    if (ref_count == 0 && slice && !slice->is_idr()) {
        for (const auto& entry : dpb.entries()) {
            if (ref_count >= 15) break;
            if (entry.frame.is_hw_frame() && entry.frame.hw_surface_id() != 0xFFFFFFFF && entry.frame.hw_surface_id() != curr_surface) {
                params.pic_param.ReferenceFrames[ref_count].picture_id = static_cast<VASurfaceID>(entry.frame.hw_surface_id());
                params.pic_param.ReferenceFrames[ref_count].pic_order_cnt = entry.poc;
                params.pic_param.ReferenceFrames[ref_count].flags = (entry.poc < poc) ? 0x10 : 0x20;
                poc_to_ref_idx[entry.poc] = static_cast<uint8_t>(ref_count);
                ref_count++;
            }
        }
    }

    if (slice) {
        params.has_slice = true;
        params.slice_param.slice_data_size = static_cast<uint32_t>(slice_data_size);
        params.slice_param.slice_data_offset = 0;
        params.slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
        params.slice_param.slice_data_byte_offset = slice->data_byte_offset;
        params.slice_param.slice_segment_address = slice->slice_segment_address;
        params.slice_param.slice_qp_delta = slice->slice_qp_delta;
        params.slice_param.slice_cb_qp_offset = static_cast<int8_t>(pps.pps_cb_qp_offset);
        params.slice_param.slice_cr_qp_offset = static_cast<int8_t>(pps.pps_cr_qp_offset);
        params.slice_param.slice_beta_offset_div2 = static_cast<int8_t>(pps.pps_beta_offset_div2);
        params.slice_param.slice_tc_offset_div2 = static_cast<int8_t>(pps.pps_tc_offset_div2);
        params.slice_param.collocated_ref_idx = slice->collocated_ref_idx;
        params.slice_param.five_minus_max_num_merge_cand = (slice->slice_type == HevcSliceType::I) ? 0 : slice->five_minus_max_num_merge_cand;

        params.slice_param.LongSliceFlags.fields.LastSliceOfPic = 1;
        params.slice_param.LongSliceFlags.fields.dependent_slice_segment_flag = slice->dependent_slice_segment_flag ? 1 : 0;
        params.slice_param.LongSliceFlags.fields.slice_type = static_cast<uint32_t>(slice->raw_slice_type % 3);
        params.slice_param.LongSliceFlags.fields.color_plane_id = slice->colour_plane_id;
        params.slice_param.LongSliceFlags.fields.slice_sao_luma_flag = slice->slice_sao_luma_flag ? 1 : 0;
        params.slice_param.LongSliceFlags.fields.slice_sao_chroma_flag = slice->slice_sao_chroma_flag ? 1 : 0;
        params.slice_param.LongSliceFlags.fields.mvd_l1_zero_flag = slice->mvd_l1_zero_flag ? 1 : 0;
        params.slice_param.LongSliceFlags.fields.cabac_init_flag = slice->cabac_init_flag ? 1 : 0;
        params.slice_param.LongSliceFlags.fields.collocated_from_l0_flag = slice->collocated_from_l0_flag ? 1 : 0;
        params.slice_param.LongSliceFlags.fields.slice_temporal_mvp_enabled_flag = slice->slice_temporal_mvp_enabled_flag ? 1 : 0;
        params.slice_param.LongSliceFlags.fields.slice_deblocking_filter_disabled_flag = slice->slice_deblocking_filter_disabled_flag ? 1 : 0;
        params.slice_param.LongSliceFlags.fields.slice_loop_filter_across_slices_enabled_flag = slice->slice_loop_filter_across_slices_enabled_flag ? 1 : 0;

        std::memset(params.slice_param.RefPicList, 0xFF, sizeof(params.slice_param.RefPicList));
        size_t l0_idx = 0;
        for (int32_t ref_poc : slice->rps_curr_before) {
            auto it = poc_to_ref_idx.find(ref_poc);
            if (it != poc_to_ref_idx.end() && l0_idx < 15) {
                params.slice_param.RefPicList[0][l0_idx++] = it->second;
            }
        }
        for (int32_t ref_poc : slice->rps_curr_after) {
            auto it = poc_to_ref_idx.find(ref_poc);
            if (it != poc_to_ref_idx.end() && l0_idx < 15) {
                params.slice_param.RefPicList[0][l0_idx++] = it->second;
            }
        }

        if (slice->slice_type == HevcSliceType::B) {
            size_t l1_idx = 0;
            for (int32_t ref_poc : slice->rps_curr_after) {
                auto it = poc_to_ref_idx.find(ref_poc);
                if (it != poc_to_ref_idx.end() && l1_idx < 15) {
                    params.slice_param.RefPicList[1][l1_idx++] = it->second;
                }
            }
            for (int32_t ref_poc : slice->rps_curr_before) {
                auto it = poc_to_ref_idx.find(ref_poc);
                if (it != poc_to_ref_idx.end() && l1_idx < 15) {
                    params.slice_param.RefPicList[1][l1_idx++] = it->second;
                }
            }
        }

        // Fallback if RPS lists produced no entries:
        if (l0_idx == 0 && ref_count > 0) {
            std::vector<uint8_t> curr_before;
            std::vector<uint8_t> curr_after;
            for (size_t i = 0; i < ref_count; ++i) {
                if (params.pic_param.ReferenceFrames[i].pic_order_cnt < poc) {
                    curr_before.push_back(static_cast<uint8_t>(i));
                } else {
                    curr_after.push_back(static_cast<uint8_t>(i));
                }
            }
            std::sort(curr_before.begin(), curr_before.end(), [&](uint8_t a, uint8_t b) {
                return params.pic_param.ReferenceFrames[a].pic_order_cnt > params.pic_param.ReferenceFrames[b].pic_order_cnt;
            });
            std::sort(curr_after.begin(), curr_after.end(), [&](uint8_t a, uint8_t b) {
                return params.pic_param.ReferenceFrames[a].pic_order_cnt < params.pic_param.ReferenceFrames[b].pic_order_cnt;
            });
            std::vector<uint8_t> list0 = curr_before;
            list0.insert(list0.end(), curr_after.begin(), curr_after.end());
            std::vector<uint8_t> list1 = curr_after;
            list1.insert(list1.end(), curr_before.begin(), curr_before.end());
            for (size_t i = 0; i < list0.size() && i < 15; ++i) params.slice_param.RefPicList[0][i] = list0[i];
            if (slice->slice_type == HevcSliceType::B) {
                for (size_t i = 0; i < list1.size() && i < 15; ++i) params.slice_param.RefPicList[1][i] = list1[i];
            }
        }

        params.slice_param.num_ref_idx_l0_active_minus1 = slice->num_ref_idx_l0_active_minus1;
        params.slice_param.num_ref_idx_l1_active_minus1 = (slice->slice_type == HevcSliceType::B) ? slice->num_ref_idx_l1_active_minus1 : 0;
    }

    return params;
}

static const int16_t vp9_dc_qlookup[256] = {
    4, 8, 8, 9, 10, 11, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19,
    20, 21, 22, 23, 24, 25, 26, 26, 27, 28, 29, 30, 31, 32, 32, 33,
    34, 35, 36, 37, 38, 38, 39, 40, 41, 42, 43, 43, 44, 45, 46, 47,
    48, 48, 49, 50, 51, 52, 53, 53, 54, 55, 56, 57, 57, 58, 59, 60,
    61, 62, 62, 63, 64, 65, 66, 66, 67, 68, 69, 70, 70, 71, 72, 73,
    74, 74, 75, 76, 77, 78, 78, 79, 80, 81, 81, 82, 83, 84, 85, 85,
    87, 88, 90, 92, 93, 95, 96, 98, 99, 101, 102, 104, 105, 107, 108, 110,
    111, 113, 114, 116, 117, 118, 120, 121, 123, 125, 127, 129, 131, 134, 136, 138,
    140, 142, 144, 146, 148, 150, 152, 154, 156, 158, 161, 164, 166, 169, 172, 174,
    177, 180, 182, 185, 187, 190, 192, 195, 199, 202, 205, 208, 211, 214, 217, 220,
    223, 226, 230, 233, 237, 240, 243, 247, 250, 253, 257, 261, 265, 269, 272, 276,
    280, 284, 288, 292, 296, 300, 304, 309, 313, 317, 322, 326, 330, 335, 340, 344,
    349, 354, 359, 364, 369, 374, 379, 384, 389, 395, 400, 406, 411, 417, 423, 429,
    435, 441, 447, 454, 461, 467, 475, 482, 489, 497, 505, 513, 522, 530, 539, 549,
    559, 569, 579, 590, 602, 614, 626, 640, 654, 668, 684, 700, 717, 736, 755, 775,
    796, 819, 843, 869, 896, 925, 955, 988, 1022, 1058, 1098, 1139, 1184, 1232, 1282, 1336
};

static const int16_t vp9_ac_qlookup[256] = {
    4, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
    39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
    55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
    71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86,
    87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102,
    104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128, 130, 132, 134,
    136, 138, 140, 142, 144, 146, 148, 150, 152, 155, 158, 161, 164, 167, 170, 173,
    176, 179, 182, 185, 188, 191, 194, 197, 200, 203, 207, 211, 215, 219, 223, 227,
    231, 235, 239, 243, 247, 251, 255, 260, 265, 270, 275, 280, 285, 290, 295, 300,
    305, 311, 317, 323, 329, 335, 341, 347, 353, 359, 366, 373, 380, 387, 394, 401,
    408, 416, 424, 432, 440, 448, 456, 465, 474, 483, 492, 501, 510, 520, 530, 540,
    550, 560, 571, 582, 593, 604, 615, 627, 639, 651, 663, 676, 689, 702, 715, 729,
    743, 757, 771, 786, 801, 816, 832, 848, 864, 881, 898, 915, 933, 951, 969, 988,
    1007, 1026, 1046, 1066, 1087, 1108, 1129, 1151, 1173, 1196, 1219, 1243, 1267, 1292, 1317, 1343,
    1369, 1396, 1423, 1451, 1479, 1508, 1537, 1567, 1597, 1628, 1660, 1692, 1725, 1759, 1793, 1828
};

VaapiVp9DecodeParams VaapiVp9DecodeParams::build(
    const Vp9UncompressedHeader& hdr,
    uint32_t curr_surface,
    const std::array<uint32_t, 8>& ref_surfaces,
    size_t slice_data_size
) {
    VaapiVp9DecodeParams params{};
    (void)curr_surface;

    params.pic_param.frame_width = static_cast<uint16_t>(hdr.width > 0 ? hdr.width : 1920);
    params.pic_param.frame_height = static_cast<uint16_t>(hdr.height > 0 ? hdr.height : 1080);

    params.pic_param.pic_fields.bits.subsampling_x = hdr.subsampling_x;
    params.pic_param.pic_fields.bits.subsampling_y = hdr.subsampling_y;
    params.pic_param.pic_fields.bits.frame_type = (hdr.frame_type == Vp9FrameType::KeyFrame) ? 0 : 1;
    params.pic_param.pic_fields.bits.show_frame = hdr.show_frame ? 1 : 0;
    params.pic_param.pic_fields.bits.error_resilient_mode = hdr.error_resilient_mode ? 1 : 0;
    params.pic_param.pic_fields.bits.intra_only = hdr.intra_only ? 1 : 0;
    params.pic_param.pic_fields.bits.allow_high_precision_mv = hdr.allow_high_precision_mv ? 1 : 0;
    params.pic_param.pic_fields.bits.mcomp_filter_type = (hdr.frame_type == Vp9FrameType::KeyFrame) ? 1 : hdr.mcomp_filter_type;
    params.pic_param.pic_fields.bits.frame_parallel_decoding_mode = hdr.frame_parallel_decoding_mode ? 1 : 0;
    params.pic_param.pic_fields.bits.reset_frame_context = hdr.reset_frame_context;
    params.pic_param.pic_fields.bits.refresh_frame_context = hdr.refresh_frame_context ? 1 : 0;
    params.pic_param.pic_fields.bits.frame_context_idx = hdr.frame_context_idx;
    params.pic_param.pic_fields.bits.segmentation_enabled = hdr.segmentation_enabled ? 1 : 0;
    params.pic_param.pic_fields.bits.segmentation_temporal_update = hdr.segmentation_temporal_update ? 1 : 0;
    params.pic_param.pic_fields.bits.segmentation_update_map = hdr.segmentation_update_map ? 1 : 0;
    params.pic_param.pic_fields.bits.last_ref_frame = hdr.ref_frame_indices[0];
    params.pic_param.pic_fields.bits.last_ref_frame_sign_bias = hdr.ref_frame_sign_bias[0];
    params.pic_param.pic_fields.bits.golden_ref_frame = hdr.ref_frame_indices[1];
    params.pic_param.pic_fields.bits.golden_ref_frame_sign_bias = hdr.ref_frame_sign_bias[1];
    params.pic_param.pic_fields.bits.alt_ref_frame = hdr.ref_frame_indices[2];
    params.pic_param.pic_fields.bits.alt_ref_frame_sign_bias = hdr.ref_frame_sign_bias[2];
    params.pic_param.pic_fields.bits.lossless_flag = 0;

    params.pic_param.filter_level = hdr.filter_level;
    params.pic_param.sharpness_level = hdr.sharpness_level;
    params.pic_param.log2_tile_rows = hdr.tile_rows_log2;
    params.pic_param.log2_tile_columns = hdr.tile_cols_log2;
    params.pic_param.frame_header_length_in_bytes = hdr.uncompressed_header_size;
    params.pic_param.first_partition_size = hdr.compressed_header_size;
    params.pic_param.profile = static_cast<uint8_t>(hdr.profile);
    params.pic_param.bit_depth = hdr.bit_depth;

    std::memcpy(params.pic_param.mb_segment_tree_probs, hdr.mb_segment_tree_probs.data(), 7);
    if (hdr.segmentation_temporal_update) {
        std::memcpy(params.pic_param.segment_pred_probs, hdr.segment_pred_probs.data(), 3);
    } else {
        std::memset(params.pic_param.segment_pred_probs, 255, 3);
    }

    for (size_t i = 0; i < 8; ++i) {
        if (ref_surfaces[i] != 0xFFFFFFFF && !hdr.is_keyframe()) {
            params.pic_param.reference_frames[i] = static_cast<VASurfaceID>(ref_surfaces[i]);
        } else {
            params.pic_param.reference_frames[i] = VA_INVALID_ID;
        }
    }

    params.has_slice = true;
    params.slice_param.slice_data_size = static_cast<uint32_t>(slice_data_size);
    params.slice_param.slice_data_offset = 0;
    params.slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;

    for (size_t i = 0; i < 8; ++i) {
        params.slice_param.seg_param[i].luma_dc_quant_scale = vp9_dc_qlookup[hdr.base_q_idx];
        params.slice_param.seg_param[i].luma_ac_quant_scale = vp9_ac_qlookup[hdr.base_q_idx];
        params.slice_param.seg_param[i].chroma_dc_quant_scale = vp9_dc_qlookup[hdr.base_q_idx];
        params.slice_param.seg_param[i].chroma_ac_quant_scale = vp9_ac_qlookup[hdr.base_q_idx];
        for (int r = 0; r < 4; ++r) {
            for (int m = 0; m < 2; ++m) {
                params.slice_param.seg_param[i].filter_level[r][m] = hdr.filter_level;
            }
        }
    }

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

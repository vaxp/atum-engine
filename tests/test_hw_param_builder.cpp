#include <cassert>
#include <iostream>
#include "atum/hwaccel/vaapi_param_builder.hpp"

using namespace atum;

#if defined(ATUM_HAS_VAAPI_HEADERS)

void test_h264_param_builder() {
    std::cout << "[Test] VA-API H.264 Parameter Builder..." << std::endl;
    H264Sps sps{};
    sps.pic_width_in_mbs_minus1 = 39;  // 640 width
    sps.pic_height_in_map_units_minus1 = 29; // 480 height
    sps.frame_mbs_only_flag = true;
    sps.chroma_format_idc = 1;
    sps.bit_depth_luma_minus8 = 0;
    sps.bit_depth_chroma_minus8 = 0;
    sps.max_num_ref_frames = 4;
    sps.level_idc = 41;

    H264Pps pps{};
    pps.pic_parameter_set_id = 0;
    pps.seq_parameter_set_id = 0;
    pps.entropy_coding_mode_flag = true;
    pps.pic_init_qp_minus26 = 0;
    pps.transform_8x8_mode_flag = true;

    H264SliceHeader slice{};
    slice.first_mb_in_slice = 0;
    slice.raw_slice_type = 2; // I-slice
    slice.slice_type = H264SliceType::I;
    slice.pic_parameter_set_id = 0;
    slice.frame_num = 0;
    slice.pic_order_cnt_lsb = 0;

    DecodedPictureBuffer dpb(4);

    auto params = VaapiH264DecodeParams::build(sps, pps, &slice, 101, 0, 0, dpb, 1024);
    assert(params.pic_param.picture_width_in_mbs_minus1 == 39);
    assert(params.pic_param.picture_height_in_mbs_minus1 == 29);
    assert(params.pic_param.CurrPic.picture_id == 101);
    assert(params.pic_param.pic_fields.bits.entropy_coding_mode_flag == 1);
    assert(params.pic_param.pic_fields.bits.transform_8x8_mode_flag == 1);
    assert(params.has_slice == true);
    assert(params.slice_param.slice_type == 2);
    assert(params.slice_param.slice_data_size == 1024);

    std::cout << "  -> Passed!" << std::endl;
}

void test_hevc_param_builder() {
    std::cout << "[Test] VA-API HEVC Parameter Builder..." << std::endl;
    HevcSps sps{};
    sps.pic_width_in_luma_samples = 1920;
    sps.pic_height_in_luma_samples = 1080;
    sps.bit_depth_luma_minus8 = 2; // 10-bit
    sps.bit_depth_chroma_minus8 = 2;
    sps.chroma_format_idc = 1;

    HevcPps pps{};
    pps.pps_pic_parameter_set_id = 0;
    pps.pps_seq_parameter_set_id = 0;
    pps.tiles_enabled_flag = true;
    pps.transquant_bypass_enabled_flag = false;

    HevcSliceHeader slice{};
    slice.raw_slice_type = 2; // I-slice
    slice.slice_type = HevcSliceType::I;
    slice.slice_pic_order_cnt_lsb = 10;

    DecodedPictureBuffer dpb(4);

    auto params = VaapiHevcDecodeParams::build(sps, pps, &slice, 202, 10, dpb, 2048);
    assert(params.pic_param.pic_width_in_luma_samples == 1920);
    assert(params.pic_param.pic_height_in_luma_samples == 1080);
    assert(params.pic_param.bit_depth_luma_minus8 == 2);
    assert(params.pic_param.CurrPic.picture_id == 202);
    assert(params.pic_param.pic_fields.bits.tiles_enabled_flag == 1);
    assert(params.has_slice == true);
    assert(params.slice_param.LongSliceFlags.fields.slice_type == 2);
    assert(params.slice_param.slice_data_size == 2048);

    std::cout << "  -> Passed!" << std::endl;
}

void test_vp9_param_builder() {
    std::cout << "[Test] VA-API VP9 Parameter Builder..." << std::endl;
    Vp9UncompressedHeader hdr{};
    hdr.width = 1280;
    hdr.height = 720;
    hdr.frame_type = Vp9FrameType::KeyFrame;
    hdr.show_frame = true;
    hdr.bit_depth = 8;
    hdr.filter_level = 12;
    hdr.sharpness_level = 3;
    hdr.profile = Vp9Profile::Profile0;

    auto params = VaapiVp9DecodeParams::build(hdr, 303);
    assert(params.pic_param.frame_width == 1280);
    assert(params.pic_param.frame_height == 720);
    assert(params.pic_param.pic_fields.bits.frame_type == 0); // Keyframe = 0
    assert(params.pic_param.pic_fields.bits.show_frame == 1);
    assert(params.pic_param.filter_level == 12);
    assert(params.pic_param.sharpness_level == 3);
    assert(params.pic_param.bit_depth == 8);

    std::cout << "  -> Passed!" << std::endl;
}

void test_av1_param_builder() {
    std::cout << "[Test] VA-API AV1 Parameter Builder..." << std::endl;
    Av1SequenceHeader seq{};
    seq.max_frame_width = 3840;
    seq.max_frame_height = 2160;
    seq.seq_profile = Av1Profile::Main;
    seq.color_config.high_bitdepth = false;

    Av1FrameHeader frame{};
    frame.frame_width = 3840;
    frame.frame_height = 2160;
    frame.frame_type = Av1FrameType::KeyFrame;

    auto params = VaapiAv1DecodeParams::build(seq, &frame, 404);
    assert(params.pic_param.frame_width_minus1 == 3839);
    assert(params.pic_param.frame_height_minus1 == 2159);
    assert(params.pic_param.pic_info_fields.bits.frame_type == 0);

    std::cout << "  -> Passed!" << std::endl;
}

#endif

int main() {
    std::cout << "=== Running VA-API Hardware Parameter Builder Tests ===" << std::endl;
#if defined(ATUM_HAS_VAAPI_HEADERS)
    test_h264_param_builder();
    test_hevc_param_builder();
    test_vp9_param_builder();
    test_av1_param_builder();
    std::cout << "=== All VA-API Parameter Builder Tests Passed Successfully! ===" << std::endl;
#else
    std::cout << "  [Notice] VA-API headers not installed; test skipped." << std::endl;
#endif
    return 0;
}

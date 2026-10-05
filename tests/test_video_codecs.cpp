#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include "atum/bitstream/bit_writer.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/bitstream/nal_unit.hpp"
#include "atum/codec/codec_registry.hpp"
#include "atum/codec/video/h264/h264_types.hpp"
#include "atum/codec/video/h264/h264_sps.hpp"
#include "atum/codec/video/h264/h264_pps.hpp"
#include "atum/codec/video/h264/h264_slice.hpp"
#include "atum/codec/video/h264/h264_parser.hpp"
#include "atum/codec/video/h264/h264_decoder.hpp"
#include "atum/codec/video/hevc/hevc_types.hpp"
#include "atum/codec/video/hevc/hevc_parameter_sets.hpp"
#include "atum/codec/video/hevc/hevc_slice.hpp"
#include "atum/codec/video/hevc/hevc_parser.hpp"
#include "atum/codec/video/hevc/hevc_decoder.hpp"
#include "atum/codec/video/vp9/vp9_types.hpp"
#include "atum/codec/video/vp9/vp9_header.hpp"
#include "atum/codec/video/vp9/vp9_parser.hpp"
#include "atum/codec/video/vp9/vp9_decoder.hpp"
#include "atum/codec/video/av1/av1_types.hpp"
#include "atum/codec/video/av1/av1_leb128.hpp"
#include "atum/codec/video/av1/av1_obu.hpp"
#include "atum/codec/video/av1/av1_parser.hpp"
#include "atum/codec/video/av1/av1_decoder.hpp"

using namespace atum;

void test_h264_codec() {
    std::cout << "[TEST] Running H.264 / AVC Subsystem Tests..." << std::endl;

    // 1. Synthesize SPS (Baseline Profile, 320x240, 30 fps, BT.709)
    BitWriter sps_writer;
    sps_writer.write_bits(66, 8);  // profile_idc = 66 (Baseline)
    sps_writer.write_bit(0);       // constraint_set0_flag
    sps_writer.write_bit(0);       // constraint_set1_flag
    sps_writer.write_bit(0);       // constraint_set2_flag
    sps_writer.write_bit(0);       // constraint_set3_flag
    sps_writer.write_bit(0);       // constraint_set4_flag
    sps_writer.write_bit(0);       // constraint_set5_flag
    sps_writer.write_bits(0, 2);   // reserved_zero_2bits
    sps_writer.write_bits(31, 8);  // level_idc = 3.1
    sps_writer.write_ue(0);        // seq_parameter_set_id = 0
    sps_writer.write_ue(0);        // log2_max_frame_num_minus4 = 0 (4 bits)
    sps_writer.write_ue(0);        // pic_order_cnt_type = 0
    sps_writer.write_ue(0);        // log2_max_pic_order_cnt_lsb_minus4 = 0 (4 bits)
    sps_writer.write_ue(1);        // max_num_ref_frames = 1
    sps_writer.write_bit(0);       // gaps_in_frame_num_value_allowed_flag = 0
    sps_writer.write_ue(19);       // pic_width_in_mbs_minus1 = 19 (width = 320)
    sps_writer.write_ue(14);       // pic_height_in_map_units_minus1 = 14 (height = 240)
    sps_writer.write_bit(1);       // frame_mbs_only_flag = 1
    sps_writer.write_bit(1);       // direct_8x8_inference_flag = 1
    sps_writer.write_bit(0);       // frame_cropping_flag = 0
    sps_writer.write_bit(1);       // vui_parameters_present_flag = 1
    // VUI
    sps_writer.write_bit(1);       // aspect_ratio_info_present_flag
    sps_writer.write_bits(1, 8);   // aspect_ratio_idc = 1 (Square)
    sps_writer.write_bit(0);       // overscan_info_present_flag
    sps_writer.write_bit(1);       // video_signal_type_present_flag
    sps_writer.write_bits(5, 3);   // video_format = 5
    sps_writer.write_bit(0);       // video_full_range_flag = 0 (Limited)
    sps_writer.write_bit(1);       // colour_description_present_flag
    sps_writer.write_bits(1, 8);   // colour_primaries = 1 (BT.709)
    sps_writer.write_bits(1, 8);   // transfer_characteristics = 1 (BT.709)
    sps_writer.write_bits(1, 8);   // matrix_coefficients = 1 (BT.709)
    sps_writer.write_bit(0);       // chroma_loc_info_present_flag
    sps_writer.write_bit(1);       // timing_info_present_flag
    sps_writer.write_bits(1000, 32); // num_units_in_tick = 1000
    sps_writer.write_bits(60000, 32); // time_scale = 60000 (30 fps)
    sps_writer.write_bit(1);       // fixed_frame_rate_flag = 1
    sps_writer.flush();

    const auto& sps_rbsp = sps_writer.buffer();
    auto sps_res = H264Sps::parse_rbsp(sps_rbsp);
    assert(sps_res.is_ok());
    assert(sps_res->width() == 320);
    assert(sps_res->height() == 240);
    assert(sps_res->vui.video_full_range_flag == false);
    assert(sps_res->vui.matrix_coefficients == 1);
    assert(sps_res->frame_rate().num == 30);

    // 2. Synthesize PPS
    BitWriter pps_writer;
    pps_writer.write_ue(0);        // pic_parameter_set_id = 0
    pps_writer.write_ue(0);        // seq_parameter_set_id = 0
    pps_writer.write_bit(0);       // entropy_coding_mode_flag = 0 (CAVLC)
    pps_writer.write_bit(0);       // bottom_field_pic_order_in_frame_present_flag = 0
    pps_writer.write_ue(0);        // num_slice_groups_minus1 = 0
    pps_writer.write_ue(0);        // num_ref_idx_l0_default_active_minus1 = 0
    pps_writer.write_ue(0);        // num_ref_idx_l1_default_active_minus1 = 0
    pps_writer.write_bit(0);       // weighted_pred_flag = 0
    pps_writer.write_bits(0, 2);   // weighted_bipred_idc = 0
    pps_writer.write_se(0);        // pic_init_qp_minus26 = 0
    pps_writer.write_se(0);        // pic_init_qs_minus26 = 0
    pps_writer.write_se(0);        // chroma_qp_index_offset = 0
    pps_writer.write_bit(0);       // deblocking_filter_control_present_flag = 0
    pps_writer.write_bit(0);       // constrained_intra_pred_flag = 0
    pps_writer.write_bit(0);       // redundant_pic_cnt_present_flag = 0
    pps_writer.flush();

    const auto& pps_rbsp = pps_writer.buffer();
    auto pps_res = H264Pps::parse_rbsp(pps_rbsp);
    assert(pps_res.is_ok());
    assert(pps_res->pic_parameter_set_id == 0);
    assert(pps_res->entropy_coding_mode_flag == false);

    // 3. Synthesize IDR Slice Header
    BitWriter slice_writer;
    slice_writer.write_ue(0);      // first_mb_in_slice = 0
    slice_writer.write_ue(7);      // slice_type = 7 (I-Slice)
    slice_writer.write_ue(0);      // pic_parameter_set_id = 0
    slice_writer.write_bits(0, 4); // frame_num = 0
    slice_writer.write_ue(0);      // idr_pic_id = 0
    slice_writer.write_bits(0, 4); // pic_order_cnt_lsb = 0
    slice_writer.write_bit(0);     // no_output_of_prior_pics_flag = 0
    slice_writer.write_bit(0);     // long_term_reference_flag = 0
    slice_writer.write_se(0);      // slice_qp_delta = 0
    slice_writer.flush();

    const auto& slice_rbsp = slice_writer.buffer();
    BitReader slice_reader(slice_rbsp);
    auto slice_res = H264SliceHeader::parse(slice_reader, H264NalType::SliceIdr, *sps_res, *pps_res);
    assert(slice_res.is_ok());
    assert(slice_res->slice_type == H264SliceType::I);
    assert(slice_res->is_keyframe());

    // 4. Test H264Decoder via CodecRegistry
    auto decoder = CodecRegistry::create_video_decoder(CodecId::H264);
    assert(decoder != nullptr);

    // Build Annex-B bitstream with SPS, PPS, and IDR slice
    std::vector<uint8_t> annex_b;
    auto append_nal = [&](uint8_t nal_header, std::span<const uint8_t> rbsp) {
        annex_b.push_back(0x00);
        annex_b.push_back(0x00);
        annex_b.push_back(0x00);
        annex_b.push_back(0x01);
        annex_b.push_back(nal_header);
        auto ebsp = NalParser::rbsp_to_ebsp(rbsp);
        annex_b.insert(annex_b.end(), ebsp.begin(), ebsp.end());
    };

    append_nal(0x67, sps_rbsp); // SPS (nal_ref_idc=3, type=7)
    append_nal(0x68, pps_rbsp); // PPS (nal_ref_idc=3, type=8)
    append_nal(0x65, slice_rbsp); // IDR (nal_ref_idc=3, type=5)

    auto packet = Packet::from_borrowed(annex_b.data(), annex_b.size());
    packet.set_pts(0);
    packet.set_keyframe(true);

    auto send_res = decoder->send_packet(packet);
    assert(send_res.is_ok());

    VideoFrame frame;
    auto recv_res = decoder->receive_frame(frame);
    assert(recv_res.is_ok());
    assert(frame.width() == 320);
    assert(frame.height() == 240);
    assert(frame.pixel_format() == PixelFormat::YUV420P);
    assert(frame.is_keyframe());
    assert(frame.color_space() == ColorSpace::BT709);

    std::cout << "[PASS] H.264 / AVC Subsystem Verified (SPS, PPS, Slice, Decoder 320x240 BT.709)." << std::endl;
}

void test_hevc_codec() {
    std::cout << "[TEST] Running HEVC / H.265 Subsystem Tests..." << std::endl;

    // 1. Synthesize SPS (Main Profile, 640x360)
    BitWriter sps_writer;
    sps_writer.write_bits(0, 4);   // sps_video_parameter_set_id = 0
    sps_writer.write_bits(0, 3);   // sps_max_sub_layers_minus1 = 0
    sps_writer.write_bit(1);       // sps_temporal_id_nesting_flag = 1
    // Profile Tier Level
    sps_writer.write_bits(0, 2);   // general_profile_space = 0
    sps_writer.write_bit(0);       // general_tier_flag = 0
    sps_writer.write_bits(1, 5);   // general_profile_idc = 1 (Main)
    sps_writer.write_bits(0xFFFFFFFF, 32); // compatibility flags
    sps_writer.write_bit(1);       // progressive source
    sps_writer.write_bit(0);       // interlaced source
    sps_writer.write_bit(1);       // non-packed
    sps_writer.write_bit(1);       // frame-only
    sps_writer.write_bits(0, 32);  // reserved 32 bits
    sps_writer.write_bits(0, 12);  // reserved 12 bits
    sps_writer.write_bits(93, 8);  // general_level_idc = 93 (Level 3.1)
    // SPS body
    sps_writer.write_ue(0);        // sps_seq_parameter_set_id = 0
    sps_writer.write_ue(1);        // chroma_format_idc = 1 (4:2:0)
    sps_writer.write_ue(640);      // pic_width_in_luma_samples = 640
    sps_writer.write_ue(360);      // pic_height_in_luma_samples = 360
    sps_writer.write_bit(0);       // conformance_window_flag = 0
    sps_writer.write_ue(0);        // bit_depth_luma_minus8 = 0 (8-bit)
    sps_writer.write_ue(0);        // bit_depth_chroma_minus8 = 0 (8-bit)
    sps_writer.write_ue(4);        // log2_max_pic_order_cnt_lsb_minus4 = 4
    sps_writer.write_bit(0);       // sps_sub_layer_ordering_info_present_flag = 0
    sps_writer.write_ue(4);        // max_dec_pic_buffering_minus1 = 4
    sps_writer.write_ue(2);        // max_num_reorder_pics = 2
    sps_writer.write_ue(0);        // max_latency_increase_plus1 = 0
    sps_writer.write_ue(0);        // log2_min_luma_coding_block_size_minus3 = 0 (min CB 8x8)
    sps_writer.write_ue(3);        // log2_diff_max_min_luma_coding_block_size = 3 (max CB 64x64)
    sps_writer.write_ue(0);        // min_tb
    sps_writer.write_ue(3);        // diff_tb
    sps_writer.write_ue(2);        // max_depth_inter
    sps_writer.write_ue(2);        // max_depth_intra
    sps_writer.write_bit(0);       // scaling_list_enabled = 0
    sps_writer.write_bit(0);       // amp_enabled = 0
    sps_writer.write_bit(0);       // sao_enabled = 0
    sps_writer.write_bit(0);       // pcm_enabled = 0
    sps_writer.write_ue(0);        // num_short_term_ref_pic_sets = 0
    sps_writer.write_bit(0);       // long_term_ref_pics_present = 0
    sps_writer.write_bit(0);       // sps_temporal_mvp_enabled = 0
    sps_writer.write_bit(0);       // strong_intra_smoothing = 0
    sps_writer.write_bit(1);       // vui_parameters_present_flag = 1
    // VUI
    sps_writer.write_bit(1);       // aspect_ratio_info_present
    sps_writer.write_bits(1, 8);   // aspect_ratio_idc = 1 (Square)
    sps_writer.write_bit(0);       // overscan
    sps_writer.write_bit(1);       // video_signal_type_present
    sps_writer.write_bits(5, 3);   // video_format
    sps_writer.write_bit(0);       // full_range = 0
    sps_writer.write_bit(1);       // colour_description_present
    sps_writer.write_bits(1, 8);   // primaries = BT.709
    sps_writer.write_bits(1, 8);   // transfer = BT.709
    sps_writer.write_bits(1, 8);   // matrix = BT.709
    sps_writer.write_bit(0);       // chroma_loc
    sps_writer.write_bit(0);       // neutral_chroma
    sps_writer.write_bit(0);       // field_seq
    sps_writer.write_bit(0);       // frame_field
    sps_writer.write_bit(0);       // timing_info
    sps_writer.flush();

    const auto& sps_rbsp = sps_writer.buffer();
    auto sps_res = HevcSps::parse_rbsp(sps_rbsp);
    assert(sps_res.is_ok());
    assert(sps_res->width() == 640);
    assert(sps_res->height() == 360);
    assert(sps_res->vui.matrix_coeffs == 1);

    // 2. Synthesize PPS
    BitWriter pps_writer;
    pps_writer.write_ue(0);        // pps_pic_parameter_set_id = 0
    pps_writer.write_ue(0);        // pps_seq_parameter_set_id = 0
    pps_writer.write_bit(0);       // dependent_slice_segments_enabled_flag = 0
    pps_writer.write_bit(0);       // output_flag_present_flag = 0
    pps_writer.write_bits(0, 3);   // num_extra_slice_header_bits = 0
    pps_writer.write_bit(0);       // sign_data_hiding_enabled_flag = 0
    pps_writer.write_bit(0);       // cabac_init_present_flag = 0
    pps_writer.write_ue(0);        // num_ref_idx_l0_default_active_minus1 = 0
    pps_writer.write_ue(0);        // num_ref_idx_l1_default_active_minus1 = 0
    pps_writer.write_se(0);        // init_qp_minus26 = 0
    pps_writer.write_bit(0);       // constrained_intra_pred_flag = 0
    pps_writer.write_bit(0);       // transform_skip_enabled_flag = 0
    pps_writer.write_bit(0);       // cu_qp_delta_enabled_flag = 0
    pps_writer.write_se(0);        // pps_cb_qp_offset = 0
    pps_writer.write_se(0);        // pps_cr_qp_offset = 0
    pps_writer.write_bit(0);       // pps_slice_chroma_qp_offsets_present_flag = 0
    pps_writer.write_bit(0);       // weighted_pred_flag = 0
    pps_writer.write_bit(0);       // weighted_bipred_flag = 0
    pps_writer.write_bit(0);       // transquant_bypass_enabled_flag = 0
    pps_writer.write_bit(0);       // tiles_enabled_flag = 0
    pps_writer.write_bit(0);       // entropy_coding_sync_enabled_flag = 0
    pps_writer.flush();

    const auto& pps_rbsp = pps_writer.buffer();
    auto pps_res = HevcPps::parse_rbsp(pps_rbsp);
    assert(pps_res.is_ok());
    assert(pps_res->pps_pic_parameter_set_id == 0);

    // 3. Test HEVC Decoder via CodecRegistry
    auto decoder = CodecRegistry::create_video_decoder(CodecId::H265);
    assert(decoder != nullptr);

    std::vector<uint8_t> annex_b;
    auto append_hevc_nal = [&](uint8_t byte0, uint8_t byte1, std::span<const uint8_t> rbsp) {
        annex_b.push_back(0x00);
        annex_b.push_back(0x00);
        annex_b.push_back(0x00);
        annex_b.push_back(0x01);
        annex_b.push_back(byte0);
        annex_b.push_back(byte1);
        auto ebsp = NalParser::rbsp_to_ebsp(rbsp);
        annex_b.insert(annex_b.end(), ebsp.begin(), ebsp.end());
    };

    // SPS NAL (type 33 = 0x21 -> byte0 = 0x42, byte1 = 0x01)
    append_hevc_nal(0x42, 0x01, sps_rbsp);
    // PPS NAL (type 34 = 0x22 -> byte0 = 0x44, byte1 = 0x01)
    append_hevc_nal(0x44, 0x01, pps_rbsp);

    // IDR Slice (type 19 = 0x13 -> byte0 = 0x26, byte1 = 0x01)
    BitWriter slice_writer;
    slice_writer.write_bit(1);     // first_slice_segment_in_pic_flag = 1
    slice_writer.write_bit(0);     // no_output_of_prior_pics_flag = 0
    slice_writer.write_ue(0);      // slice_pic_parameter_set_id = 0
    slice_writer.write_ue(2);      // slice_type = 2 (I-Slice)
    slice_writer.write_se(0);      // slice_qp_delta = 0
    slice_writer.flush();

    append_hevc_nal(0x26, 0x01, slice_writer.buffer());

    auto packet = Packet::from_borrowed(annex_b.data(), annex_b.size());
    packet.set_pts(1000);
    packet.set_keyframe(true);

    auto send_res = decoder->send_packet(packet);
    assert(send_res.is_ok());

    VideoFrame frame;
    auto recv_res = decoder->receive_frame(frame);
    assert(recv_res.is_ok());
    assert(frame.width() == 640);
    assert(frame.height() == 360);
    assert(frame.pixel_format() == PixelFormat::YUV420P);
    assert(frame.color_space() == ColorSpace::BT709);

    std::cout << "[PASS] HEVC / H.265 Subsystem Verified (SPS, PPS, Slice, Decoder 640x360 BT.709)." << std::endl;
}

void test_vp9_codec() {
    std::cout << "[TEST] Running VP9 Subsystem Tests..." << std::endl;

    // 1. Synthesize VP9 Uncompressed Keyframe Header (1280x720, BT.709, Full Range)
    BitWriter writer;
    writer.write_bits(2, 2);       // frame_marker = 2 (0b10)
    writer.write_bit(0);           // profile_low_bit = 0 (Profile 0)
    writer.write_bit(0);           // profile_high_bit = 0
    writer.write_bit(0);           // show_existing_frame = 0
    writer.write_bit(0);           // frame_type = 0 (KeyFrame)
    writer.write_bit(1);           // show_frame = 1
    writer.write_bit(0);           // error_resilient_mode = 0
    // Sync code 0x49, 0x83, 0x42
    writer.write_bits(0x49, 8);
    writer.write_bits(0x83, 8);
    writer.write_bits(0x42, 8);
    // Color config
    writer.write_bits(2, 3);       // color_space = 2 (CS_BT_709)
    writer.write_bit(1);           // color_range_full = 1 (Full)
    // Frame size
    writer.write_bits(1280 - 1, 16); // width = 1280
    writer.write_bits(720 - 1, 16);  // height = 720
    writer.write_bit(0);           // render_and_frame_size_different = 0
    // Loop filter & quantization
    writer.write_bits(10, 6);      // filter_level = 10
    writer.write_bits(2, 3);       // sharpness_level = 2
    writer.write_bit(0);           // mode_ref_delta_enabled = 0
    writer.write_bits(50, 8);      // base_q_idx = 50
    writer.flush();

    const auto& vp9_data = writer.buffer();
    auto hdr_res = Vp9UncompressedHeader::parse_frame(vp9_data);
    assert(hdr_res.is_ok());
    assert(hdr_res->is_keyframe());
    assert(hdr_res->width == 1280);
    assert(hdr_res->height == 720);
    assert(hdr_res->color_space == Vp9ColorSpace::BT709);
    assert(hdr_res->color_range_full == true);

    // 2. Test VP9 Superframe Parser
    // Construct superframe containing 2 identical subframes
    std::vector<uint8_t> sf_data;
    sf_data.insert(sf_data.end(), vp9_data.begin(), vp9_data.end());
    sf_data.insert(sf_data.end(), vp9_data.begin(), vp9_data.end());

    // Superframe index footer:
    // marker: 0b110[bytes_per_frame_minus_1][num_frames_minus_1]
    // 2 frames, 1 byte length per frame -> marker = 0b110_00_001 = 0xC1
    uint8_t marker = 0xC1;
    sf_data.push_back(marker);
    sf_data.push_back(static_cast<uint8_t>(vp9_data.size()));
    sf_data.push_back(static_cast<uint8_t>(vp9_data.size()));
    sf_data.push_back(marker);

    Vp9Parser parser;
    auto subframes = parser.parse_packet(sf_data);
    assert(subframes.is_ok());
    assert(subframes->size() == 2);
    assert((*subframes)[0].header.has_value());
    assert((*subframes)[0].header->width == 1280);
    assert((*subframes)[1].header.has_value());
    assert((*subframes)[1].header->height == 720);

    // 3. Test VP9 Decoder via CodecRegistry
    auto decoder = CodecRegistry::create_video_decoder(CodecId::VP9);
    assert(decoder != nullptr);

    auto packet = Packet::from_borrowed(vp9_data.data(), vp9_data.size());
    packet.set_pts(1000);
    packet.set_keyframe(true);

    auto send_res = decoder->send_packet(packet);
    assert(send_res.is_ok());

    VideoFrame frame;
    auto recv_res = decoder->receive_frame(frame);
    assert(recv_res.is_ok());
    assert(frame.width() == 1280);
    assert(frame.height() == 720);
    assert(frame.color_space() == ColorSpace::BT709);
    assert(frame.color_range() == ColorRange::Full);

    std::cout << "[PASS] VP9 Subsystem Verified (Superframe, Uncompressed Header, Decoder 1280x720 Full-Range BT.709)." << std::endl;
}

void test_av1_codec() {
    std::cout << "[TEST] Running AV1 Subsystem Tests..." << std::endl;

    // 1. Test LEB128 Decoder and Encoder
    uint8_t leb_buf[8];
    size_t written = Av1Leb128::encode(1234567, leb_buf);
    assert(written > 0 && written <= 4);

    auto decoded = Av1Leb128::decode(leb_buf, written);
    assert(decoded.is_ok());
    assert(decoded->first == 1234567);
    assert(decoded->second == written);

    // Test 1-byte LEB128
    written = Av1Leb128::encode(42, leb_buf);
    assert(written == 1);
    decoded = Av1Leb128::decode(leb_buf, 1);
    assert(decoded.is_ok() && decoded->first == 42 && decoded->second == 1);

    // 2. Synthesize Sequence Header OBU (1920x1080, Main Profile)
    BitWriter seq_writer;
    seq_writer.write_bits(0, 3);   // seq_profile = 0 (Main)
    seq_writer.write_bit(0);       // still_picture = 0
    seq_writer.write_bit(0);       // reduced_still_picture_header = 0
    seq_writer.write_bit(1);       // timing_info_present_flag = 1
    seq_writer.write_bits(1, 32);  // num_units_in_display_tick = 1
    seq_writer.write_bits(60, 32); // time_scale = 60 (60 fps)
    seq_writer.write_bit(0);       // equal_picture_interval = 0
    seq_writer.write_bit(0);       // decoder_model_info_present_flag = 0
    seq_writer.write_bit(0);       // initial_display_delay_present_flag = 0
    seq_writer.write_bits(0, 5);   // operating_points_cnt_minus_1 = 0
    seq_writer.write_bits(0, 12);  // operating_point_idc[0] = 0
    seq_writer.write_bits(8, 5);   // seq_level_idx[0] = 8 (Level 4.0)
    seq_writer.write_bit(0);       // seq_tier[0] = 0
    // Dimensions: 1920x1080
    // 1920-1 = 1919 (11 bits), 1080-1 = 1079 (11 bits)
    seq_writer.write_bits(10, 4);  // frame_width_bits_minus_1 = 10 (11 bits)
    seq_writer.write_bits(10, 4);  // frame_height_bits_minus_1 = 10 (11 bits)
    seq_writer.write_bits(1919, 11); // max_frame_width_minus_1 = 1919
    seq_writer.write_bits(1079, 11); // max_frame_height_minus_1 = 1079
    seq_writer.write_bit(0);       // frame_id_numbers_present_flag = 0
    seq_writer.write_bit(0);       // use_128x128_superblock = 0
    seq_writer.write_bit(0);       // enable_filter_intra = 0
    seq_writer.write_bit(0);       // enable_intra_edge_filter = 0
    seq_writer.write_bit(0);       // enable_interintra_compound = 0
    seq_writer.write_bit(0);       // enable_masked_compound = 0
    seq_writer.write_bit(0);       // enable_warped_motion = 0
    seq_writer.write_bit(0);       // enable_dual_filter = 0
    seq_writer.write_bit(0);       // enable_order_hint = 0
    seq_writer.write_bit(0);       // seq_choose_screen_content_tools = 0
    seq_writer.write_bits(0, 1);   // seq_force_screen_content_tools = 0
    seq_writer.write_bits(0, 3);   // order_hint_bits
    seq_writer.write_bit(0);       // enable_superres = 0
    seq_writer.write_bit(0);       // enable_cdef = 0
    seq_writer.write_bit(0);       // enable_restoration = 0
    // Color config
    seq_writer.write_bit(0);       // high_bitdepth = 0 (8-bit)
    seq_writer.write_bit(0);       // mono_chrome = 0
    seq_writer.write_bit(1);       // color_description_present_flag = 1
    seq_writer.write_bits(1, 8);   // color_primaries = 1 (BT.709)
    seq_writer.write_bits(1, 8);   // transfer_characteristics = 1 (BT.709)
    seq_writer.write_bits(1, 8);   // matrix_coefficients = 1 (BT.709)
    seq_writer.write_bit(0);       // color_range_full = 0 (Limited)
    seq_writer.write_bits(0, 2);   // chroma_sample_position = 0
    seq_writer.write_bit(0);       // separate_uv_delta_q = 0
    seq_writer.write_bit(0);       // film_grain_params_present = 0
    seq_writer.flush();

    const auto& seq_payload = seq_writer.buffer();
    auto seq_res = Av1SequenceHeader::parse_payload(seq_payload);
    assert(seq_res.is_ok());
    assert(seq_res->width() == 1920);
    assert(seq_res->height() == 1080);
    assert(seq_res->color_config.matrix_coefficients == 1);
    assert(seq_res->frame_rate().num == 60);

    // 3. Test AV1 Parser with OBU sequence
    // Wrap SequenceHeader OBU:
    // OBU Header: type=1 (SequenceHeader), has_size_field=1 -> (1 << 3) | (1 << 1) = 0x0A
    std::vector<uint8_t> obu_stream;
    obu_stream.push_back(0x0A);
    uint8_t len_buf[8];
    size_t len_sz = Av1Leb128::encode(seq_payload.size(), len_buf);
    obu_stream.insert(obu_stream.end(), len_buf, len_buf + len_sz);
    obu_stream.insert(obu_stream.end(), seq_payload.begin(), seq_payload.end());

    // Wrap FrameHeader OBU:
    // OBU Header: type=3 (FrameHeader), has_size_field=1 -> 0b0_0011_0_1_0 = 0x1A
    BitWriter frame_writer;
    frame_writer.write_bit(0);     // show_existing_frame = 0
    frame_writer.write_bits(0, 2); // frame_type = 0 (KeyFrame)
    frame_writer.write_bit(1);     // show_frame = 1
    frame_writer.write_bit(0);     // error_resilient_mode = 0
    frame_writer.write_bit(0);     // disable_cdf_update = 0
    frame_writer.write_bit(0);     // frame_size_override_flag = 0
    frame_writer.write_bit(0);     // render_and_frame_size_different = 0
    frame_writer.flush();

    const auto& frame_payload = frame_writer.buffer();
    obu_stream.push_back(0x1A);
    len_sz = Av1Leb128::encode(frame_payload.size(), len_buf);
    obu_stream.insert(obu_stream.end(), len_buf, len_buf + len_sz);
    obu_stream.insert(obu_stream.end(), frame_payload.begin(), frame_payload.end());

    Av1Parser parser;
    auto units = parser.parse_stream(obu_stream);
    assert(units.is_ok());
    assert(units->size() == 2);
    assert(parser.width() == 1920);
    assert(parser.height() == 1080);
    assert(parser.last_frame_header().has_value());
    assert(parser.last_frame_header()->is_keyframe());

    // 4. Test AV1 Decoder via CodecRegistry
    auto decoder = CodecRegistry::create_video_decoder(CodecId::AV1);
    assert(decoder != nullptr);

    auto packet = Packet::from_borrowed(obu_stream.data(), obu_stream.size());
    packet.set_pts(1000);
    packet.set_keyframe(true);

    auto send_res = decoder->send_packet(packet);
    assert(send_res.is_ok());

    VideoFrame frame;
    auto recv_res = decoder->receive_frame(frame);
    assert(recv_res.is_ok());
    assert(frame.width() == 1920);
    assert(frame.height() == 1080);
    assert(frame.pixel_format() == PixelFormat::YUV420P);
    assert(frame.color_space() == ColorSpace::BT709);

    std::cout << "[PASS] AV1 Subsystem Verified (LEB128, OBU Sequence & Frame Headers, Decoder 1920x1080 60fps BT.709)." << std::endl;
}

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "  Nisaba Media: Sovereign Video Codecs Test Suite " << std::endl;
    std::cout << "  (H.264 / AVC, HEVC / H.265, VP9, AV1)           " << std::endl;
    std::cout << "=================================================" << std::endl;

    test_h264_codec();
    test_hevc_codec();
    test_vp9_codec();
    test_av1_codec();

    std::cout << "\n>>> ALL 4 VIDEO CODECS VERIFIED FLAWLESSLY! <<<" << std::endl;
    return 0;
}

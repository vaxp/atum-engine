#include "atum/resilience/error_concealer.hpp"
#include "atum/codec/video/h264/h264_decoder.hpp"
#include "atum/bitstream/bit_writer.hpp"
#include "atum/bitstream/nal_unit.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace atum;

static void test_auto_coordinator_modes() {
    constexpr uint32_t W = 64;
    constexpr uint32_t H = 64;

    // 1. Keyframe -> Spatial Concealment
    {
        VideoFrame frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
        frame.set_keyframe(true);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                frame.plane_mut(0)[y * frame.stride(0) + x] = 150;
            }
        }

        ErrorConcealmentMask mask(W, H, 16);
        mask.mark_damaged(1, 1);

        DecodedPictureBuffer dpb(4);
        ErrorConcealer concealer(ErrorConcealmentMode::Auto);

        auto res = concealer.conceal(frame, mask, &dpb);
        assert(res.is_ok());
        assert(mask.get(1, 1) == BlockStatus::ConcealedSpatial);
        assert(concealer.stats().concealed_frames == 1);
        assert(concealer.stats().spatially_concealed_blocks >= 1);
        assert(concealer.stats().temporally_concealed_blocks == 0);
    }

    // 2. Inter-frame with DPB -> Temporal Concealment
    {
        VideoFrame ref_frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
        VideoFrame frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
        frame.set_keyframe(false);

        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                ref_frame.plane_mut(0)[y * ref_frame.stride(0) + x] = 200;
                frame.plane_mut(0)[y * frame.stride(0) + x] = 200;
            }
        }

        DecodedPictureBuffer dpb(4);
        dpb.insert(ref_frame.clone(), 0, 0, true);

        ErrorConcealmentMask mask(W, H, 16);
        mask.mark_damaged(2, 2);

        ErrorConcealer concealer(ErrorConcealmentMode::Auto);
        auto res = concealer.conceal(frame, mask, &dpb);
        assert(res.is_ok());
        assert(mask.get(2, 2) == BlockStatus::ConcealedTemporal);
        assert(concealer.stats().concealed_frames == 1);
        assert(concealer.stats().temporally_concealed_blocks >= 1);
    }

    // 3. Disabled Mode
    {
        VideoFrame frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
        ErrorConcealmentMask mask(W, H, 16);
        mask.mark_damaged(1, 1);

        ErrorConcealer concealer(ErrorConcealmentMode::Disabled);
        auto res = concealer.conceal(frame, mask, nullptr);
        assert(res.is_ok());
        assert(mask.is_damaged(1, 1)); // Untouched
        assert(concealer.stats().concealed_frames == 0);
    }

    std::cout << "test_auto_coordinator_modes passed.\n";
}

static void test_h264_decoder_resilience_pipeline() {
    H264Decoder decoder;
    assert(decoder.is_error_concealment_enabled());

    // 1. Synthesize 64x64 SPS
    BitWriter sps_w;
    sps_w.write_bits(66, 8);  // profile_idc = 66 (Baseline)
    sps_w.write_bit(0);       // constraint_set0_flag
    sps_w.write_bit(0);       // constraint_set1_flag
    sps_w.write_bit(0);       // constraint_set2_flag
    sps_w.write_bit(0);       // constraint_set3_flag
    sps_w.write_bit(0);       // constraint_set4_flag
    sps_w.write_bit(0);       // constraint_set5_flag
    sps_w.write_bits(0, 2);   // reserved_zero_2bits
    sps_w.write_bits(31, 8);  // level_idc = 3.1
    sps_w.write_ue(0);        // seq_parameter_set_id = 0
    sps_w.write_ue(0);        // log2_max_frame_num_minus4 = 0 (4 bits)
    sps_w.write_ue(0);        // pic_order_cnt_type = 0
    sps_w.write_ue(0);        // log2_max_pic_order_cnt_lsb_minus4 = 0 (4 bits)
    sps_w.write_ue(1);        // max_num_ref_frames = 1
    sps_w.write_bit(0);       // gaps_in_frame_num_value_allowed_flag = 0
    sps_w.write_ue(3);        // pic_width_in_mbs_minus1 = 3 (width = 64)
    sps_w.write_ue(3);        // pic_height_in_map_units_minus1 = 3 (height = 64)
    sps_w.write_bit(1);       // frame_mbs_only_flag = 1
    sps_w.write_bit(1);       // direct_8x8_inference_flag = 1
    sps_w.write_bit(0);       // frame_cropping_flag = 0
    sps_w.write_bit(0);       // vui_parameters_present_flag = 0
    sps_w.flush();

    // 2. Synthesize PPS
    BitWriter pps_w;
    pps_w.write_ue(0);        // pic_parameter_set_id = 0
    pps_w.write_ue(0);        // seq_parameter_set_id = 0
    pps_w.write_bit(0);       // entropy_coding_mode_flag = 0 (CAVLC)
    pps_w.write_bit(0);       // bottom_field_pic_order_in_frame_present_flag = 0
    pps_w.write_ue(0);        // num_slice_groups_minus1 = 0
    pps_w.write_ue(0);        // num_ref_idx_l0_default_active_minus1 = 0
    pps_w.write_ue(0);        // num_ref_idx_l1_default_active_minus1 = 0
    pps_w.write_bit(0);       // weighted_pred_flag = 0
    pps_w.write_bits(0, 2);   // weighted_bipred_idc = 0
    pps_w.write_se(0);        // pic_init_qp_minus26 = 0
    pps_w.write_se(0);        // pic_init_qs_minus26 = 0
    pps_w.write_se(0);        // chroma_qp_index_offset = 0
    pps_w.write_bit(0);       // deblocking_filter_control_present_flag = 0
    pps_w.write_bit(0);       // constrained_intra_pred_flag = 0
    pps_w.write_bit(0);       // redundant_pic_cnt_present_flag = 0
    pps_w.flush();

    std::vector<uint8_t> init_stream;
    auto append_nal = [&](uint8_t header, const std::vector<uint8_t>& rbsp) {
        init_stream.push_back(0x00);
        init_stream.push_back(0x00);
        init_stream.push_back(0x00);
        init_stream.push_back(0x01);
        init_stream.push_back(header);
        auto ebsp = NalParser::rbsp_to_ebsp(rbsp);
        init_stream.insert(init_stream.end(), ebsp.begin(), ebsp.end());
    };

    append_nal(0x67, sps_w.buffer()); // SPS
    append_nal(0x68, pps_w.buffer()); // PPS

    Packet init_pkt = Packet::from_bytes(init_stream);
    init_pkt.set_keyframe(true);
    auto res_init = decoder.send_packet(init_pkt);
    assert(res_init.is_ok());

    // 3. Synthesize IDR Slice Header
    BitWriter idr_w;
    idr_w.write_ue(0);      // first_mb_in_slice = 0
    idr_w.write_ue(7);      // slice_type = 7 (I-Slice)
    idr_w.write_ue(0);      // pic_parameter_set_id = 0
    idr_w.write_bits(0, 4); // frame_num = 0
    idr_w.write_ue(0);      // idr_pic_id = 0
    idr_w.write_bits(0, 4); // pic_order_cnt_lsb = 0
    idr_w.write_bit(0);     // no_output_of_prior_pics_flag = 0
    idr_w.write_bit(0);     // long_term_reference_flag = 0
    idr_w.write_se(0);      // slice_qp_delta = 0
    idr_w.flush();

    // Now send an IDR slice packet with intentional garbage prepended (resync hunting test)
    std::vector<uint8_t> corrupt_idr = {
        0xDE, 0xAD, 0xBE, 0xEF,                         // Corrupt leading bytes
        0x00, 0x00, 0x01, 0x65                          // Valid IDR start code & header (type 5)
    };
    auto idr_ebsp = NalParser::rbsp_to_ebsp(idr_w.buffer());
    corrupt_idr.insert(corrupt_idr.end(), idr_ebsp.begin(), idr_ebsp.end());

    Packet idr_pkt = Packet::from_bytes(corrupt_idr);
    idr_pkt.set_keyframe(true);
    idr_pkt.set_pts(0);

    auto res_send = decoder.send_packet(idr_pkt);
    assert(res_send.is_ok());

    VideoFrame out_frame;
    auto res_recv = decoder.receive_frame(out_frame);
    assert(res_recv.is_ok());
    assert(out_frame.width() == 64);
    assert(out_frame.height() == 64);
    assert(out_frame.is_keyframe());

    // Check that the resilient parser logged the skipped garbage
    const auto& pstats = decoder.resilient_parser().stats();
    assert(pstats.valid_nals_extracted >= 1);
    assert(pstats.corrupted_bytes_skipped >= 4);

    // 4. Synthesize P-Slice Header
    BitWriter p_w;
    p_w.write_ue(0);        // first_mb_in_slice = 0
    p_w.write_ue(5);        // slice_type = 5 (P-Slice)
    p_w.write_ue(0);        // pic_parameter_set_id = 0
    p_w.write_bits(1, 4);   // frame_num = 1
    p_w.write_bits(2, 4);   // pic_order_cnt_lsb = 2
    p_w.write_bit(0);       // num_ref_idx_active_override_flag = 0
    p_w.write_bit(0);       // ref_pic_list_modification_flag_l0 = 0
    p_w.write_bit(0);       // adaptive_ref_pic_marking_mode_flag = 0
    p_w.write_se(0);        // slice_qp_delta = 0
    p_w.flush();

    std::vector<uint8_t> corrupt_p = {
        0x00, 0x00, 0x01, 0x61                          // Non-IDR P-slice header (type 1)
    };
    auto p_ebsp = NalParser::rbsp_to_ebsp(p_w.buffer());
    corrupt_p.insert(corrupt_p.end(), p_ebsp.begin(), p_ebsp.end());

    Packet p_pkt = Packet::from_bytes(corrupt_p);
    p_pkt.set_keyframe(false);
    p_pkt.set_pts(3000);

    auto res_p = decoder.send_packet(p_pkt);
    assert(res_p.is_ok());

    VideoFrame p_frame;
    auto res_p_recv = decoder.receive_frame(p_frame);
    assert(res_p_recv.is_ok());
    assert(p_frame.width() == 64);
    assert(p_frame.height() == 64);

    // Verify error concealer concealed frames
    const auto& cstats = decoder.error_concealer().stats();
    std::cout << "Concealed frames: " << cstats.concealed_frames
              << ", spatially concealed: " << cstats.spatially_concealed_blocks
              << ", temporally concealed: " << cstats.temporally_concealed_blocks
              << ", parser recovered bytes: " << pstats.corrupted_bytes_skipped << "\n";

    std::cout << "test_h264_decoder_resilience_pipeline passed.\n";
}

int main() {
    std::cout << "Running test_integrated_error_concealment...\n";
    test_auto_coordinator_modes();
    test_h264_decoder_resilience_pipeline();
    std::cout << "ALL test_integrated_error_concealment checks passed successfully!\n";
    return 0;
}

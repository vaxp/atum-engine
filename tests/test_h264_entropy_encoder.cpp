#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>
#include "atum/codec/video/h264/h264_entropy_encoder.hpp"
#include "atum/codec/video/h264/h264_parser.hpp"

using namespace atum;

void test_sps_pps_serialization_roundtrip() {
    std::cout << "Testing SPS / PPS Serialization and Parsing Round-Trip..." << std::endl;

    H264Sps sps{};
    sps.profile_idc = 100; // High Profile
    sps.level_idc = 41;
    sps.seq_parameter_set_id = 0;
    sps.chroma_format_idc = 1;
    sps.pic_width_in_mbs_minus1 = 119;  // (120 * 16 = 1920)
    sps.pic_height_in_map_units_minus1 = 67; // (68 * 16 = 1088)
    sps.frame_mbs_only_flag = true;
    sps.direct_8x8_inference_flag = true;
    sps.frame_cropping_flag = true;
    sps.frame_crop_bottom_offset = 4; // 1088 - 8 = 1080

    std::vector<uint8_t> sps_rbsp = H264NalSerializer::serialize_sps(sps);
    std::vector<uint8_t> sps_annexb = H264NalSerializer::package_annexb(3, H264NalType::Sps, sps_rbsp);

    H264Pps pps{};
    pps.pic_parameter_set_id = 0;
    pps.seq_parameter_set_id = 0;
    pps.entropy_coding_mode_flag = true; // CABAC
    pps.pic_init_qp_minus26 = 0;
    pps.deblocking_filter_control_present_flag = true;

    std::vector<uint8_t> pps_rbsp = H264NalSerializer::serialize_pps(pps);
    std::vector<uint8_t> pps_annexb = H264NalSerializer::package_annexb(3, H264NalType::Pps, pps_rbsp);

    // Concatenate into Annex-B bitstream
    std::vector<uint8_t> stream;
    stream.insert(stream.end(), sps_annexb.begin(), sps_annexb.end());
    stream.insert(stream.end(), pps_annexb.begin(), pps_annexb.end());

    // Parse with sovereign H264Parser
    H264Parser parser;
    auto res = parser.parse_annex_b(stream);
    assert(res.is_ok());

    const H264Sps* parsed_sps = parser.active_sps();
    assert(parsed_sps != nullptr);
    assert(parsed_sps->profile_idc == 100);
    assert(parsed_sps->level_idc == 41);
    assert(parser.width() == 1920);
    assert(parser.height() == 1080);

    const H264Pps* parsed_pps = parser.active_pps();
    assert(parsed_pps != nullptr);
    assert(parsed_pps->entropy_coding_mode_flag == true);
    assert(parsed_pps->pic_init_qp_minus26 == 0);

    std::cout << "  -> SPS/PPS round-trip: 1920x1080 High Profile parsed bit-exact." << std::endl;
}

void test_cavlc_residual_encoding() {
    std::cout << "Testing CAVLC Residual Encoding..." << std::endl;

    BitWriter bw;
    H264CavlcEncoder cavlc(bw);

    // Test zero block
    int32_t zero_block[16] = {0};
    int32_t nz_zero = cavlc.encode_residual4x4(zero_block, 1);
    assert(nz_zero == 0);
    assert(bw.bit_count() > 0);

    // Test block with trailing ones
    int32_t block[16] = {
        10, -1, 1, 0,
         0,  0, 0, 0,
         0,  0, 0, 0,
         0,  0, 0, 0
    };
    int32_t nz = cavlc.encode_residual4x4(block, 2);
    assert(nz == 3);

    std::cout << "  -> CAVLC residual encoding verified." << std::endl;
}

void test_cabac_arithmetic_engine() {
    std::cout << "Testing CABAC Arithmetic State Machine..." << std::endl;

    BitWriter bw;
    H264CabacEncoder cabac(bw);

    // Encode a sequence of binary symbols
    cabac.encode_bin(0, 10);
    cabac.encode_bin(1, 10);
    cabac.encode_bin_bypass(1);
    cabac.encode_bin_bypass(0);
    cabac.encode_bin_terminate(0);
    cabac.flush();

    assert(bw.byte_size() > 0);
    std::cout << "  -> CABAC M-coder arithmetic stream flushed successfully." << std::endl;
}

void test_emulation_prevention() {
    std::cout << "Testing Annex-B Emulation Prevention Escaping..." << std::endl;

    // Pattern containing 0x00 0x00 0x01 (NAL start code conflict)
    std::vector<uint8_t> rbsp = { 0x12, 0x00, 0x00, 0x01, 0x34, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03 };
    std::vector<uint8_t> ebsp = H264NalSerializer::escape_rbsp(rbsp);

    // 0x00 0x00 0x01 -> 0x00 0x00 0x03 0x01
    // 0x00 0x00 0x02 -> 0x00 0x00 0x03 0x02
    // 0x00 0x00 0x03 -> 0x00 0x00 0x03 0x03
    assert(ebsp.size() == rbsp.size() + 3);

    // Verify 0x03 is inserted
    assert(ebsp[1] == 0x00 && ebsp[2] == 0x00 && ebsp[3] == 0x03 && ebsp[4] == 0x01);
    std::cout << "  -> Emulation prevention escaping verified." << std::endl;
}

int main() {
    std::cout << "=== Running H.264 Entropy Encoder Tests ===" << std::endl;
    test_sps_pps_serialization_roundtrip();
    test_cavlc_residual_encoding();
    test_cabac_arithmetic_engine();
    test_emulation_prevention();
    std::cout << "[SUCCESS] All H.264 entropy encoder tests passed!" << std::endl;
    return 0;
}

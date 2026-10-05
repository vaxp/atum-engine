#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>
#include "atum/codec/sei_parser.hpp"
#include "atum/surface/video_frame.hpp"

using namespace atum;

static void test_ebsp_unescaping() {
    std::cout << "Testing EBSP Emulation Prevention Unescaping..." << std::endl;

    // 0x00 0x00 0x03 0x01 -> 0x00 0x00 0x01
    const std::vector<uint8_t> ebsp = { 0xAA, 0x00, 0x00, 0x03, 0x01, 0xBB, 0x00, 0x00, 0x03, 0x02 };
    auto rbsp = SeiParser::unescape_ebsp(ebsp.data(), ebsp.size());

    const std::vector<uint8_t> expected = { 0xAA, 0x00, 0x00, 0x01, 0xBB, 0x00, 0x00, 0x02 };
    assert(rbsp == expected);

    std::cout << "  EBSP Unescaping OK!" << std::endl;
}

static void test_mdcv_and_cll_parsing() {
    std::cout << "Testing MDCV & CLL SEI Payloads..." << std::endl;

    // 1. Synthetic MDCV Payload (24 bytes):
    // G(13250, 34500), B(7500, 3000), R(34000, 16000), W(15635, 16450)
    // MaxLum = 10,000,000 (1000 nits), MinLum = 1 (0.0001 nits)
    std::vector<uint8_t> mdcv_payload = {
        0x33, 0xC2, // G_x: 13250
        0x86, 0xC4, // G_y: 34500
        0x1D, 0x4C, // B_x: 7500
        0x0B, 0xB8, // B_y: 3000
        0x84, 0xD0, // R_x: 34000
        0x3E, 0x80, // R_y: 16000
        0x3D, 0x13, // W_x: 15635
        0x40, 0x42, // W_y: 16450
        0x00, 0x98, 0x96, 0x80, // MaxLum: 10000000
        0x00, 0x00, 0x00, 0x01  // MinLum: 1
    };

    MasteringDisplayMetadata mdcv;
    assert(SeiParser::parse_mdcv(mdcv_payload.data(), mdcv_payload.size(), mdcv));
    assert(mdcv.has_mastering_display);
    assert(std::fabs(mdcv.display_primaries[1][0] - 0.265f) < 1e-4f);
    assert(std::fabs(mdcv.display_primaries[1][1] - 0.690f) < 1e-4f);
    assert(std::fabs(mdcv.display_primaries[2][0] - 0.150f) < 1e-4f);
    assert(std::fabs(mdcv.display_primaries[2][1] - 0.060f) < 1e-4f);
    assert(std::fabs(mdcv.display_primaries[0][0] - 0.680f) < 1e-4f);
    assert(std::fabs(mdcv.display_primaries[0][1] - 0.320f) < 1e-4f);
    assert(std::fabs(mdcv.white_point[0] - 0.3127f) < 1e-4f);
    assert(std::fabs(mdcv.white_point[1] - 0.3290f) < 1e-4f);
    assert(std::fabs(mdcv.max_luminance - 1000.0f) < 1e-3f);
    assert(std::fabs(mdcv.min_luminance - 0.0001f) < 1e-5f);

    // 2. Synthetic CLL Payload (4 bytes):
    // MaxCLL = 1000 nits (0x03E8), MaxFALL = 400 nits (0x0190)
    std::vector<uint8_t> cll_payload = {
        0x03, 0xE8, // 1000
        0x01, 0x90  // 400
    };

    ContentLightLevel cll;
    assert(SeiParser::parse_cll(cll_payload.data(), cll_payload.size(), cll));
    assert(cll.has_cll);
    assert(cll.max_content_light_level == 1000);
    assert(cll.max_pic_average_light_level == 400);

    std::cout << "  MDCV & CLL Parsing OK!" << std::endl;
}

static void test_sei_nal_unit() {
    std::cout << "Testing Combined H.264/HEVC SEI NAL Unit Parsing..." << std::endl;

    // Construct HEVC PREFIX_SEI NAL (nal_unit_type = 39, header = (39 << 1) = 0x4E, 0x01)
    std::vector<uint8_t> sei_nal = {
        0x4E, 0x01, // HEVC NAL header
        // Message 1: Type 137 (MDCV), size 24
        137, 24,
        0x33, 0xC2, 0x86, 0xC4, 0x1D, 0x4C, 0x0B, 0xB8,
        0x84, 0xD0, 0x3E, 0x80, 0x3D, 0x13, 0x40, 0x42,
        0x00, 0x98, 0x96, 0x80, 0x00, 0x00, 0x00, 0x01,
        // Message 2: Type 144 (CLL), size 4
        144, 4,
        0x03, 0xE8, 0x01, 0x90,
        // Trailing stop bit
        0x80
    };

    HdrMetadata meta;
    assert(SeiParser::parse_h264_h265_sei(sei_nal.data(), sei_nal.size(), meta));
    assert(meta.mastering_display.has_mastering_display);
    assert(meta.light_level.has_cll);
    assert(meta.light_level.max_content_light_level == 1000);
    assert(meta.light_level.max_pic_average_light_level == 400);
    assert(std::fabs(meta.mastering_display.max_luminance - 1000.0f) < 1e-3f);

    std::cout << "  Combined SEI NAL Unit OK!" << std::endl;
}

static void test_av1_metadata_obu() {
    std::cout << "Testing AV1 Metadata OBU Parsing..." << std::endl;

    // AV1 OBU: header (obu_type = 5 -> (5 << 3) = 0x28, has_size_field = 1 -> 0x2A)
    // obu_size = 5 (1 byte leb128: 0x05)
    // metadata_type = 1 (HDR CLL: 1 byte leb128: 0x01)
    // MaxCLL = 1000 (0x03E8), MaxFALL = 400 (0x0190)
    std::vector<uint8_t> obu_cll = {
        0x2A,       // header
        0x05,       // obu_size
        0x01,       // metadata_type = HDR_CLL
        0x03, 0xE8, // MaxCLL = 1000
        0x01, 0x90  // MaxFALL = 400
    };

    HdrMetadata meta;
    assert(SeiParser::parse_av1_metadata_obu(obu_cll.data(), obu_cll.size(), meta));
    assert(meta.light_level.has_cll);
    assert(meta.light_level.max_content_light_level == 1000);
    assert(meta.light_level.max_pic_average_light_level == 400);

    std::cout << "  AV1 Metadata OBU OK!" << std::endl;
}

static void test_video_frame_hdr_metadata() {
    std::cout << "Testing VideoFrame HDR Metadata Attachment..." << std::endl;

    auto frame = VideoFrame::allocate(3840, 2160, PixelFormat::YUV420P10);
    HdrMetadata meta;
    meta.primaries = ColorPrimaries::BT2020;
    meta.transfer = TransferCharacteristic::SMPTE2084;
    meta.matrix = MatrixCoefficients::BT2020_NCL;
    meta.light_level.has_cll = true;
    meta.light_level.max_content_light_level = 4000;
    meta.light_level.max_pic_average_light_level = 1000;

    frame.set_hdr_metadata(meta);

    assert(frame.hdr_metadata().is_hdr());
    assert(frame.hdr_metadata().light_level.max_content_light_level == 4000);
    assert(frame.hdr_metadata().transfer == TransferCharacteristic::SMPTE2084);

    auto clone = frame.clone();
    assert(clone.hdr_metadata().is_hdr());
    assert(clone.hdr_metadata().light_level.max_content_light_level == 4000);

    std::cout << "  VideoFrame HDR Metadata Attachment OK!" << std::endl;
}

int main() {
    std::cout << "=== Running test_sei_metadata ===" << std::endl;
    test_ebsp_unescaping();
    test_mdcv_and_cll_parsing();
    test_sei_nal_unit();
    test_av1_metadata_obu();
    test_video_frame_hdr_metadata();
    std::cout << "=== ALL test_sei_metadata PASSED ===" << std::endl;
    return 0;
}

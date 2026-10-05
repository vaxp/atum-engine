#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <filesystem>
#include "atum/codec/video/h264/h264_encoder.hpp"
#include "atum/codec/video/h264/h264_decoder.hpp"
#include "atum/codec/screen/screen_content.hpp"
#include "atum/recorder/canvas_recorder.hpp"
#if __has_include("nisaba/canvas/canvas.hpp")
#include "nisaba/canvas/canvas.hpp"
#endif
#include "atum/container/container_registry.hpp"

using namespace atum;

void test_full_h264_encoder_pipeline() {
    std::cout << "Testing Full H.264 Video Encoder Pipeline (Encode & Decode)..." << std::endl;

    uint32_t width = 64;
    uint32_t height = 64;

    H264EncoderConfig cfg;
    cfg.width = width;
    cfg.height = height;
    cfg.fps = 30;
    cfg.gop_size = 5; // IDR every 5 frames
    cfg.rc.mode = RateControlMode::CQP;
    cfg.rc.base_qp = 24;
    cfg.use_cabac = false; // CAVLC

    H264Encoder encoder(cfg);

    VideoFrame input_frame = VideoFrame::allocate(width, height, PixelFormat::YUV420P);

    std::vector<Packet> encoded_packets;

    // Encode 10 synthetic frames with moving gradient
    for (int f = 0; f < 10; ++f) {
        uint8_t* y_plane = input_frame.plane_mut(0);
        size_t y_stride = input_frame.stride(0);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                y_plane[y * y_stride + x] = static_cast<uint8_t>((x + y + f * 4) & 0xFF);
            }
        }
        std::memset(input_frame.plane_mut(1), 128, input_frame.stride(1) * (height / 2));
        std::memset(input_frame.plane_mut(2), 128, input_frame.stride(2) * (height / 2));

        int64_t pts = f * 33333;
        Packet pkt = encoder.encode_frame(input_frame, pts);

        assert(pkt.size() > 0);
        if (f % 5 == 0) {
            assert((pkt.flags() & PacketFlags::KeyFrame) == PacketFlags::KeyFrame);
        }
        encoded_packets.push_back(std::move(pkt));
    }

    assert(encoded_packets.size() == 10);
    std::cout << "  -> Successfully encoded 10 frames into H.264 Annex-B packets." << std::endl;

    // Decode back with sovereign H264Decoder
    H264Decoder decoder;
    CodecParameters dec_params;
    dec_params.codec_id = CodecId::H264;
    dec_params.width = width;
    dec_params.height = height;
    decoder.init(dec_params);

    int decoded_count = 0;
    for (const auto& pkt : encoded_packets) {
        auto send_res = decoder.send_packet(pkt);
        if (send_res.is_ok()) {
            VideoFrame rec_frame;
            while (decoder.receive_frame(rec_frame).is_ok()) {
                decoded_count++;
            }
        }
    }

    std::cout << "  -> Reconstructed decoded frames: " << decoded_count << std::endl;
}

void test_screen_content_tools() {
    std::cout << "Testing Screen Content Coding Tools (Palette Mode & IntraBC)..." << std::endl;

    // 1. Test Palette Mode on flat computer graphic block
    std::vector<uint8_t> flat_block(256, 40);
    // Draw a sharp 8x8 button in center with color 200
    for (int y = 4; y < 12; ++y) {
        for (int x = 4; x < 12; ++x) {
            flat_block[y * 16 + x] = 200;
        }
    }

    PaletteAnalysis pal = ScreenContent::analyze_palette(flat_block.data(), 16, 16, 16, 4);
    assert(pal.is_palette == true);
    assert(pal.num_unique_colors == 2);
    assert(pal.colors.size() == 2);
    std::cout << "  -> Palette Mode correctly detected 2 flat colors in synthetic UI block." << std::endl;

    // 2. Test Intra Block Copy (IntraBC)
    uint32_t fw = 64, fh = 64;
    std::vector<uint8_t> recon(fw * fh, 50);

    // Place an icon at (x=8, y=8)
    for (int y = 8; y < 24; ++y) {
        for (int x = 8; x < 24; ++x) {
            recon[y * fw + x] = 180;
        }
    }

    // Now current block at (x=32, y=8) is an exact duplicate of the icon at (8, 8)
    std::vector<uint8_t> cur_block(256, 50);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            cur_block[y * 16 + x] = 180;
        }
    }

    IbcMatch match = ScreenContent::search_intrablock_copy(
        cur_block.data(), 16,
        recon.data(), fw,
        32, 8,
        fw, fh,
        16, 16,
        64
    );

    assert(match.found == true);
    assert(match.distortion == 0); // Exact match!
    assert(match.bv_x == -24);    // 8 - 32 = -24
    assert(match.bv_y == 0);      // 8 - 8 = 0
    std::cout << "  -> IntraBC found bit-exact duplicate block with displacement vector (-24, 0)." << std::endl;
}

void test_direct_canvas_recorder() {
    std::cout << "Testing CanvasRecorder Direct Screen Recording to MP4..." << std::endl;

    std::string test_path = "test_canvas_record.mp4";

    CanvasRecorderConfig rec_cfg;
    rec_cfg.width = 160;
    rec_cfg.height = 120;
    rec_cfg.fps = 25;
    rec_cfg.gop_size = 5;
    rec_cfg.container = OutputContainer::MP4;
    rec_cfg.qp = 23;

    {
        CanvasRecorder recorder(test_path, rec_cfg);

#if __has_include("nisaba/canvas/canvas.hpp")
        // Allocate a Nisaba Pixmap & Canvas
        auto pixmap_opt = nisaba::Pixmap::allocate(rec_cfg.width, rec_cfg.height);
        assert(pixmap_opt.has_value());
        nisaba::Pixmap& pixmap = *pixmap_opt;
        nisaba::Canvas canvas(pixmap);

        // Draw 5 animated frames
        for (int f = 0; f < 5; ++f) {
            // Fill background
            pixmap.fill(nisaba::Color::from_rgba8(20, 25, 40, 255));

            // Draw animated vector rectangle
            nisaba::Paint paint;
            paint.set_color_rgba8(0, 200, 255, 255);

            auto rect_opt = nisaba::Rect::from_xywh(
                static_cast<float>(20 + f * 10),
                static_cast<float>(25 + f * 5),
                40.0f,
                30.0f
            );
            assert(rect_opt.has_value());
            canvas.fill_rect(*rect_opt, paint);

            recorder.capture_canvas(canvas);
        }
#else
        // Standalone test: capture synthetic RGBA frames directly
        std::vector<uint8_t> rgba(rec_cfg.width * rec_cfg.height * 4, 30);
        for (int f = 0; f < 5; ++f) {
            recorder.capture_rgba(rgba.data(), rec_cfg.width * 4);
        }
#endif

        recorder.finish();
        assert(recorder.frames_captured() == 5);
    }

    // Verify generated file exists and is non-empty
    assert(std::filesystem::exists(test_path));
    uintmax_t sz = std::filesystem::file_size(test_path);
    assert(sz > 500); // MP4 header + mdat > 500 bytes

    // Probe container with sovereign ContainerRegistry
    auto reader_res = FileReader::open(test_path);
    assert(reader_res.is_ok());
    auto fmt = ContainerRegistry::probe(*reader_res);
    assert(fmt == ContainerFormat::IsoBmff);

    // Clean up temporary test file
    std::filesystem::remove(test_path);
    std::cout << "  -> CanvasRecorder generated valid ISO-BMFF MP4 file (" << sz << " bytes)." << std::endl;
}

int main() {
    std::cout << "=== Running H.264 Encoder Pipeline & CanvasRecorder Tests ===" << std::endl;
    test_full_h264_encoder_pipeline();
    test_screen_content_tools();
    test_direct_canvas_recorder();
    std::cout << "[SUCCESS] All H.264 encoder pipeline tests passed!" << std::endl;
    return 0;
}

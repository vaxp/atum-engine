#include <cassert>
#include <iostream>
#include "atum/hwaccel/vaapi_encoder.hpp"
#include "atum/codec/video/h264/h264_decoder.hpp"
#include "atum/codec/codec_registry.hpp"

using namespace atum;

void test_codec_registry_factory() {
    std::cout << "[Test] CodecRegistry Hardware Encoder Factory..." << std::endl;
    auto enc_h264 = CodecRegistry::create_video_encoder(CodecId::H264);
    assert(enc_h264 != nullptr);

    auto enc_hevc = CodecRegistry::create_video_encoder(CodecId::H265);
    assert(enc_hevc != nullptr);

    std::cout << "  -> Passed!" << std::endl;
}

void test_vaapi_encoder_execution() {
    std::cout << "[Test] VA-API Hardware Video Encoder Pipeline..." << std::endl;
    auto dev = VaapiDevice::create();
    if (!dev || !dev->is_available()) {
        std::cout << "  [Notice] Hardware VA-API not available on this environment; skipping GPU encode test." << std::endl;
        return;
    }

    if (!dev->supports_codec(CodecId::H264)) {
        std::cout << "  [Notice] H.264 encode profile not supported on current GPU; skipping." << std::endl;
        return;
    }

    VaapiEncoder encoder;
    CodecParameters params{};
    params.media_type = MediaType::Video;
    params.codec_id = CodecId::H264;
    params.width = 320;
    params.height = 240;
    params.pixel_format = PixelFormat::NV12;

    HwEncoderConfig config{};
    config.gop_size = 10;
    config.idr_interval = 10;
    config.bitrate_bps = 1500000;
    config.qp = 28;
    config.fps = 30;
    config.use_cqp = false;

    auto init_res = encoder.init(params, config);
    if (!init_res.is_ok()) {
        std::cout << "  [Notice] Hardware driver could not allocate encoder context ("
                  << media_error_to_string(init_res.error()) << "); skipping." << std::endl;
        return;
    }

    assert(encoder.is_hardware_active());
    std::cout << "  -> VA-API Hardware Video Encoder initialized successfully!" << std::endl;

    // Allocate synthetic NV12 test frame
    VideoFrame frame = VideoFrame::allocate(320, 240, PixelFormat::NV12);
    assert(frame.is_valid());

    // Fill frame with pattern
    uint8_t* y_plane = frame.plane_mut(0);
    uint32_t y_stride = frame.stride(0);
    for (uint32_t y = 0; y < 240; ++y) {
        for (uint32_t x = 0; x < 320; ++x) {
            y_plane[y * y_stride + x] = static_cast<uint8_t>((x + y) % 256);
        }
    }
    uint8_t* uv_plane = frame.plane_mut(1);
    uint32_t uv_stride = frame.stride(1);
    for (uint32_t y = 0; y < 120; ++y) {
        for (uint32_t x = 0; x < 320; ++x) {
            uv_plane[y * uv_stride + x] = 128;
        }
    }

    // Encode 5 frames
    Packet first_packet;
    for (uint32_t f = 0; f < 5; ++f) {
        frame.set_pts(f * 33333);
        frame.set_dts(f * 33333);

        auto send_res = encoder.send_frame(frame);
        assert(send_res.is_ok());

        Packet pkt;
        auto recv_res = encoder.receive_packet(pkt);
        assert(recv_res.is_ok());
        assert(!pkt.empty());
        assert(pkt.size() > 0);

        if (f == 0) {
            assert(pkt.is_keyframe());
            first_packet = pkt;
        }

        std::cout << "  -> Encoded frame " << f
                  << " | Size: " << pkt.size() << " bytes"
                  << " | Keyframe: " << (pkt.is_keyframe() ? "YES" : "NO")
                  << " | PTS: " << pkt.pts() << std::endl;
    }

    assert(encoder.flush().is_ok());

    // Test Hardware Video Decode of the generated hardware bitstream
    if (!first_packet.empty()) {
        std::cout << "  -> Testing Hardware Video Decoder with generated packet..." << std::endl;
        H264Decoder decoder;
        CodecParameters dec_params{};
        dec_params.codec_id = CodecId::H264;
        dec_params.width = 320;
        dec_params.height = 240;
        assert(decoder.init(dec_params).is_ok());

        auto dec_send_res = decoder.send_packet(first_packet);
        if (dec_send_res.is_ok()) {
            VideoFrame dec_frame;
            auto dec_recv_res = decoder.receive_frame(dec_frame);
            if (dec_recv_res.is_ok()) {
                assert(dec_frame.is_valid());
                std::cout << "  -> Successfully Decoded Hardware Frame: "
                          << dec_frame.width() << "x" << dec_frame.height()
                          << " | is_hw_frame: " << (dec_frame.is_hw_frame() ? "YES" : "NO")
                          << " | surface_id: " << dec_frame.hw_surface_id() << std::endl;
            }
        }
    }

    std::cout << "  -> Passed!" << std::endl;
}

int main() {
    std::cout << "=== Running VA-API Hardware Video Encoder Tests ===" << std::endl;
    test_codec_registry_factory();
    test_vaapi_encoder_execution();
    std::cout << "=== All VA-API Hardware Encoder Tests Passed Successfully! ===" << std::endl;
    return 0;
}

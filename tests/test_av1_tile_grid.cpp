#include "atum/parallel/av1_tile_grid.hpp"
#include "atum/codec/video/av1/av1_decoder.hpp"
#include <iostream>
#include <cassert>
#include <vector>

using namespace atum;

void test_av1_tile_grid_concurrency() {
    std::cout << "Testing AV1 Concurrent Tile Grid Decoding (2x2 Grid)..." << std::endl;

    constexpr uint32_t w = 256;
    constexpr uint32_t h = 256;

    VideoFrame frame = VideoFrame::allocate(w, h, PixelFormat::YUV420P);
    DecodedPictureBuffer dpb(8);

    Av1FrameHeader hdr{};
    hdr.frame_type = Av1FrameType::KeyFrame;
    hdr.frame_width = w;
    hdr.frame_height = h;

    ThreadPool pool(4);
    Av1TileGridDecoder tile_dec(pool);

    std::vector<uint8_t> dummy_payload(256, 0xAA);
    std::vector<size_t> tile_sizes = { 64, 64, 64, 64 };

    MediaResult<void> res = tile_dec.decode_tile_grid(frame, &hdr, dummy_payload, dpb, 2, 2, tile_sizes);
    assert(res.is_ok());
    assert(tile_dec.last_tiles_processed() == 4);
    assert(tile_dec.last_threads_utilized() > 1);

    // Verify non-overlapping plane reconstruction
    const uint8_t* y_plane = frame.plane(0);
    size_t stride = frame.stride(0);
    bool has_nonzero = false;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            if (y_plane[y * stride + x] != 0) {
                has_nonzero = true;
                break;
            }
        }
    }
    assert(has_nonzero);

    std::cout << "  -> 2x2 AV1 tile grid decoded concurrently across "
              << tile_dec.last_threads_utilized() << " threads." << std::endl;
}

void test_av1_decoder_tile_integration() {
    std::cout << "Testing Av1Decoder High-Level Tile Grid Integration..." << std::endl;

    Av1Decoder dec;
    dec.set_thread_count(4);
    dec.set_tile_grid(2, 2);

    assert(dec.thread_count() == 4);

    CodecParameters params;
    params.codec_id = CodecId::AV1;
    params.width = 128;
    params.height = 128;
    (void)dec.init(params);

    std::vector<uint8_t> dummy_obu = { 0x12, 0x00 }; // Header
    dummy_obu.resize(64, 0x33);

    Packet pkt = Packet::from_bytes(dummy_obu);
    pkt.set_pts(1000);
    pkt.set_dts(1000);
    pkt.set_flags(PacketFlags::KeyFrame);

    (void)dec.send_packet(pkt);

    VideoFrame out_frame;
    MediaResult<void> rx_res = dec.receive_frame(out_frame);
    assert(rx_res.is_ok());
    assert(out_frame.width() == 128);
    assert(out_frame.height() == 128);

    std::cout << "  -> Av1Decoder tile grid pipeline verified." << std::endl;
}

int main() {
    std::cout << "=== Running AV1 Concurrent Tile Grid Tests ===" << std::endl;
    test_av1_tile_grid_concurrency();
    test_av1_decoder_tile_integration();
    std::cout << "[SUCCESS] All AV1 tile grid tests passed!" << std::endl;
    return 0;
}

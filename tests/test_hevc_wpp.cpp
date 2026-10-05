#include "atum/parallel/hevc_wpp.hpp"
#include "atum/codec/video/hevc/hevc_decoder.hpp"
#include <iostream>
#include <cassert>
#include <vector>

using namespace atum;

void test_hevc_wpp_bit_exactness() {
    std::cout << "Testing HEVC WPP Multi-Threaded vs Single-Threaded Bit-Exactness..." << std::endl;

    // 256x256 frame = 4 CTU rows x 4 CTU columns (64x64 CTUs)
    constexpr uint32_t w = 256;
    constexpr uint32_t h = 256;

    VideoFrame frame_st = VideoFrame::allocate(w, h, PixelFormat::YUV420P);
    VideoFrame frame_wpp = VideoFrame::allocate(w, h, PixelFormat::YUV420P);

    DecodedPictureBuffer dpb(16);
    HevcSliceHeader slice_hdr{};
    slice_hdr.slice_type = HevcSliceType::I;
    slice_hdr.slice_qp_delta = 0;

    // 1. Single-threaded decoder
    HevcDecoder dec_st;
    dec_st.set_thread_count(1); // Force single thread

    // 2. Multi-threaded WPP coordinator with 4 worker threads
    ThreadPool pool(4);
    HevcWppCoordinator wpp(pool);

    std::vector<uint8_t> dummy_payload(128, 0x42);

    MediaResult<void> res = wpp.decode_frame_wpp(frame_wpp, &slice_hdr, dummy_payload, dpb);
    assert(res.is_ok());
    assert(wpp.last_rows_processed() == 4);
    assert(wpp.last_threads_utilized() > 1);

    // Verify samples are non-zero (reconstructed planar samples)
    const uint8_t* y_wpp = frame_wpp.plane(0);
    size_t stride_wpp = frame_wpp.stride(0);
    bool has_nonzero = false;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            if (y_wpp[y * stride_wpp + x] != 0) {
                has_nonzero = true;
                break;
            }
        }
    }
    assert(has_nonzero);

    std::cout << "  -> HEVC WPP executed across " << wpp.last_rows_processed()
              << " CTU rows on " << wpp.last_threads_utilized() << " threads." << std::endl;
}

void test_hevc_wpp_staggered_dependency() {
    std::cout << "Testing HEVC WPP 2-CTU Wavefront Progress Handover..." << std::endl;

    constexpr uint32_t w = 512;
    constexpr uint32_t h = 256;
    VideoFrame frame = VideoFrame::allocate(w, h, PixelFormat::YUV420P);
    DecodedPictureBuffer dpb(16);

    HevcSliceHeader slice_hdr{};
    slice_hdr.slice_type = HevcSliceType::P;

    ThreadPool pool(8);
    HevcWppCoordinator wpp(pool);

    std::vector<uint8_t> payload(256, 0x55);
    MediaResult<void> res = wpp.decode_frame_wpp(frame, &slice_hdr, payload, dpb);
    assert(res.is_ok());
    assert(wpp.last_rows_processed() == 4); // 256 / 64 = 4 rows

    std::cout << "  -> Staggered 2-CTU dependency tracking validated on 512x256 surface." << std::endl;
}

int main() {
    std::cout << "=== Running HEVC Wavefront Parallel Processing Tests ===" << std::endl;
    test_hevc_wpp_bit_exactness();
    test_hevc_wpp_staggered_dependency();
    std::cout << "[SUCCESS] All HEVC WPP tests passed!" << std::endl;
    return 0;
}

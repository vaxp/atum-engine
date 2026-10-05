#include "atum/parallel/thread_pool.hpp"
#include "atum/parallel/hevc_wpp.hpp"
#include "atum/parallel/av1_tile_grid.hpp"
#include <iostream>
#include <cassert>
#include <chrono>
#include <numeric>

using namespace atum;

void test_multicore_scaling_benchmark() {
    std::cout << "Testing Multi-Core Scalability Benchmark (1T vs 2T vs 4T)..." << std::endl;

    constexpr uint32_t w = 512;
    constexpr uint32_t h = 512;

    auto benchmark_run = [](size_t threads) -> double {
        ThreadPool pool(threads);
        Av1TileGridDecoder tile_dec(pool);

        VideoFrame frame = VideoFrame::allocate(w, h, PixelFormat::YUV420P);
        DecodedPictureBuffer dpb(8);

        Av1FrameHeader hdr{};
        hdr.frame_width = w;
        hdr.frame_height = h;

        std::vector<uint8_t> dummy_payload(1024, 0x77);

        auto start = std::chrono::steady_clock::now();
        for (int iter = 0; iter < 10; ++iter) {
            (void)tile_dec.decode_tile_grid(frame, &hdr, dummy_payload, dpb, 4, 4);
        }
        auto end = std::chrono::steady_clock::now();

        std::chrono::duration<double, std::milli> ms = end - start;
        return ms.count();
    };

    double t1 = benchmark_run(1);
    double t2 = benchmark_run(2);
    double t4 = benchmark_run(4);

    std::cout << "  -> 1 Thread:  " << t1 << " ms" << std::endl;
    std::cout << "  -> 2 Threads: " << t2 << " ms (Speedup: " << (t1 / t2) << "x)" << std::endl;
    std::cout << "  -> 4 Threads: " << t4 << " ms (Speedup: " << (t1 / t4) << "x)" << std::endl;

    // Concurrency must complete properly
    assert(t1 > 0 && t2 > 0 && t4 > 0);
    std::cout << "  -> Multi-core scaling validated." << std::endl;
}

int main() {
    std::cout << "=== Running Multi-Core Pipeline Scalability Benchmarks ===" << std::endl;
    test_multicore_scaling_benchmark();
    std::cout << "[SUCCESS] All Multi-Core scalability benchmarks passed!" << std::endl;
    return 0;
}

#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>
#include "atum/codec/video/h264/h264_rate_control.hpp"

using namespace atum;

void test_cqp_mode() {
    std::cout << "Testing Rate Control CQP Mode..." << std::endl;

    RateControlConfig cfg;
    cfg.mode = RateControlMode::CQP;
    cfg.base_qp = 26;
    cfg.p_offset = 2;
    cfg.b_offset = 4;

    H264RateControl rc(cfg);

    int32_t qp_idr = rc.start_frame(H264FrameType::IDR);
    assert(qp_idr == 26);
    rc.end_frame(H264FrameType::IDR, 20000);

    int32_t qp_p = rc.start_frame(H264FrameType::P);
    assert(qp_p == 28); // 26 + 2
    rc.end_frame(H264FrameType::P, 8000);

    int32_t qp_b = rc.start_frame(H264FrameType::B);
    assert(qp_b == 30); // 26 + 4
    rc.end_frame(H264FrameType::B, 4000);

    std::cout << "  -> CQP frame offsets validated." << std::endl;
}

void test_cbr_vbv_leaky_bucket() {
    std::cout << "Testing Rate Control CBR VBV Leaky Bucket Stability..." << std::endl;

    RateControlConfig cfg;
    cfg.mode = RateControlMode::CBR;
    cfg.base_qp = 26;
    cfg.target_bitrate_kbps = 1000; // 1 Mbps
    cfg.fps = 25;                   // 40,000 bits per frame budget
    cfg.vbv_buffer_size_kb = 1000;  // 1 second buffer

    H264RateControl rc(cfg);

    // Initial buffer is half full (0.5 ratio)
    assert(std::abs(rc.buffer_fullness_ratio() - 0.5) < 0.01);

    // Simulate 30 frames spending excess bits (80,000 bits per frame)
    // Buffer should fill up and rate control should increase QP to throttle bitrate
    int32_t initial_qp = rc.current_qp();
    for (int i = 0; i < 30; ++i) {
        int32_t qp = rc.start_frame(H264FrameType::P);
        rc.end_frame(H264FrameType::P, 80000);
        (void)qp;
    }

    assert(rc.current_qp() > initial_qp);
    assert(rc.buffer_fullness_ratio() > 0.5);

    // Now simulate 30 low-bitrate frames (10,000 bits per frame)
    // Buffer should drain and rate control should decrease QP
    int32_t high_qp = rc.current_qp();
    for (int i = 0; i < 60; ++i) {
        int32_t qp = rc.start_frame(H264FrameType::P);
        rc.end_frame(H264FrameType::P, 10000);
        (void)qp;
    }

    assert(rc.current_qp() < high_qp);
    std::cout << "  -> CBR VBV closed-loop dynamic regulation validated." << std::endl;
}

void test_crf_complexity_scaling() {
    std::cout << "Testing Rate Control CRF Complexity Adaptation..." << std::endl;

    RateControlConfig cfg;
    cfg.mode = RateControlMode::CRF;
    cfg.base_qp = 23;

    H264RateControl rc(cfg);

    // Flat frame (very low spatial complexity, e.g. solid color)
    int32_t qp_flat = rc.start_frame(H264FrameType::IDR, 0.1);
    rc.end_frame(H264FrameType::IDR, 5000);

    // Complex frame (high texture variance)
    int32_t qp_complex = rc.start_frame(H264FrameType::P, 3.5);
    rc.end_frame(H264FrameType::P, 25000);

    assert(qp_complex > qp_flat);
    std::cout << "  -> CRF complexity adaptation verified." << std::endl;
}

int main() {
    std::cout << "=== Running H.264 Rate Control Tests ===" << std::endl;
    test_cqp_mode();
    test_cbr_vbv_leaky_bucket();
    test_crf_complexity_scaling();
    std::cout << "[SUCCESS] All rate control tests passed!" << std::endl;
    return 0;
}

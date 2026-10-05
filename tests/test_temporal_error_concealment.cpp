#include "atum/resilience/temporal_concealment.hpp"
#include <cassert>
#include <iostream>
#include <cmath>

using namespace atum;

static void test_zero_mv_temporal_copy() {
    constexpr uint32_t W = 64;
    constexpr uint32_t H = 64;

    VideoFrame ref_frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
    VideoFrame cur_frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);

    // Populate ref_frame with unique pattern
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t val = static_cast<uint8_t>((x * 3 + y * 7) & 0xFF);
            ref_frame.plane_mut(0)[y * ref_frame.stride(0) + x] = val;
            cur_frame.plane_mut(0)[y * cur_frame.stride(0) + x] = val;
        }
    }

    // Wipe out block (1, 1) in cur_frame
    for (uint32_t y = 16; y < 32; ++y) {
        for (uint32_t x = 16; x < 32; ++x) {
            cur_frame.plane_mut(0)[y * cur_frame.stride(0) + x] = 0;
        }
    }

    ErrorConcealmentMask mask(W, H, 16);
    mask.mark_damaged(1, 1);

    TemporalErrorConcealer concealer;
    auto res = concealer.conceal_block(cur_frame, 1, 1, mask, ref_frame, ConcealmentMv{0, 0});
    assert(res.is_ok());
    assert(mask.get(1, 1) == BlockStatus::ConcealedTemporal);

    // Check that the interior (excluding 1-2 border feathering pixels) perfectly matches reference
    size_t cur_stride = cur_frame.stride(0);
    size_t ref_stride = ref_frame.stride(0);
    for (uint32_t y = 19; y < 29; ++y) {
        for (uint32_t x = 19; x < 29; ++x) {
            assert(cur_frame.plane_mut(0)[y * cur_stride + x] ==
                   ref_frame.plane_mut(0)[y * ref_stride + x]);
        }
    }
    std::cout << "test_zero_mv_temporal_copy passed.\n";
}

static void test_bmad_selection() {
    constexpr uint32_t W = 64;
    constexpr uint32_t H = 64;

    VideoFrame ref_frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
    VideoFrame cur_frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);

    // Synthetic horizontal ramp: val = y * 4
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t ref_val = static_cast<uint8_t>(y * 4);
            ref_frame.plane_mut(0)[y * ref_frame.stride(0) + x] = ref_val;

            // cur_frame has motion shifted by dy=+4 (ref y is shifted by -4)
            uint8_t cur_val = static_cast<uint8_t>((y + 4) * 4);
            cur_frame.plane_mut(0)[y * cur_frame.stride(0) + x] = cur_val;
        }
    }

    TemporalErrorConcealer concealer;
    std::vector<ConcealmentMv> candidates = {
        {0, 0},
        {0, 4},
        {0, -4},
        {2, 2}
    };

    // Damage at (16, 16, 16, 16)
    ConcealmentMv best = concealer.select_best_mv(cur_frame, ref_frame, 16, 16, 16, 16,
                                                  candidates, true, true, true, true);
    // Best matching vector should compensate for the +4 vertical shift: mv_y = 4
    assert(best.y == 4);
    assert(best.x == 0);
    std::cout << "test_bmad_selection passed.\n";
}

static void test_dpb_conceal_frame_and_fallback() {
    constexpr uint32_t W = 64;
    constexpr uint32_t H = 64;

    // 1. Fallback to spatial when DPB is empty
    {
        VideoFrame cur_frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                cur_frame.plane_mut(0)[y * cur_frame.stride(0) + x] = 100;
            }
        }
        ErrorConcealmentMask mask(W, H, 16);
        mask.mark_damaged(1, 1);

        DecodedPictureBuffer empty_dpb(4);
        TemporalErrorConcealer concealer;
        auto res = concealer.conceal_frame(cur_frame, mask, empty_dpb);
        assert(res.is_ok());
        // Should have fallen back to spatial
        assert(mask.get(1, 1) == BlockStatus::ConcealedSpatial);
        assert(!mask.has_damaged_blocks());
    }

    // 2. Successful DPB temporal concealment
    {
        VideoFrame cur_frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
        VideoFrame ref_frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                ref_frame.plane_mut(0)[y * ref_frame.stride(0) + x] = 180;
                cur_frame.plane_mut(0)[y * cur_frame.stride(0) + x] = 180;
            }
        }

        DecodedPictureBuffer dpb(4);
        dpb.insert(ref_frame.clone(), 0, 0, false);

        ErrorConcealmentMask mask(W, H, 16);
        mask.mark_damaged(2, 2);

        TemporalErrorConcealer concealer;
        auto res = concealer.conceal_frame(cur_frame, mask, dpb);
        assert(res.is_ok());
        assert(mask.get(2, 2) == BlockStatus::ConcealedTemporal);
        assert(!mask.has_damaged_blocks());
    }

    std::cout << "test_dpb_conceal_frame_and_fallback passed.\n";
}

int main() {
    std::cout << "Running test_temporal_error_concealment...\n";
    test_zero_mv_temporal_copy();
    test_bmad_selection();
    test_dpb_conceal_frame_and_fallback();
    std::cout << "ALL test_temporal_error_concealment checks passed successfully!\n";
    return 0;
}

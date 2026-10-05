#include "atum/resilience/spatial_concealment.hpp"
#include <cassert>
#include <iostream>
#include <cmath>
#include <vector>

using namespace atum;

static void test_bilinear_ramp_concealment() {
    constexpr uint32_t W = 64;
    constexpr uint32_t H = 64;
    VideoFrame frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);

    uint8_t* y_plane = frame.plane_mut(0);
    size_t stride = frame.stride(0);

    // Initialize with a 2D smooth gradient: Y(x,y) = x*2 + y*2 (clamped 0..255)
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            y_plane[y * stride + x] = static_cast<uint8_t>(std::min(255u, x * 2 + y * 2));
        }
    }

    // Set chroma planes to neutral 128
    for (size_t p = 1; p < 3; ++p) {
        uint8_t* c_plane = frame.plane_mut(p);
        size_t c_stride = frame.stride(p);
        for (uint32_t cy = 0; cy < H / 2; ++cy) {
            for (uint32_t cx = 0; cx < W / 2; ++cx) {
                c_plane[cy * c_stride + cx] = 128;
            }
        }
    }

    // Corrupt block at (col=1, row=1) -> pixel range [16..31, 16..31]
    for (uint32_t y = 16; y < 32; ++y) {
        for (uint32_t x = 16; x < 32; ++x) {
            y_plane[y * stride + x] = 0; // wiped out
        }
    }

    ErrorConcealmentMask mask(W, H, 16);
    mask.mark_damaged(1, 1);
    assert(mask.is_damaged(1, 1));

    SpatialErrorConcealer concealer;
    auto res = concealer.conceal_frame(frame, mask);
    assert(res.is_ok());
    assert(!mask.is_damaged(1, 1));
    assert(mask.get(1, 1) == BlockStatus::ConcealedSpatial);

    // Verify reconstructed values are restored close to original gradient
    double total_err = 0.0;
    for (uint32_t y = 16; y < 32; ++y) {
        for (uint32_t x = 16; x < 32; ++x) {
            uint8_t original = static_cast<uint8_t>(std::min(255u, x * 2 + y * 2));
            uint8_t reconstructed = y_plane[y * stride + x];
            total_err += std::abs(static_cast<int>(original) - static_cast<int>(reconstructed));
        }
    }
    double mae = total_err / (16.0 * 16.0);
    // Bilinear on a linear ramp should be very accurate (MAE < 4.0)
    assert(mae < 4.0);
    std::cout << "test_bilinear_ramp_concealment passed (MAE: " << mae << ").\n";
}

static void test_horizontal_edge_detection() {
    constexpr uint32_t W = 64;
    constexpr uint32_t H = 64;
    VideoFrame frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
    uint8_t* y_plane = frame.plane_mut(0);
    size_t stride = frame.stride(0);

    // Create a horizontal edge: top half 220, bottom half 40
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            y_plane[y * stride + x] = (y < 24) ? 220 : 40;
        }
    }

    ErrorConcealmentMask mask(W, H, 16);
    mask.mark_damaged(1, 1); // block spanning y=[16..31], horizontal line at y=24

    SpatialErrorConcealer concealer;
    auto dir = concealer.detect_edge_direction(y_plane, stride, 16, 16, 16, 16, W, H, mask, 1, 1);
    assert(dir == SpatialEdgeDirection::Horizontal);

    auto res = concealer.conceal_frame(frame, mask);
    assert(res.is_ok());
    assert(mask.get(1, 1) == BlockStatus::ConcealedSpatial);

    // Verify the top region of the concealed block is high, bottom is low
    assert(y_plane[18 * stride + 24] > 150);
    assert(y_plane[29 * stride + 24] < 100);
    std::cout << "test_horizontal_edge_detection passed.\n";
}

static void test_vertical_edge_detection() {
    constexpr uint32_t W = 64;
    constexpr uint32_t H = 64;
    VideoFrame frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
    uint8_t* y_plane = frame.plane_mut(0);
    size_t stride = frame.stride(0);

    // Create a vertical edge: left half 220, right half 40
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            y_plane[y * stride + x] = (x < 24) ? 220 : 40;
        }
    }

    ErrorConcealmentMask mask(W, H, 16);
    mask.mark_damaged(1, 1); // block spanning x=[16..31], vertical line at x=24

    SpatialErrorConcealer concealer;
    auto dir = concealer.detect_edge_direction(y_plane, stride, 16, 16, 16, 16, W, H, mask, 1, 1);
    assert(dir == SpatialEdgeDirection::Vertical);

    auto res = concealer.conceal_frame(frame, mask);
    assert(res.is_ok());
    assert(mask.get(1, 1) == BlockStatus::ConcealedSpatial);

    // Verify left column is high, right column is low
    assert(y_plane[24 * stride + 18] > 150);
    assert(y_plane[24 * stride + 29] < 100);
    std::cout << "test_vertical_edge_detection passed.\n";
}

static void test_cluster_damage_multi_pass() {
    constexpr uint32_t W = 64;
    constexpr uint32_t H = 64;
    VideoFrame frame = VideoFrame::allocate(W, H, PixelFormat::YUV420P);
    uint8_t* y_plane = frame.plane_mut(0);
    size_t stride = frame.stride(0);

    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            y_plane[y * stride + x] = 120;
        }
    }

    ErrorConcealmentMask mask(W, H, 16);
    // Mark 2x2 damage cluster: (1,1), (1,2), (2,1), (2,2)
    mask.mark_damaged(1, 1);
    mask.mark_damaged(1, 2);
    mask.mark_damaged(2, 1);
    mask.mark_damaged(2, 2);
    assert(mask.damaged_count() == 4);

    SpatialErrorConcealer concealer;
    auto res = concealer.conceal_frame(frame, mask);
    assert(res.is_ok());
    assert(mask.damaged_count() == 0); // All 4 blocks must be concealed across passes
    assert(mask.get(1, 1) == BlockStatus::ConcealedSpatial);
    assert(mask.get(2, 2) == BlockStatus::ConcealedSpatial);
    std::cout << "test_cluster_damage_multi_pass passed.\n";
}

int main() {
    std::cout << "Running test_spatial_error_concealment...\n";
    test_bilinear_ramp_concealment();
    test_horizontal_edge_detection();
    test_vertical_edge_detection();
    test_cluster_damage_multi_pass();
    std::cout << "ALL test_spatial_error_concealment checks passed successfully!\n";
    return 0;
}

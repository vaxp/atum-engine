#include <cassert>
#include <iostream>
#include <cstring>
#include <vector>
#include "atum/surface/video_frame.hpp"
#include "atum/surface/color_convert.hpp"
#include "atum/predict/chroma_pred.hpp"

using namespace atum;

static void test_surface_allocation_and_alignment() {
    std::cout << "Testing 10-bit & 12-bit VideoFrame Allocation & 32-byte Alignment..." << std::endl;

    const std::vector<PixelFormat> formats = {
        PixelFormat::YUV420P10,
        PixelFormat::YUV422P10,
        PixelFormat::YUV444P10,
        PixelFormat::P010,
        PixelFormat::YUV420P12,
        PixelFormat::YUV422P12,
        PixelFormat::YUV444P12,
        PixelFormat::RGBA64,
        PixelFormat::YUV422P,
        PixelFormat::YUV444P
    };

    for (auto fmt : formats) {
        auto frame = VideoFrame::allocate(1920, 1080, fmt);
        assert(frame.is_valid());
        assert(frame.pixel_format() == fmt);
        assert(frame.width() == 1920);
        assert(frame.height() == 1080);

        if (fmt == PixelFormat::RGBA64) {
            assert(frame.bit_depth() == 16);
            assert(frame.bytes_per_sample() == 2);
            assert(frame.is_high_bit_depth());
        } else if (pixel_format_bit_depth(fmt) == 10) {
            assert(frame.bit_depth() == 10);
            assert(frame.bytes_per_sample() == 2);
            assert(frame.is_high_bit_depth());
        } else if (pixel_format_bit_depth(fmt) == 12) {
            assert(frame.bit_depth() == 12);
            assert(frame.bytes_per_sample() == 2);
            assert(frame.is_high_bit_depth());
        } else {
            assert(frame.bit_depth() == 8);
            assert(frame.bytes_per_sample() == 1);
            assert(!frame.is_high_bit_depth());
        }

        // Verify 32-byte alignment of plane memory and strides
        for (uint32_t p = 0; p < frame.plane_count(); ++p) {
            assert(reinterpret_cast<uintptr_t>(frame.plane(p)) % 32 == 0);
            assert(frame.stride(p) % 32 == 0);
        }

        // Verify clone integrity
        auto clone = frame.clone();
        assert(clone.is_valid());
        assert(clone.pixel_format() == fmt);
        assert(clone.bit_depth() == frame.bit_depth());
    }

    std::cout << "  Surface Allocation & Alignment OK!" << std::endl;
}

static void test_high_bit_depth_color_conversion() {
    std::cout << "Testing 10-bit & 12-bit to RGBA32 Color Conversions..." << std::endl;

    constexpr uint32_t W = 64;
    constexpr uint32_t H = 64;

    // 1. YUV420P10 to RGBA32
    {
        auto f10 = VideoFrame::allocate(W, H, PixelFormat::YUV420P10);
        f10.set_color_space(ColorSpace::BT2020);
        f10.set_color_range(ColorRange::Studio);

        // Fill with mid-gray (Y=512, U=512, V=512 in 10-bit)
        uint16_t* py = f10.plane16_mut(0);
        uint16_t* pu = f10.plane16_mut(1);
        uint16_t* pv = f10.plane16_mut(2);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                py[y * (f10.stride(0) / 2) + x] = 512;
            }
        }
        for (uint32_t y = 0; y < H / 2; ++y) {
            for (uint32_t x = 0; x < W / 2; ++x) {
                pu[y * (f10.stride(1) / 2) + x] = 512;
                pv[y * (f10.stride(2) / 2) + x] = 512;
            }
        }

        auto rgba = ColorConverter::convert(f10, PixelFormat::RGBA32);
        assert(rgba.is_valid());
        assert(rgba.pixel_format() == PixelFormat::RGBA32);

        // Check converted RGB value: mid-gray in 8-bit ~ 128
        const uint8_t* p = rgba.plane(0);
        assert(std::abs(static_cast<int>(p[0]) - 130) < 5);
        assert(std::abs(static_cast<int>(p[1]) - 130) < 5);
        assert(std::abs(static_cast<int>(p[2]) - 130) < 5);
        assert(p[3] == 255);
    }

    // 2. P010 to RGBA32
    {
        auto p010 = VideoFrame::allocate(W, H, PixelFormat::P010);
        p010.set_color_space(ColorSpace::BT2020);
        p010.set_color_range(ColorRange::Studio);

        uint16_t* py = p010.plane16_mut(0);
        uint16_t* puv = p010.plane16_mut(1);
        // P010 sample is shifted by 6 (MSB-aligned)
        const uint16_t mid_gray_p010 = 512 << 6;
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                py[y * (p010.stride(0) / 2) + x] = mid_gray_p010;
            }
        }
        for (uint32_t y = 0; y < H / 2; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                puv[y * (p010.stride(1) / 2) + x] = mid_gray_p010;
            }
        }

        auto rgba = ColorConverter::convert(p010, PixelFormat::RGBA32);
        assert(rgba.is_valid());
        const uint8_t* p = rgba.plane(0);
        assert(std::abs(static_cast<int>(p[0]) - 130) < 5);
        assert(std::abs(static_cast<int>(p[1]) - 130) < 5);
        assert(std::abs(static_cast<int>(p[2]) - 130) < 5);
    }

    // 3. YUV422P10 and YUV444P10
    {
        auto f422 = VideoFrame::allocate(W, H, PixelFormat::YUV422P10);
        auto rgba422 = ColorConverter::convert(f422, PixelFormat::RGBA32);
        assert(rgba422.is_valid());

        auto f444 = VideoFrame::allocate(W, H, PixelFormat::YUV444P10);
        auto rgba444 = ColorConverter::convert(f444, PixelFormat::RGBA32);
        assert(rgba444.is_valid());
    }

    std::cout << "  High Bit-Depth Color Conversion OK!" << std::endl;
}

static void test_chroma_prediction() {
    std::cout << "Testing Cross-Component Prediction (CfL) & 4:2:2 Chroma..." << std::endl;

    // 1. Downsample luma 4:2:0
    std::vector<int16_t> luma(16 * 16, 100);
    luma[0] = 120; luma[1] = 140; luma[16] = 160; luma[17] = 180; // Top-left 2x2 mean = 150

    std::vector<int16_t> luma_down(8 * 8, 0);
    ChromaPredictor::downsample_luma(luma.data(), 16, luma_down.data(), 8, 8, ChromaSubsampling::CS_420);
    assert(luma_down[0] == 150);

    // 2. AC luma
    std::vector<int16_t> ac_luma(8 * 8, 0);
    ChromaPredictor::compute_ac_luma(luma_down.data(), ac_luma.data(), 8, 8);
    // Mean of ac_luma should sum to approximately 0 (within integer rounding bounds <= total_samples / 2)
    int32_t ac_sum = 0;
    for (auto v : ac_luma) ac_sum += v;
    assert(std::abs(ac_sum) <= 32);

    // 3. CfL prediction
    std::vector<int16_t> base_pred(8 * 8, 512);
    std::vector<int16_t> pred_dst(8 * 8, 0);
    const int16_t alpha_q4 = 16; // alpha = 1.0
    ChromaPredictor::predict_cfl(base_pred.data(), ac_luma.data(), pred_dst.data(), 8, 8, alpha_q4, 10);
    assert(pred_dst[0] == 512 + ac_luma[0]);

    // 4. Rectangular 4:2:2 intra prediction (4x8 block)
    std::vector<int16_t> top(4, 200);
    std::vector<int16_t> left(8, 400);
    std::vector<int16_t> rect_dst(4 * 8, 0);

    // DC: (4 * 200 + 8 * 400 + 6) / 12 = (800 + 3200 + 6) / 12 = 4006 / 12 = 333
    ChromaPredictor::pred_422_dc(top.data(), left.data(), rect_dst.data(), 4, 4, 8);
    assert(rect_dst[0] == 333);
    assert(rect_dst[4 * 8 - 1] == 333);

    // Vertical
    ChromaPredictor::pred_422_vertical(top.data(), rect_dst.data(), 4, 4, 8);
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            assert(rect_dst[y * 4 + x] == top[x]);
        }
    }

    // Horizontal
    ChromaPredictor::pred_422_horizontal(left.data(), rect_dst.data(), 4, 4, 8);
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            assert(rect_dst[y * 4 + x] == left[y]);
        }
    }

    std::cout << "  Chroma Prediction OK!" << std::endl;
}

int main() {
    std::cout << "=== Running test_high_bit_depth_surface ===" << std::endl;
    test_surface_allocation_and_alignment();
    test_high_bit_depth_color_conversion();
    test_chroma_prediction();
    std::cout << "=== ALL test_high_bit_depth_surface PASSED ===" << std::endl;
    return 0;
}

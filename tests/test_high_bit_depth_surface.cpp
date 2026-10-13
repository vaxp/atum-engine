#include <cassert>
#include <iostream>
#include <cstring>
#include <vector>
#include "atum/surface/video_frame.hpp"
#include "atum/surface/color_convert.hpp"

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

int main() {
    std::cout << "=== Running test_high_bit_depth_surface ===" << std::endl;
    test_surface_allocation_and_alignment();
    test_high_bit_depth_color_conversion();
    std::cout << "=== ALL test_high_bit_depth_surface PASSED ===" << std::endl;
    return 0;
}

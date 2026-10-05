#include <cassert>
#include <iostream>
#include <cmath>
#include "atum/surface/video_frame.hpp"
#include "atum/surface/color_convert.hpp"
#include "atum/audio/audio_frame.hpp"
#include "atum/audio/audio_resampler.hpp"

using namespace atum;

void test_video_frame_and_color_conversion() {
    uint32_t width = 64;
    uint32_t height = 64;

    auto rgba = VideoFrame::allocate(width, height, PixelFormat::RGBA32);
    assert(rgba.is_valid());
    assert(rgba.plane_count() == 1);
    assert(rgba.stride(0) >= width * 4);

    // Fill with a recognizable color: Red = 200, Green = 100, Blue = 50
    uint8_t* ptr = rgba.plane_mut(0);
    size_t stride = rgba.stride(0);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            ptr[y * stride + x * 4 + 0] = 200;
            ptr[y * stride + x * 4 + 1] = 100;
            ptr[y * stride + x * 4 + 2] = 50;
            ptr[y * stride + x * 4 + 3] = 255;
        }
    }

    // Convert RGBA32 -> YUV420P
    auto yuv = ColorConverter::convert(rgba, PixelFormat::YUV420P);
    assert(yuv.is_valid());
    assert(yuv.plane_count() == 3);
    assert(yuv.plane(0) != nullptr);
    assert(yuv.plane(1) != nullptr);
    assert(yuv.plane(2) != nullptr);

    // Convert YUV420P -> RGBA32
    auto recon_rgba = ColorConverter::convert(yuv, PixelFormat::RGBA32);
    assert(recon_rgba.is_valid());

    // Check color reconstruction fidelity (tolerance within typical YUV truncation: +/- 4)
    const uint8_t* recon_ptr = recon_rgba.plane(0);
    size_t recon_stride = recon_rgba.stride(0);
    uint8_t r = recon_ptr[32 * recon_stride + 32 * 4 + 0];
    uint8_t g = recon_ptr[32 * recon_stride + 32 * 4 + 1];
    uint8_t b = recon_ptr[32 * recon_stride + 32 * 4 + 2];

    assert(std::abs(static_cast<int>(r) - 200) <= 4);
    assert(std::abs(static_cast<int>(g) - 100) <= 4);
    assert(std::abs(static_cast<int>(b) - 50) <= 4);

    std::cout << "[PASS] VideoFrame allocation and YUV/RGBA color conversion test passed.\n";
}

void test_audio_frame_and_resampling() {
    uint32_t src_rate = 48000;
    uint32_t channels = 2; // Stereo
    size_t nb_samples = 480; // 10 ms at 48 kHz

    auto frame = AudioFrame::allocate(src_rate, channels, AudioSampleFormat::S16LE, nb_samples);
    assert(frame.is_valid());
    assert(frame.size_bytes() == nb_samples * channels * 2);
    assert(std::abs(frame.duration_seconds() - 0.01) < 0.0001);

    // Generate a 440 Hz sine wave tone
    int16_t* samples = reinterpret_cast<int16_t*>(frame.data_mut());
    for (size_t i = 0; i < nb_samples; ++i) {
        double t = static_cast<double>(i) / src_rate;
        int16_t val = static_cast<int16_t>(std::sin(2.0 * M_PI * 440.0 * t) * 16000.0);
        samples[i * 2 + 0] = val; // Left
        samples[i * 2 + 1] = val; // Right
    }

    // Resample from 48000 Hz Stereo to 44100 Hz Mono (F32LE)
    uint32_t dst_rate = 44100;
    auto resampled = AudioResampler::resample(frame, dst_rate, 1, AudioSampleFormat::F32LE);
    assert(resampled.is_valid());
    assert(resampled.sample_rate() == 44100);
    assert(resampled.channels() == 1);
    assert(resampled.format() == AudioSampleFormat::F32LE);

    // Expected sample count: 480 * 44100 / 48000 = 441 samples
    assert(resampled.nb_samples() == 441);

    const float* f_samples = reinterpret_cast<const float*>(resampled.data());
    assert(std::abs(f_samples[0]) < 0.01f); // sin(0) is 0

    std::cout << "[PASS] AudioFrame and AudioResampler test passed.\n";
}

int main() {
    std::cout << "--- Running Nisaba Media Surface & Audio Tests ---\n";
    test_video_frame_and_color_conversion();
    test_audio_frame_and_resampling();
    std::cout << "All surface & audio tests PASSED successfully!\n";
    return 0;
}

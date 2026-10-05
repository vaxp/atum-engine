#include "atum/inloop/av1_restoration.hpp"
#include <algorithm>
#include <vector>

namespace atum {

void Av1LoopRestoration::apply_wiener(
    uint8_t* plane,
    size_t stride,
    uint32_t width,
    uint32_t height,
    const int16_t h_taps[3],
    const int16_t v_taps[3]
) noexcept {
    if (!plane || width == 0 || height == 0) return;

    // Expand 7-tap symmetric filter taps: [w0, w1, w2, w3, w2, w1, w0]
    int16_t h_coeffs[7] = {
        h_taps[0], h_taps[1], h_taps[2],
        static_cast<int16_t>(128 - 2 * (h_taps[0] + h_taps[1] + h_taps[2])),
        h_taps[2], h_taps[1], h_taps[0]
    };
    int16_t v_coeffs[7] = {
        v_taps[0], v_taps[1], v_taps[2],
        static_cast<int16_t>(128 - 2 * (v_taps[0] + v_taps[1] + v_taps[2])),
        v_taps[2], v_taps[1], v_taps[0]
    };

    std::vector<int32_t> intermediate(width * height);

    // 1. Horizontal 7-tap filtering
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            int32_t sum = 0;
            for (int k = -3; k <= 3; ++k) {
                int32_t sx = std::clamp<int32_t>(static_cast<int32_t>(x) + k, 0, static_cast<int32_t>(width) - 1);
                sum += plane[y * stride + sx] * h_coeffs[k + 3];
            }
            intermediate[y * width + x] = (sum + 64) >> 7;
        }
    }

    // 2. Vertical 7-tap filtering
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            int32_t sum = 0;
            for (int k = -3; k <= 3; ++k) {
                int32_t sy = std::clamp<int32_t>(static_cast<int32_t>(y) + k, 0, static_cast<int32_t>(height) - 1);
                sum += intermediate[sy * width + x] * v_coeffs[k + 3];
            }
            plane[y * stride + x] = static_cast<uint8_t>(std::clamp((sum + 64) >> 7, 0, 255));
        }
    }
}

void Av1LoopRestoration::apply_sgrproj(
    uint8_t* plane,
    size_t stride,
    uint32_t width,
    uint32_t height,
    int32_t x0,
    int32_t x1,
    uint8_t r0,
    uint8_t r1
) noexcept {
    if (!plane || width == 0 || height == 0) return;

    auto box_filter = [&](uint8_t r, std::vector<int32_t>& out) {
        out.resize(width * height);
        int32_t rad = std::clamp<int32_t>(r, 1, 3);
        int32_t area = (2 * rad + 1) * (2 * rad + 1);

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                int32_t sum = 0;
                for (int dy = -rad; dy <= rad; ++dy) {
                    int32_t sy = std::clamp<int32_t>(static_cast<int32_t>(y) + dy, 0, static_cast<int32_t>(height) - 1);
                    for (int dx = -rad; dx <= rad; ++dx) {
                        int32_t sx = std::clamp<int32_t>(static_cast<int32_t>(x) + dx, 0, static_cast<int32_t>(width) - 1);
                        sum += plane[sy * stride + sx];
                    }
                }
                out[y * width + x] = sum / area;
            }
        }
    };

    std::vector<int32_t> flt0, flt1;
    box_filter(r0, flt0);
    box_filter(r1, flt1);

    // Self-guided projective combination
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            int32_t orig = plane[y * stride + x];
            int32_t d0 = flt0[y * width + x] - orig;
            int32_t d1 = flt1[y * width + x] - orig;
            int32_t delta = (x0 * d0 + x1 * d1 + 128) >> 8;
            plane[y * stride + x] = static_cast<uint8_t>(std::clamp(orig + delta, 0, 255));
        }
    }
}

void Av1LoopRestoration::apply_frame_restoration(
    VideoFrame& frame,
    Av1RestoreType type
) noexcept {
    if (type == Av1RestoreType::None) return;

    for (size_t p = 0; p < frame.plane_count(); ++p) {
        uint8_t* data = frame.plane_mut(p);
        size_t stride = frame.stride(p);
        uint32_t w = frame.plane_width(p);
        uint32_t h = frame.plane_height(p);

        if (type == Av1RestoreType::Wiener) {
            // Default 7-tap Wiener coefficients: [-4, 8, 28] -> center 60
            const int16_t h_taps[3] = { -4, 8, 28 };
            const int16_t v_taps[3] = { -4, 8, 28 };
            apply_wiener(data, stride, w, h, h_taps, v_taps);
        } else if (type == Av1RestoreType::Sgrproj) {
            apply_sgrproj(data, stride, w, h, 64, 64, 2, 1);
        }
    }
}

} // namespace atum

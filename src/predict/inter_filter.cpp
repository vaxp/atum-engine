#include "atum/predict/inter_filter.hpp"
#include <algorithm>
#include <vector>

namespace atum {

namespace {

inline uint8_t sample_clamped(
    const uint8_t* ref,
    size_t stride,
    int32_t width,
    int32_t height,
    int32_t x,
    int32_t y
) noexcept {
    int32_t cx = std::clamp(x, 0, width - 1);
    int32_t cy = std::clamp(y, 0, height - 1);
    return ref[cy * stride + cx];
}

// H.264 6-tap FIR filter: [1, -5, 20, 20, -5, 1] / 32
inline int32_t fir6(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) noexcept {
    return (a - 5 * b + 20 * c + 20 * d - 5 * e + f + 16) >> 5;
}

} // anonymous namespace

void InterFilter::interpolate_luma(
    const uint8_t* ref,
    size_t ref_stride,
    int32_t ref_width,
    int32_t ref_height,
    int32_t src_x,
    int32_t src_y,
    int32_t mv_x_qpel,
    int32_t mv_y_qpel,
    uint8_t* dst,
    size_t dst_stride,
    uint32_t w,
    uint32_t h
) noexcept {
    int32_t full_x = src_x + (mv_x_qpel >> 2);
    int32_t full_y = src_y + (mv_y_qpel >> 2);
    int32_t qx = mv_x_qpel & 3;
    int32_t qy = mv_y_qpel & 3;

    // Fast path: full pixel alignment
    if (qx == 0 && qy == 0) {
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                dst[y * dst_stride + x] = sample_clamped(ref, ref_stride, ref_width, ref_height, full_x + x, full_y + y);
            }
        }
        return;
    }

    // Subpixel 6-tap FIR interpolation
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            int32_t px = full_x + static_cast<int32_t>(x);
            int32_t py = full_y + static_cast<int32_t>(y);

            if (qy == 0) { // Horizontal subpixel only
                int32_t c = fir6(
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px - 2, py),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px - 1, py),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px,     py),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px + 1, py),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px + 2, py),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px + 3, py)
                );
                c = std::clamp(c, 0, 255);

                if (qx == 2) { // Half-pel
                    dst[y * dst_stride + x] = static_cast<uint8_t>(c);
                } else if (qx == 1) { // 1/4-pel (average full and half)
                    uint8_t full_samp = sample_clamped(ref, ref_stride, ref_width, ref_height, px, py);
                    dst[y * dst_stride + x] = static_cast<uint8_t>((full_samp + c + 1) >> 1);
                } else { // 3/4-pel
                    uint8_t next_samp = sample_clamped(ref, ref_stride, ref_width, ref_height, px + 1, py);
                    dst[y * dst_stride + x] = static_cast<uint8_t>((next_samp + c + 1) >> 1);
                }
            } else if (qx == 0) { // Vertical subpixel only
                int32_t c = fir6(
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px, py - 2),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px, py - 1),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px, py),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px, py + 1),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px, py + 2),
                    sample_clamped(ref, ref_stride, ref_width, ref_height, px, py + 3)
                );
                c = std::clamp(c, 0, 255);

                if (qy == 2) {
                    dst[y * dst_stride + x] = static_cast<uint8_t>(c);
                } else if (qy == 1) {
                    uint8_t full_samp = sample_clamped(ref, ref_stride, ref_width, ref_height, px, py);
                    dst[y * dst_stride + x] = static_cast<uint8_t>((full_samp + c + 1) >> 1);
                } else {
                    uint8_t next_samp = sample_clamped(ref, ref_stride, ref_width, ref_height, px, py + 1);
                    dst[y * dst_stride + x] = static_cast<uint8_t>((next_samp + c + 1) >> 1);
                }
            } else { // Both horizontal and vertical (diagonal)
                // 2D 6-tap separable interpolation
                int32_t h_pass[6];
                for (int k = -2; k <= 3; ++k) {
                    h_pass[k + 2] = fir6(
                        sample_clamped(ref, ref_stride, ref_width, ref_height, px - 2, py + k),
                        sample_clamped(ref, ref_stride, ref_width, ref_height, px - 1, py + k),
                        sample_clamped(ref, ref_stride, ref_width, ref_height, px,     py + k),
                        sample_clamped(ref, ref_stride, ref_width, ref_height, px + 1, py + k),
                        sample_clamped(ref, ref_stride, ref_width, ref_height, px + 2, py + k),
                        sample_clamped(ref, ref_stride, ref_width, ref_height, px + 3, py + k)
                    );
                }
                int32_t val = (h_pass[0] - 5 * h_pass[1] + 20 * h_pass[2] + 20 * h_pass[3] - 5 * h_pass[4] + h_pass[5] + 16) >> 5;
                dst[y * dst_stride + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
            }
        }
    }
}

void InterFilter::interpolate_chroma(
    const uint8_t* ref,
    size_t ref_stride,
    int32_t ref_width,
    int32_t ref_height,
    int32_t src_x,
    int32_t src_y,
    int32_t mv_x_epel,
    int32_t mv_y_epel,
    uint8_t* dst,
    size_t dst_stride,
    uint32_t w,
    uint32_t h
) noexcept {
    int32_t full_x = src_x + (mv_x_epel >> 3);
    int32_t full_y = src_y + (mv_y_epel >> 3);
    int32_t dx = mv_x_epel & 7;
    int32_t dy = mv_y_epel & 7;

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            int32_t px = full_x + static_cast<int32_t>(x);
            int32_t py = full_y + static_cast<int32_t>(y);

            uint32_t s0 = sample_clamped(ref, ref_stride, ref_width, ref_height, px,     py);
            uint32_t s1 = sample_clamped(ref, ref_stride, ref_width, ref_height, px + 1, py);
            uint32_t s2 = sample_clamped(ref, ref_stride, ref_width, ref_height, px,     py + 1);
            uint32_t s3 = sample_clamped(ref, ref_stride, ref_width, ref_height, px + 1, py + 1);

            uint32_t val = ((8 - dx) * (8 - dy) * s0 +
                            dx * (8 - dy) * s1 +
                            (8 - dx) * dy * s2 +
                            dx * dy * s3 + 32) >> 6;

            dst[y * dst_stride + x] = static_cast<uint8_t>(val);
        }
    }
}

} // namespace atum

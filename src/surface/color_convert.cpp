#include "atum/surface/color_convert.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace atum {

static inline uint8_t clamp_u8(int32_t val) noexcept {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return static_cast<uint8_t>(val);
}

static inline uint8_t clamp_f_to_u8(float val) noexcept {
    if (val <= 0.0f) return 0;
    if (val >= 1.0f) return 255;
    return static_cast<uint8_t>(std::lround(val * 255.0f));
}

// =============================================================================
// 8-bit Conversions
// =============================================================================

void ColorConverter::yuv420p_to_rgba(
    const uint8_t* y_plane, size_t y_stride,
    const uint8_t* u_plane, size_t u_stride,
    const uint8_t* v_plane, size_t v_stride,
    uint8_t* dst_rgba, size_t dst_stride,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!y_plane || !u_plane || !v_plane || !dst_rgba) return;

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* py = y_plane + y * y_stride;
        const uint8_t* pu = u_plane + (y / 2) * u_stride;
        const uint8_t* pv = v_plane + (y / 2) * v_stride;
        uint8_t* pdst = dst_rgba + y * dst_stride;

        for (uint32_t x = 0; x < width; ++x) {
            uint8_t Y = py[x];
            uint8_t U = pu[x / 2];
            uint8_t V = pv[x / 2];

            int32_t r = 0, g = 0, b = 0;

            if (range == ColorRange::Studio) {
                int32_t c = static_cast<int32_t>(Y) - 16;
                int32_t d = static_cast<int32_t>(U) - 128;
                int32_t e = static_cast<int32_t>(V) - 128;

                if (space == ColorSpace::BT709) {
                    r = (298 * c + 459 * e + 128) >> 8;
                    g = (298 * c - 55 * d - 136 * e + 128) >> 8;
                    b = (298 * c + 541 * d + 128) >> 8;
                } else if (space == ColorSpace::BT2020) {
                    r = (298 * c + 430 * e + 128) >> 8;
                    g = (298 * c - 48 * d - 166 * e + 128) >> 8;
                    b = (298 * c + 548 * d + 128) >> 8;
                } else { // BT.601
                    r = (298 * c + 409 * e + 128) >> 8;
                    g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                    b = (298 * c + 516 * d + 128) >> 8;
                }
            } else { // Full Range
                int32_t c = static_cast<int32_t>(Y);
                int32_t d = static_cast<int32_t>(U) - 128;
                int32_t e = static_cast<int32_t>(V) - 128;

                if (space == ColorSpace::BT709) {
                    r = c + ((403 * e + 128) >> 8);
                    g = c - ((48 * d + 120 * e + 128) >> 8);
                    b = c + ((475 * d + 128) >> 8);
                } else if (space == ColorSpace::BT2020) {
                    r = c + ((377 * e + 128) >> 8);
                    g = c - ((42 * d + 146 * e + 128) >> 8);
                    b = c + ((481 * d + 128) >> 8);
                } else { // BT.601
                    r = c + ((359 * e + 128) >> 8);
                    g = c - ((88 * d + 183 * e + 128) >> 8);
                    b = c + ((454 * d + 128) >> 8);
                }
            }

            pdst[x * 4 + 0] = clamp_u8(r);
            pdst[x * 4 + 1] = clamp_u8(g);
            pdst[x * 4 + 2] = clamp_u8(b);
            pdst[x * 4 + 3] = 255;
        }
    }
}

void ColorConverter::yuv422p_to_rgba(
    const uint8_t* y_plane, size_t y_stride,
    const uint8_t* u_plane, size_t u_stride,
    const uint8_t* v_plane, size_t v_stride,
    uint8_t* dst_rgba, size_t dst_stride,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!y_plane || !u_plane || !v_plane || !dst_rgba) return;

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* py = y_plane + y * y_stride;
        const uint8_t* pu = u_plane + y * u_stride;
        const uint8_t* pv = v_plane + y * v_stride;
        uint8_t* pdst = dst_rgba + y * dst_stride;

        for (uint32_t x = 0; x < width; ++x) {
            uint8_t Y = py[x];
            uint8_t U = pu[x / 2];
            uint8_t V = pv[x / 2];

            int32_t r = 0, g = 0, b = 0;

            if (range == ColorRange::Studio) {
                int32_t c = static_cast<int32_t>(Y) - 16;
                int32_t d = static_cast<int32_t>(U) - 128;
                int32_t e = static_cast<int32_t>(V) - 128;

                if (space == ColorSpace::BT709) {
                    r = (298 * c + 459 * e + 128) >> 8;
                    g = (298 * c - 55 * d - 136 * e + 128) >> 8;
                    b = (298 * c + 541 * d + 128) >> 8;
                } else {
                    r = (298 * c + 409 * e + 128) >> 8;
                    g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                    b = (298 * c + 516 * d + 128) >> 8;
                }
            } else {
                int32_t c = static_cast<int32_t>(Y);
                int32_t d = static_cast<int32_t>(U) - 128;
                int32_t e = static_cast<int32_t>(V) - 128;

                if (space == ColorSpace::BT709) {
                    r = c + ((403 * e + 128) >> 8);
                    g = c - ((48 * d + 120 * e + 128) >> 8);
                    b = c + ((475 * d + 128) >> 8);
                } else {
                    r = c + ((359 * e + 128) >> 8);
                    g = c - ((88 * d + 183 * e + 128) >> 8);
                    b = c + ((454 * d + 128) >> 8);
                }
            }

            pdst[x * 4 + 0] = clamp_u8(r);
            pdst[x * 4 + 1] = clamp_u8(g);
            pdst[x * 4 + 2] = clamp_u8(b);
            pdst[x * 4 + 3] = 255;
        }
    }
}

void ColorConverter::yuv444p_to_rgba(
    const uint8_t* y_plane, size_t y_stride,
    const uint8_t* u_plane, size_t u_stride,
    const uint8_t* v_plane, size_t v_stride,
    uint8_t* dst_rgba, size_t dst_stride,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!y_plane || !u_plane || !v_plane || !dst_rgba) return;

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* py = y_plane + y * y_stride;
        const uint8_t* pu = u_plane + y * u_stride;
        const uint8_t* pv = v_plane + y * v_stride;
        uint8_t* pdst = dst_rgba + y * dst_stride;

        for (uint32_t x = 0; x < width; ++x) {
            uint8_t Y = py[x];
            uint8_t U = pu[x];
            uint8_t V = pv[x];

            int32_t r = 0, g = 0, b = 0;

            if (range == ColorRange::Studio) {
                int32_t c = static_cast<int32_t>(Y) - 16;
                int32_t d = static_cast<int32_t>(U) - 128;
                int32_t e = static_cast<int32_t>(V) - 128;

                if (space == ColorSpace::BT709) {
                    r = (298 * c + 459 * e + 128) >> 8;
                    g = (298 * c - 55 * d - 136 * e + 128) >> 8;
                    b = (298 * c + 541 * d + 128) >> 8;
                } else {
                    r = (298 * c + 409 * e + 128) >> 8;
                    g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                    b = (298 * c + 516 * d + 128) >> 8;
                }
            } else {
                int32_t c = static_cast<int32_t>(Y);
                int32_t d = static_cast<int32_t>(U) - 128;
                int32_t e = static_cast<int32_t>(V) - 128;

                if (space == ColorSpace::BT709) {
                    r = c + ((403 * e + 128) >> 8);
                    g = c - ((48 * d + 120 * e + 128) >> 8);
                    b = c + ((475 * d + 128) >> 8);
                } else {
                    r = c + ((359 * e + 128) >> 8);
                    g = c - ((88 * d + 183 * e + 128) >> 8);
                    b = c + ((454 * d + 128) >> 8);
                }
            }

            pdst[x * 4 + 0] = clamp_u8(r);
            pdst[x * 4 + 1] = clamp_u8(g);
            pdst[x * 4 + 2] = clamp_u8(b);
            pdst[x * 4 + 3] = 255;
        }
    }
}

void ColorConverter::nv12_to_rgba(
    const uint8_t* y_plane, size_t y_stride,
    const uint8_t* uv_plane, size_t uv_stride,
    uint8_t* dst_rgba, size_t dst_stride,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!y_plane || !uv_plane || !dst_rgba) return;

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* py = y_plane + y * y_stride;
        const uint8_t* puv = uv_plane + (y / 2) * uv_stride;
        uint8_t* pdst = dst_rgba + y * dst_stride;

        for (uint32_t x = 0; x < width; ++x) {
            uint8_t Y = py[x];
            uint32_t uv_idx = (x / 2) * 2;
            uint8_t U = puv[uv_idx + 0];
            uint8_t V = puv[uv_idx + 1];

            int32_t r = 0, g = 0, b = 0;

            if (range == ColorRange::Studio) {
                int32_t c = static_cast<int32_t>(Y) - 16;
                int32_t d = static_cast<int32_t>(U) - 128;
                int32_t e = static_cast<int32_t>(V) - 128;

                if (space == ColorSpace::BT709) {
                    r = (298 * c + 459 * e + 128) >> 8;
                    g = (298 * c - 55 * d - 136 * e + 128) >> 8;
                    b = (298 * c + 541 * d + 128) >> 8;
                } else {
                    r = (298 * c + 409 * e + 128) >> 8;
                    g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                    b = (298 * c + 516 * d + 128) >> 8;
                }
            } else {
                int32_t c = static_cast<int32_t>(Y);
                int32_t d = static_cast<int32_t>(U) - 128;
                int32_t e = static_cast<int32_t>(V) - 128;

                if (space == ColorSpace::BT709) {
                    r = c + ((403 * e + 128) >> 8);
                    g = c - ((48 * d + 120 * e + 128) >> 8);
                    b = c + ((475 * d + 128) >> 8);
                } else {
                    r = c + ((359 * e + 128) >> 8);
                    g = c - ((88 * d + 183 * e + 128) >> 8);
                    b = c + ((454 * d + 128) >> 8);
                }
            }

            pdst[x * 4 + 0] = clamp_u8(r);
            pdst[x * 4 + 1] = clamp_u8(g);
            pdst[x * 4 + 2] = clamp_u8(b);
            pdst[x * 4 + 3] = 255;
        }
    }
}

void ColorConverter::rgba_to_yuv420p(
    const uint8_t* src_rgba, size_t src_stride,
    uint8_t* y_plane, size_t y_stride,
    uint8_t* u_plane, size_t u_stride,
    uint8_t* v_plane, size_t v_stride,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!src_rgba || !y_plane || !u_plane || !v_plane) return;

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* psrc = src_rgba + y * src_stride;
        uint8_t* py = y_plane + y * y_stride;

        for (uint32_t x = 0; x < width; ++x) {
            int32_t R = psrc[x * 4 + 0];
            int32_t G = psrc[x * 4 + 1];
            int32_t B = psrc[x * 4 + 2];

            int32_t Y = 0;
            if (range == ColorRange::Studio) {
                if (space == ColorSpace::BT709) {
                    Y = ((47 * R + 157 * G + 16 * B + 128) >> 8) + 16;
                } else {
                    Y = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
                }
            } else {
                if (space == ColorSpace::BT709) {
                    Y = (54 * R + 183 * G + 19 * B + 128) >> 8;
                } else {
                    Y = (77 * R + 150 * G + 29 * B + 128) >> 8;
                }
            }
            py[x] = clamp_u8(Y);
        }
    }

    // Subsampled Chroma U and V (2x2 blocks)
    for (uint32_t y = 0; y < (height + 1) / 2; ++y) {
        uint8_t* pu = u_plane + y * u_stride;
        uint8_t* pv = v_plane + y * v_stride;

        for (uint32_t x = 0; x < (width + 1) / 2; ++x) {
            uint32_t sx = x * 2;
            uint32_t sy = y * 2;

            int32_t sum_r = 0, sum_g = 0, sum_b = 0;
            int32_t count = 0;

            for (uint32_t dy = 0; dy < 2 && (sy + dy) < height; ++dy) {
                const uint8_t* row = src_rgba + (sy + dy) * src_stride;
                for (uint32_t dx = 0; dx < 2 && (sx + dx) < width; ++dx) {
                    sum_r += row[(sx + dx) * 4 + 0];
                    sum_g += row[(sx + dx) * 4 + 1];
                    sum_b += row[(sx + dx) * 4 + 2];
                    count++;
                }
            }

            int32_t R = sum_r / count;
            int32_t G = sum_g / count;
            int32_t B = sum_b / count;

            int32_t U = 0, V = 0;
            if (range == ColorRange::Studio) {
                if (space == ColorSpace::BT709) {
                    U = ((-26 * R - 87 * G + 112 * B + 128) >> 8) + 128;
                    V = ((112 * R - 102 * G - 10 * B + 128) >> 8) + 128;
                } else {
                    U = ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128;
                    V = ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128;
                }
            } else {
                if (space == ColorSpace::BT709) {
                    U = ((-30 * R - 101 * G + 131 * B + 128) >> 8) + 128;
                    V = ((131 * R - 119 * G - 12 * B + 128) >> 8) + 128;
                } else {
                    U = ((-43 * R - 85 * G + 128 * B + 128) >> 8) + 128;
                    V = ((128 * R - 107 * G - 21 * B + 128) >> 8) + 128;
                }
            }

            pu[x] = clamp_u8(U);
            pv[x] = clamp_u8(V);
        }
    }
}

// =============================================================================
// 10-bit High Bit-Depth Conversions
// =============================================================================

void ColorConverter::yuv420p10_to_rgba(
    const uint16_t* y_plane, size_t y_stride_bytes,
    const uint16_t* u_plane, size_t u_stride_bytes,
    const uint16_t* v_plane, size_t v_stride_bytes,
    uint8_t* dst_rgba, size_t dst_stride_bytes,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!y_plane || !u_plane || !v_plane || !dst_rgba) return;

    const size_t y_stride = y_stride_bytes / sizeof(uint16_t);
    const size_t u_stride = u_stride_bytes / sizeof(uint16_t);
    const size_t v_stride = v_stride_bytes / sizeof(uint16_t);

    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t* py = y_plane + y * y_stride;
        const uint16_t* pu = u_plane + (y / 2) * u_stride;
        const uint16_t* pv = v_plane + (y / 2) * v_stride;
        uint8_t* pdst = dst_rgba + y * dst_stride_bytes;

        for (uint32_t x = 0; x < width; ++x) {
            float Y = static_cast<float>(py[x]);
            float U = static_cast<float>(pu[x / 2]);
            float V = static_cast<float>(pv[x / 2]);

            float c, d, e;
            if (range == ColorRange::Studio) {
                // 10-bit limited: Y in [64, 940], UV in [64, 960]
                c = (Y - 64.0f) / 876.0f;
                d = (U - 512.0f) / 896.0f;
                e = (V - 512.0f) / 896.0f;
            } else {
                // 10-bit full: [0, 1023]
                c = Y / 1023.0f;
                d = (U - 512.0f) / 1023.0f;
                e = (V - 512.0f) / 1023.0f;
            }

            float r, g, b;
            if (space == ColorSpace::BT2020) {
                r = c + 1.4746f * e;
                b = c + 1.8814f * d;
                g = c - 0.57135f * e - 0.16455f * d;
            } else { // BT.709
                r = c + 1.5748f * e;
                b = c + 1.8556f * d;
                g = c - 0.4681f * e - 0.1873f * d;
            }

            pdst[x * 4 + 0] = clamp_f_to_u8(r);
            pdst[x * 4 + 1] = clamp_f_to_u8(g);
            pdst[x * 4 + 2] = clamp_f_to_u8(b);
            pdst[x * 4 + 3] = 255;
        }
    }
}

void ColorConverter::p010_to_rgba(
    const uint16_t* y_plane, size_t y_stride_bytes,
    const uint16_t* uv_plane, size_t uv_stride_bytes,
    uint8_t* dst_rgba, size_t dst_stride_bytes,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!y_plane || !uv_plane || !dst_rgba) return;

    const size_t y_stride = y_stride_bytes / sizeof(uint16_t);
    const size_t uv_stride = uv_stride_bytes / sizeof(uint16_t);

    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t* py = y_plane + y * y_stride;
        const uint16_t* puv = uv_plane + (y / 2) * uv_stride;
        uint8_t* pdst = dst_rgba + y * dst_stride_bytes;

        for (uint32_t x = 0; x < width; ++x) {
            // P010 is MSB-aligned 16-bit, shift down by 6 for 10-bit sample
            float Y = static_cast<float>(py[x] >> 6);
            uint32_t uv_idx = (x / 2) * 2;
            float U = static_cast<float>(puv[uv_idx + 0] >> 6);
            float V = static_cast<float>(puv[uv_idx + 1] >> 6);

            float c, d, e;
            if (range == ColorRange::Studio) {
                c = (Y - 64.0f) / 876.0f;
                d = (U - 512.0f) / 896.0f;
                e = (V - 512.0f) / 896.0f;
            } else {
                c = Y / 1023.0f;
                d = (U - 512.0f) / 1023.0f;
                e = (V - 512.0f) / 1023.0f;
            }

            float r, g, b;
            if (space == ColorSpace::BT2020) {
                r = c + 1.4746f * e;
                b = c + 1.8814f * d;
                g = c - 0.57135f * e - 0.16455f * d;
            } else {
                r = c + 1.5748f * e;
                b = c + 1.8556f * d;
                g = c - 0.4681f * e - 0.1873f * d;
            }

            pdst[x * 4 + 0] = clamp_f_to_u8(r);
            pdst[x * 4 + 1] = clamp_f_to_u8(g);
            pdst[x * 4 + 2] = clamp_f_to_u8(b);
            pdst[x * 4 + 3] = 255;
        }
    }
}

void ColorConverter::yuv422p10_to_rgba(
    const uint16_t* y_plane, size_t y_stride_bytes,
    const uint16_t* u_plane, size_t u_stride_bytes,
    const uint16_t* v_plane, size_t v_stride_bytes,
    uint8_t* dst_rgba, size_t dst_stride_bytes,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!y_plane || !u_plane || !v_plane || !dst_rgba) return;

    const size_t y_stride = y_stride_bytes / sizeof(uint16_t);
    const size_t u_stride = u_stride_bytes / sizeof(uint16_t);
    const size_t v_stride = v_stride_bytes / sizeof(uint16_t);

    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t* py = y_plane + y * y_stride;
        const uint16_t* pu = u_plane + y * u_stride;
        const uint16_t* pv = v_plane + y * v_stride;
        uint8_t* pdst = dst_rgba + y * dst_stride_bytes;

        for (uint32_t x = 0; x < width; ++x) {
            float Y = static_cast<float>(py[x]);
            float U = static_cast<float>(pu[x / 2]);
            float V = static_cast<float>(pv[x / 2]);

            float c, d, e;
            if (range == ColorRange::Studio) {
                c = (Y - 64.0f) / 876.0f;
                d = (U - 512.0f) / 896.0f;
                e = (V - 512.0f) / 896.0f;
            } else {
                c = Y / 1023.0f;
                d = (U - 512.0f) / 1023.0f;
                e = (V - 512.0f) / 1023.0f;
            }

            float r, g, b;
            if (space == ColorSpace::BT2020) {
                r = c + 1.4746f * e;
                b = c + 1.8814f * d;
                g = c - 0.57135f * e - 0.16455f * d;
            } else {
                r = c + 1.5748f * e;
                b = c + 1.8556f * d;
                g = c - 0.4681f * e - 0.1873f * d;
            }

            pdst[x * 4 + 0] = clamp_f_to_u8(r);
            pdst[x * 4 + 1] = clamp_f_to_u8(g);
            pdst[x * 4 + 2] = clamp_f_to_u8(b);
            pdst[x * 4 + 3] = 255;
        }
    }
}

void ColorConverter::yuv444p10_to_rgba(
    const uint16_t* y_plane, size_t y_stride_bytes,
    const uint16_t* u_plane, size_t u_stride_bytes,
    const uint16_t* v_plane, size_t v_stride_bytes,
    uint8_t* dst_rgba, size_t dst_stride_bytes,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!y_plane || !u_plane || !v_plane || !dst_rgba) return;

    const size_t y_stride = y_stride_bytes / sizeof(uint16_t);
    const size_t u_stride = u_stride_bytes / sizeof(uint16_t);
    const size_t v_stride = v_stride_bytes / sizeof(uint16_t);

    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t* py = y_plane + y * y_stride;
        const uint16_t* pu = u_plane + y * u_stride;
        const uint16_t* pv = v_plane + y * v_stride;
        uint8_t* pdst = dst_rgba + y * dst_stride_bytes;

        for (uint32_t x = 0; x < width; ++x) {
            float Y = static_cast<float>(py[x]);
            float U = static_cast<float>(pu[x]);
            float V = static_cast<float>(pv[x]);

            float c, d, e;
            if (range == ColorRange::Studio) {
                c = (Y - 64.0f) / 876.0f;
                d = (U - 512.0f) / 896.0f;
                e = (V - 512.0f) / 896.0f;
            } else {
                c = Y / 1023.0f;
                d = (U - 512.0f) / 1023.0f;
                e = (V - 512.0f) / 1023.0f;
            }

            float r, g, b;
            if (space == ColorSpace::BT2020) {
                r = c + 1.4746f * e;
                b = c + 1.8814f * d;
                g = c - 0.57135f * e - 0.16455f * d;
            } else {
                r = c + 1.5748f * e;
                b = c + 1.8556f * d;
                g = c - 0.4681f * e - 0.1873f * d;
            }

            pdst[x * 4 + 0] = clamp_f_to_u8(r);
            pdst[x * 4 + 1] = clamp_f_to_u8(g);
            pdst[x * 4 + 2] = clamp_f_to_u8(b);
            pdst[x * 4 + 3] = 255;
        }
    }
}

// =============================================================================
// 12-bit Cinematic Master Conversions
// =============================================================================

void ColorConverter::yuv420p12_to_rgba(
    const uint16_t* y_plane, size_t y_stride_bytes,
    const uint16_t* u_plane, size_t u_stride_bytes,
    const uint16_t* v_plane, size_t v_stride_bytes,
    uint8_t* dst_rgba, size_t dst_stride_bytes,
    uint32_t width, uint32_t height,
    ColorSpace space,
    ColorRange range
) noexcept {
    if (!y_plane || !u_plane || !v_plane || !dst_rgba) return;

    const size_t y_stride = y_stride_bytes / sizeof(uint16_t);
    const size_t u_stride = u_stride_bytes / sizeof(uint16_t);
    const size_t v_stride = v_stride_bytes / sizeof(uint16_t);

    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t* py = y_plane + y * y_stride;
        const uint16_t* pu = u_plane + (y / 2) * u_stride;
        const uint16_t* pv = v_plane + (y / 2) * v_stride;
        uint8_t* pdst = dst_rgba + y * dst_stride_bytes;

        for (uint32_t x = 0; x < width; ++x) {
            float Y = static_cast<float>(py[x]);
            float U = static_cast<float>(pu[x / 2]);
            float V = static_cast<float>(pv[x / 2]);

            float c, d, e;
            if (range == ColorRange::Studio) {
                // 12-bit limited: Y in [256, 3760], UV in [256, 3840]
                c = (Y - 256.0f) / 3504.0f;
                d = (U - 2048.0f) / 3584.0f;
                e = (V - 2048.0f) / 3584.0f;
            } else {
                c = Y / 4095.0f;
                d = (U - 2048.0f) / 4095.0f;
                e = (V - 2048.0f) / 4095.0f;
            }

            float r, g, b;
            if (space == ColorSpace::BT2020) {
                r = c + 1.4746f * e;
                b = c + 1.8814f * d;
                g = c - 0.57135f * e - 0.16455f * d;
            } else {
                r = c + 1.5748f * e;
                b = c + 1.8556f * d;
                g = c - 0.4681f * e - 0.1873f * d;
            }

            pdst[x * 4 + 0] = clamp_f_to_u8(r);
            pdst[x * 4 + 1] = clamp_f_to_u8(g);
            pdst[x * 4 + 2] = clamp_f_to_u8(b);
            pdst[x * 4 + 3] = 255;
        }
    }
}

// =============================================================================
// Full Pipeline HDR-to-SDR Tone-Mapped Conversion
// =============================================================================

void ColorConverter::convert_hdr_to_sdr_rgba(
    const VideoFrame& src,
    uint8_t* dst_rgba,
    size_t dst_stride_bytes,
    const ToneMapper& tone_mapper
) noexcept {
    if (!src.is_valid() || !dst_rgba) return;

    const uint32_t width = src.width();
    const uint32_t height = src.height();
    const HdrMetadata& meta = src.hdr_metadata();

    // Gamut transform matrix from source primaries (e.g. BT.2020) to destination (BT.709)
    const ColorMatrix3x3 gamut_matrix = ColorMatrix3x3::get_transform(
        (meta.primaries != ColorPrimaries::Unspecified && meta.primaries != ColorPrimaries::Unknown)
            ? meta.primaries : ColorPrimaries::BT2020,
        ColorPrimaries::BT709
    );

    const bool is_10bit = (src.bit_depth() == 10);
    const bool is_12bit = (src.bit_depth() == 12);
    const bool is_studio = (src.color_range() == ColorRange::Studio);

    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* pdst = dst_rgba + y * dst_stride_bytes;

        for (uint32_t x = 0; x < width; ++x) {
            float Y_norm = 0.0f, U_norm = 0.0f, V_norm = 0.0f;

            if (is_10bit) {
                if (src.pixel_format() == PixelFormat::P010) {
                    const uint16_t* py = src.plane16(0) + y * (src.stride(0) / 2);
                    const uint16_t* puv = src.plane16(1) + (y / 2) * (src.stride(1) / 2);
                    const float Y = static_cast<float>(py[x] >> 6);
                    const uint32_t uv_idx = (x / 2) * 2;
                    const float U = static_cast<float>(puv[uv_idx + 0] >> 6);
                    const float V = static_cast<float>(puv[uv_idx + 1] >> 6);

                    if (is_studio) {
                        Y_norm = (Y - 64.0f) / 876.0f;
                        U_norm = (U - 512.0f) / 896.0f;
                        V_norm = (V - 512.0f) / 896.0f;
                    } else {
                        Y_norm = Y / 1023.0f;
                        U_norm = (U - 512.0f) / 1023.0f;
                        V_norm = (V - 512.0f) / 1023.0f;
                    }
                } else {
                    const uint32_t chr_x = (src.pixel_format() == PixelFormat::YUV444P10) ? x : (x / 2);
                    const uint32_t chr_y = (src.pixel_format() == PixelFormat::YUV420P10) ? (y / 2) : y;
                    const uint16_t* py = src.plane16(0) + y * (src.stride(0) / 2);
                    const uint16_t* pu = src.plane16(1) + chr_y * (src.stride(1) / 2);
                    const uint16_t* pv = src.plane16(2) + chr_y * (src.stride(2) / 2);

                    const float Y = static_cast<float>(py[x]);
                    const float U = static_cast<float>(pu[chr_x]);
                    const float V = static_cast<float>(pv[chr_x]);

                    if (is_studio) {
                        Y_norm = (Y - 64.0f) / 876.0f;
                        U_norm = (U - 512.0f) / 896.0f;
                        V_norm = (V - 512.0f) / 896.0f;
                    } else {
                        Y_norm = Y / 1023.0f;
                        U_norm = (U - 512.0f) / 1023.0f;
                        V_norm = (V - 512.0f) / 1023.0f;
                    }
                }
            } else if (is_12bit) {
                const uint32_t chr_x = (src.pixel_format() == PixelFormat::YUV444P12) ? x : (x / 2);
                const uint32_t chr_y = (src.pixel_format() == PixelFormat::YUV420P12) ? (y / 2) : y;
                const uint16_t* py = src.plane16(0) + y * (src.stride(0) / 2);
                const uint16_t* pu = src.plane16(1) + chr_y * (src.stride(1) / 2);
                const uint16_t* pv = src.plane16(2) + chr_y * (src.stride(2) / 2);

                const float Y = static_cast<float>(py[x]);
                const float U = static_cast<float>(pu[chr_x]);
                const float V = static_cast<float>(pv[chr_x]);

                if (is_studio) {
                    Y_norm = (Y - 256.0f) / 3504.0f;
                    U_norm = (U - 2048.0f) / 3584.0f;
                    V_norm = (V - 2048.0f) / 3584.0f;
                } else {
                    Y_norm = Y / 4095.0f;
                    U_norm = (U - 2048.0f) / 4095.0f;
                    V_norm = (V - 2048.0f) / 4095.0f;
                }
            } else {
                // 8-bit
                const uint32_t chr_x = (src.pixel_format() == PixelFormat::YUV444P) ? x : (x / 2);
                const uint32_t chr_y = (src.pixel_format() == PixelFormat::YUV420P) ? (y / 2) : y;
                const uint8_t* py = src.plane(0) + y * src.stride(0);
                const uint8_t* pu = src.plane(1) + chr_y * src.stride(1);
                const uint8_t* pv = src.plane(2) + chr_y * src.stride(2);

                const float Y = static_cast<float>(py[x]);
                const float U = static_cast<float>(pu[chr_x]);
                const float V = static_cast<float>(pv[chr_x]);

                if (is_studio) {
                    Y_norm = (Y - 16.0f) / 219.0f;
                    U_norm = (U - 128.0f) / 224.0f;
                    V_norm = (V - 128.0f) / 224.0f;
                } else {
                    Y_norm = Y / 255.0f;
                    U_norm = (U - 128.0f) / 255.0f;
                    V_norm = (V - 128.0f) / 255.0f;
                }
            }

            // Matrix coefficients (BT.2020 vs BT.709)
            float r_nonlin, g_nonlin, b_nonlin;
            if (src.color_space() == ColorSpace::BT2020 || meta.matrix == MatrixCoefficients::BT2020_NCL) {
                r_nonlin = Y_norm + 1.4746f * V_norm;
                b_nonlin = Y_norm + 1.8814f * U_norm;
                g_nonlin = Y_norm - 0.57135f * V_norm - 0.16455f * U_norm;
            } else {
                r_nonlin = Y_norm + 1.5748f * V_norm;
                b_nonlin = Y_norm + 1.8556f * U_norm;
                g_nonlin = Y_norm - 0.4681f * V_norm - 0.1873f * U_norm;
            }

            // 1. Inverse EOTF to linear light
            const TransferCharacteristic tr = (meta.transfer != TransferCharacteristic::Unspecified &&
                                               meta.transfer != TransferCharacteristic::Unknown)
                ? meta.transfer : TransferCharacteristic::SMPTE2084;
            const float r_lin = TransferFunction::to_linear(r_nonlin, tr);
            const float g_lin = TransferFunction::to_linear(g_nonlin, tr);
            const float b_lin = TransferFunction::to_linear(b_nonlin, tr);

            // 2. Gamut conversion to target BT.709
            float r_gamut, g_gamut, b_gamut;
            gamut_matrix.transform(r_lin, g_lin, b_lin, r_gamut, g_gamut, b_gamut);

            // 3. Tone Mapping
            float r_mapped, g_mapped, b_mapped;
            tone_mapper.map_rgb(r_gamut, g_gamut, b_gamut, r_mapped, g_mapped, b_mapped);

            // 4. Destination OETF (sRGB)
            const float r_out = TransferFunction::srgb_oetf(r_mapped);
            const float g_out = TransferFunction::srgb_oetf(g_mapped);
            const float b_out = TransferFunction::srgb_oetf(b_mapped);

            pdst[x * 4 + 0] = clamp_f_to_u8(r_out);
            pdst[x * 4 + 1] = clamp_f_to_u8(g_out);
            pdst[x * 4 + 2] = clamp_f_to_u8(b_out);
            pdst[x * 4 + 3] = 255;
        }
    }
}

// =============================================================================
// Unified Dispatch
// =============================================================================

VideoFrame ColorConverter::convert(const VideoFrame& src, PixelFormat target_fmt) {
    if (!src.is_valid()) return VideoFrame();
    if (src.pixel_format() == target_fmt) return src.clone();

    auto dst = VideoFrame::allocate(src.width(), src.height(), target_fmt);
    dst.set_color_range(src.color_range());
    dst.set_color_space(src.color_space());
    dst.set_pts(src.pts());
    dst.set_dts(src.dts());
    dst.set_duration(src.duration());
    dst.set_timebase(src.timebase());
    dst.set_keyframe(src.is_keyframe());
    dst.set_hdr_metadata(src.hdr_metadata());

    // If source is HDR (PQ / HLG / mastering display) and target is RGBA32, run tone mapped HDR-to-SDR
    if (target_fmt == PixelFormat::RGBA32 && src.hdr_metadata().is_hdr()) {
        ToneMapper tm;
        tm.op = ToneMappingOperator::ACES_FILMIC;
        tm.primaries = src.hdr_metadata().primaries;
        convert_hdr_to_sdr_rgba(src, dst.plane_mut(0), dst.stride(0), tm);
        return dst;
    }

    if (src.pixel_format() == PixelFormat::YUV420P && target_fmt == PixelFormat::RGBA32) {
        yuv420p_to_rgba(
            src.plane(0), src.stride(0),
            src.plane(1), src.stride(1),
            src.plane(2), src.stride(2),
            dst.plane_mut(0), dst.stride(0),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    } else if (src.pixel_format() == PixelFormat::YUV422P && target_fmt == PixelFormat::RGBA32) {
        yuv422p_to_rgba(
            src.plane(0), src.stride(0),
            src.plane(1), src.stride(1),
            src.plane(2), src.stride(2),
            dst.plane_mut(0), dst.stride(0),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    } else if (src.pixel_format() == PixelFormat::YUV444P && target_fmt == PixelFormat::RGBA32) {
        yuv444p_to_rgba(
            src.plane(0), src.stride(0),
            src.plane(1), src.stride(1),
            src.plane(2), src.stride(2),
            dst.plane_mut(0), dst.stride(0),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    } else if (src.pixel_format() == PixelFormat::NV12 && target_fmt == PixelFormat::RGBA32) {
        nv12_to_rgba(
            src.plane(0), src.stride(0),
            src.plane(1), src.stride(1),
            dst.plane_mut(0), dst.stride(0),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    } else if (src.pixel_format() == PixelFormat::RGBA32 && target_fmt == PixelFormat::YUV420P) {
        rgba_to_yuv420p(
            src.plane(0), src.stride(0),
            dst.plane_mut(0), dst.stride(0),
            dst.plane_mut(1), dst.stride(1),
            dst.plane_mut(2), dst.stride(2),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    } else if (src.pixel_format() == PixelFormat::YUV420P10 && target_fmt == PixelFormat::RGBA32) {
        yuv420p10_to_rgba(
            src.plane16(0), src.stride(0),
            src.plane16(1), src.stride(1),
            src.plane16(2), src.stride(2),
            dst.plane_mut(0), dst.stride(0),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    } else if (src.pixel_format() == PixelFormat::P010 && target_fmt == PixelFormat::RGBA32) {
        p010_to_rgba(
            src.plane16(0), src.stride(0),
            src.plane16(1), src.stride(1),
            dst.plane_mut(0), dst.stride(0),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    } else if (src.pixel_format() == PixelFormat::YUV422P10 && target_fmt == PixelFormat::RGBA32) {
        yuv422p10_to_rgba(
            src.plane16(0), src.stride(0),
            src.plane16(1), src.stride(1),
            src.plane16(2), src.stride(2),
            dst.plane_mut(0), dst.stride(0),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    } else if (src.pixel_format() == PixelFormat::YUV444P10 && target_fmt == PixelFormat::RGBA32) {
        yuv444p10_to_rgba(
            src.plane16(0), src.stride(0),
            src.plane16(1), src.stride(1),
            src.plane16(2), src.stride(2),
            dst.plane_mut(0), dst.stride(0),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    } else if (src.pixel_format() == PixelFormat::YUV420P12 && target_fmt == PixelFormat::RGBA32) {
        yuv420p12_to_rgba(
            src.plane16(0), src.stride(0),
            src.plane16(1), src.stride(1),
            src.plane16(2), src.stride(2),
            dst.plane_mut(0), dst.stride(0),
            src.width(), src.height(),
            src.color_space(), src.color_range()
        );
    }

    return dst;
}

} // namespace atum

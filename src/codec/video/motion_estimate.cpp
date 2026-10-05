#include "atum/codec/video/motion_estimate.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace atum {

namespace {

inline int32_t clamp_coord(int32_t coord, int32_t max_val) noexcept {
    return std::clamp(coord, 0, max_val - 1);
}

inline uint8_t sample_ref(
    const uint8_t* plane, size_t stride,
    int32_t frame_w, int32_t frame_h,
    int32_t x, int32_t y
) noexcept {
    int32_t cx = clamp_coord(x, frame_w);
    int32_t cy = clamp_coord(y, frame_h);
    return plane[cy * stride + cx];
}

// H.264 6-tap FIR filter [1, -5, 20, 20, -5, 1] / 32
inline int32_t filter_6tap(int32_t p_2, int32_t p_1, int32_t p0, int32_t p1, int32_t p2, int32_t p3) noexcept {
    return p_2 - 5 * p_1 + 20 * p0 + 20 * p1 - 5 * p2 + p3;
}

inline int32_t median3(int32_t a, int32_t b, int32_t c) noexcept {
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

} // namespace

uint32_t MotionEstimator::sad(
    const uint8_t* src, size_t src_stride,
    const uint8_t* ref, size_t ref_stride,
    uint32_t width, uint32_t height
) noexcept {
    uint32_t total_sad = 0;
#if defined(__AVX2__)
    if (width == 16) {
        __m128i acc = _mm_setzero_si128();
        for (uint32_t y = 0; y < height; ++y) {
            __m128i s = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + y * src_stride));
            __m128i r = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ref + y * ref_stride));
            acc = _mm_add_epi32(acc, _mm_sad_epu8(s, r));
        }
        total_sad = _mm_extract_epi32(acc, 0) + _mm_extract_epi32(acc, 2);
        return total_sad;
    }
#endif

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* s = src + y * src_stride;
        const uint8_t* r = ref + y * ref_stride;
        for (uint32_t x = 0; x < width; ++x) {
            total_sad += std::abs(static_cast<int32_t>(s[x]) - static_cast<int32_t>(r[x]));
        }
    }
    return total_sad;
}

uint32_t MotionEstimator::sad16x16(
    const uint8_t* src, size_t src_stride,
    const uint8_t* ref, size_t ref_stride
) noexcept {
    return sad(src, src_stride, ref, ref_stride, 16, 16);
}

uint32_t MotionEstimator::sad8x8(
    const uint8_t* src, size_t src_stride,
    const uint8_t* ref, size_t ref_stride
) noexcept {
    return sad(src, src_stride, ref, ref_stride, 8, 8);
}

uint32_t MotionEstimator::sad4x4(
    const uint8_t* src, size_t src_stride,
    const uint8_t* ref, size_t ref_stride
) noexcept {
    return sad(src, src_stride, ref, ref_stride, 4, 4);
}

uint32_t MotionEstimator::satd4x4(
    const uint8_t* src, size_t src_stride,
    const uint8_t* ref, size_t ref_stride
) noexcept {
    int32_t diff[16];
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            diff[y * 4 + x] = static_cast<int32_t>(src[y * src_stride + x]) -
                              static_cast<int32_t>(ref[y * ref_stride + x]);
        }
    }

    // 1D horizontal Hadamard
    for (int i = 0; i < 4; ++i) {
        int32_t* r = diff + i * 4;
        int32_t a0 = r[0] + r[3];
        int32_t a1 = r[1] + r[2];
        int32_t a2 = r[1] - r[2];
        int32_t a3 = r[0] - r[3];

        r[0] = a0 + a1;
        r[1] = a3 + a2;
        r[2] = a0 - a1;
        r[3] = a3 - a2;
    }

    // 1D vertical Hadamard
    uint32_t sum = 0;
    for (int j = 0; j < 4; ++j) {
        int32_t b0 = diff[0 * 4 + j] + diff[3 * 4 + j];
        int32_t b1 = diff[1 * 4 + j] + diff[2 * 4 + j];
        int32_t b2 = diff[1 * 4 + j] - diff[2 * 4 + j];
        int32_t b3 = diff[0 * 4 + j] - diff[3 * 4 + j];

        sum += std::abs(b0 + b1);
        sum += std::abs(b3 + b2);
        sum += std::abs(b0 - b1);
        sum += std::abs(b3 - b2);
    }

    return (sum + 1) >> 1;
}

uint32_t MotionEstimator::satd16x16(
    const uint8_t* src, size_t src_stride,
    const uint8_t* ref, size_t ref_stride
) noexcept {
    uint32_t total = 0;
    for (uint32_t by = 0; by < 16; by += 4) {
        for (uint32_t bx = 0; bx < 16; bx += 4) {
            total += satd4x4(src + by * src_stride + bx, src_stride,
                             ref + by * ref_stride + bx, ref_stride);
        }
    }
    return total;
}

void MotionEstimator::interpolate_block(
    const uint8_t* ref_plane, size_t ref_stride,
    int32_t frame_width, int32_t frame_height,
    int32_t int_x, int32_t int_y,
    int32_t frac_x, int32_t frac_y,
    uint8_t* dst, size_t dst_stride,
    uint32_t block_w, uint32_t block_h
) noexcept {
    if (frac_x == 0 && frac_y == 0) {
        // Direct integer copy with boundary clamping
        for (uint32_t y = 0; y < block_h; ++y) {
            for (uint32_t x = 0; x < block_w; ++x) {
                dst[y * dst_stride + x] = sample_ref(ref_plane, ref_stride, frame_width, frame_height,
                                                     int_x + static_cast<int32_t>(x),
                                                     int_y + static_cast<int32_t>(y));
            }
        }
        return;
    }

    // Half-pel horizontal & vertical calculations
    // For general quarter-pel interpolation across block_w x block_h:
    for (uint32_t by = 0; by < block_h; ++by) {
        int32_t py = int_y + static_cast<int32_t>(by);
        for (uint32_t bx = 0; bx < block_w; ++bx) {
            int32_t px = int_x + static_cast<int32_t>(bx);

            // Fetch 6 samples horizontally for half-pel
            auto get_h_half = [&](int32_t x, int32_t y) -> int32_t {
                int32_t p_2 = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 2, y);
                int32_t p_1 = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 1, y);
                int32_t p0  = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x,     y);
                int32_t p1  = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 1, y);
                int32_t p2  = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 2, y);
                int32_t p3  = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 3, y);
                return std::clamp((filter_6tap(p_2, p_1, p0, p1, p2, p3) + 16) >> 5, 0, 255);
            };

            auto get_v_half = [&](int32_t x, int32_t y) -> int32_t {
                int32_t p_2 = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x, y - 2);
                int32_t p_1 = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x, y - 1);
                int32_t p0  = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x, y);
                int32_t p1  = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x, y + 1);
                int32_t p2  = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x, y + 2);
                int32_t p3  = sample_ref(ref_plane, ref_stride, frame_width, frame_height, x, y + 3);
                return std::clamp((filter_6tap(p_2, p_1, p0, p1, p2, p3) + 16) >> 5, 0, 255);
            };

            auto get_center_half = [&](int32_t x, int32_t y) -> int32_t {
                // 6-tap on 6 horizontal half-pel values
                int32_t r_2 = filter_6tap(
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 2, y - 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 1, y - 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x,     y - 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 1, y - 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 2, y - 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 3, y - 2)
                );
                int32_t r_1 = filter_6tap(
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 2, y - 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 1, y - 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x,     y - 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 1, y - 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 2, y - 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 3, y - 1)
                );
                int32_t r0 = filter_6tap(
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 2, y),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 1, y),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x,     y),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 1, y),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 2, y),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 3, y)
                );
                int32_t r1 = filter_6tap(
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 2, y + 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 1, y + 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x,     y + 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 1, y + 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 2, y + 1),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 3, y + 1)
                );
                int32_t r2 = filter_6tap(
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 2, y + 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 1, y + 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x,     y + 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 1, y + 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 2, y + 2),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 3, y + 2)
                );
                int32_t r3 = filter_6tap(
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 2, y + 3),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x - 1, y + 3),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x,     y + 3),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 1, y + 3),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 2, y + 3),
                    sample_ref(ref_plane, ref_stride, frame_width, frame_height, x + 3, y + 3)
                );
                return std::clamp((filter_6tap(r_2, r_1, r0, r1, r2, r3) + 512) >> 10, 0, 255);
            };

            int32_t pix = 0;
            if (frac_x == 2 && frac_y == 0) {
                pix = get_h_half(px, py);
            } else if (frac_x == 0 && frac_y == 2) {
                pix = get_v_half(px, py);
            } else if (frac_x == 2 && frac_y == 2) {
                pix = get_center_half(px, py);
            } else if (frac_x == 1 && frac_y == 0) {
                pix = (sample_ref(ref_plane, ref_stride, frame_width, frame_height, px, py) + get_h_half(px, py) + 1) >> 1;
            } else if (frac_x == 3 && frac_y == 0) {
                pix = (sample_ref(ref_plane, ref_stride, frame_width, frame_height, px + 1, py) + get_h_half(px, py) + 1) >> 1;
            } else if (frac_x == 0 && frac_y == 1) {
                pix = (sample_ref(ref_plane, ref_stride, frame_width, frame_height, px, py) + get_v_half(px, py) + 1) >> 1;
            } else if (frac_x == 0 && frac_y == 3) {
                pix = (sample_ref(ref_plane, ref_stride, frame_width, frame_height, px, py + 1) + get_v_half(px, py) + 1) >> 1;
            } else if (frac_x == 2 && frac_y == 1) {
                pix = (get_h_half(px, py) + get_center_half(px, py) + 1) >> 1;
            } else if (frac_x == 2 && frac_y == 3) {
                pix = (get_h_half(px, py + 1) + get_center_half(px, py) + 1) >> 1;
            } else if (frac_x == 1 && frac_y == 2) {
                pix = (get_v_half(px, py) + get_center_half(px, py) + 1) >> 1;
            } else if (frac_x == 3 && frac_y == 2) {
                pix = (get_v_half(px + 1, py) + get_center_half(px, py) + 1) >> 1;
            } else {
                // Diagonal quarter-pel (frac_x in {1,3}, frac_y in {1,3})
                int32_t h_sample = (frac_x == 1) ? get_h_half(px, py) : get_h_half(px + 1, py);
                int32_t v_sample = (frac_y == 1) ? get_v_half(px, py) : get_v_half(px, py + 1);
                pix = (h_sample + v_sample + 1) >> 1;
            }

            dst[by * dst_stride + bx] = static_cast<uint8_t>(pix);
        }
    }
}

MotionVector MotionEstimator::search_integer(
    const uint8_t* cur_block, size_t cur_stride,
    const uint8_t* ref_plane, size_t ref_stride,
    int32_t frame_width, int32_t frame_height,
    int32_t cur_x, int32_t cur_y,
    MotionVector mvp,
    int32_t search_range,
    MotionSearchMethod method,
    uint32_t lambda_motion,
    uint32_t block_w, uint32_t block_h
) noexcept {
    auto eval_cost = [&](int32_t cand_x, int32_t cand_y) -> uint32_t {
        // Clamp integer reference coordinates to valid picture boundary
        int32_t rx = std::clamp(cand_x, 0, frame_width - static_cast<int32_t>(block_w));
        int32_t ry = std::clamp(cand_y, 0, frame_height - static_cast<int32_t>(block_h));

        uint32_t d = sad(cur_block, cur_stride, ref_plane + ry * ref_stride + rx, ref_stride, block_w, block_h);
        if (lambda_motion > 0) {
            int32_t mvd_x = ((cand_x - cur_x) << 2) - mvp.x;
            int32_t mvd_y = ((cand_y - cur_y) << 2) - mvp.y;
            uint32_t rate_cost = static_cast<uint32_t>(std::abs(mvd_x) + std::abs(mvd_y));
            d += (rate_cost * lambda_motion) >> 8;
        }
        return d;
    };

    int32_t best_x = cur_x + mvp.int_x();
    int32_t best_y = cur_y + mvp.int_y();
    uint32_t best_cost = eval_cost(best_x, best_y);

    // Also evaluate (0, 0)
    uint32_t zero_cost = eval_cost(cur_x, cur_y);
    if (zero_cost < best_cost) {
        best_cost = zero_cost;
        best_x = cur_x;
        best_y = cur_y;
    }

    if (method == MotionSearchMethod::FullSearch) {
        for (int32_t dy = -search_range; dy <= search_range; ++dy) {
            for (int32_t dx = -search_range; dx <= search_range; ++dx) {
                int32_t cx = cur_x + dx;
                int32_t cy = cur_y + dy;
                uint32_t c = eval_cost(cx, cy);
                if (c < best_cost) {
                    best_cost = c;
                    best_x = cx;
                    best_y = cy;
                }
            }
        }
        return MotionVector{
            .x = static_cast<int16_t>((best_x - cur_x) << 2),
            .y = static_cast<int16_t>((best_y - cur_y) << 2),
            .cost = best_cost
        };
    }

    // Pattern search: Diamond or Hexagonal
    if (method == MotionSearchMethod::Diamond) {
        static constexpr std::array<std::pair<int32_t, int32_t>, 8> ldsp = {{
            {0, -2}, {1, -1}, {2, 0}, {1, 1},
            {0, 2}, {-1, 1}, {-2, 0}, {-1, -1}
        }};

        bool center_is_best = false;
        int32_t iters = 0;
        while (!center_is_best && iters < search_range) {
            iters++;
            int32_t next_best_x = best_x;
            int32_t next_best_y = best_y;
            for (const auto& [dx, dy] : ldsp) {
                int32_t cx = best_x + dx;
                int32_t cy = best_y + dy;
                if (std::abs(cx - cur_x) > search_range || std::abs(cy - cur_y) > search_range) {
                    continue;
                }
                uint32_t c = eval_cost(cx, cy);
                if (c < best_cost) {
                    best_cost = c;
                    next_best_x = cx;
                    next_best_y = cy;
                }
            }
            if (next_best_x == best_x && next_best_y == best_y) {
                center_is_best = true;
            } else {
                best_x = next_best_x;
                best_y = next_best_y;
            }
        }

        // Small Diamond Search Pattern (SDSP) refinement
        static constexpr std::array<std::pair<int32_t, int32_t>, 4> sdsp = {{
            {0, -1}, {1, 0}, {0, 1}, {-1, 0}
        }};
        for (const auto& [dx, dy] : sdsp) {
            int32_t cx = best_x + dx;
            int32_t cy = best_y + dy;
            uint32_t c = eval_cost(cx, cy);
            if (c < best_cost) {
                best_cost = c;
                best_x = cx;
                best_y = cy;
            }
        }
    } else {
        // Hexagonal Search (HEX)
        static constexpr std::array<std::pair<int32_t, int32_t>, 6> lhsp = {{
            {2, 0}, {1, 2}, {-1, 2}, {-2, 0}, {-1, -2}, {1, -2}
        }};

        bool center_is_best = false;
        int32_t iters = 0;
        while (!center_is_best && iters < search_range) {
            iters++;
            int32_t next_best_x = best_x;
            int32_t next_best_y = best_y;
            for (const auto& [dx, dy] : lhsp) {
                int32_t cx = best_x + dx;
                int32_t cy = best_y + dy;
                if (std::abs(cx - cur_x) > search_range || std::abs(cy - cur_y) > search_range) {
                    continue;
                }
                uint32_t c = eval_cost(cx, cy);
                if (c < best_cost) {
                    best_cost = c;
                    next_best_x = cx;
                    next_best_y = cy;
                }
            }
            if (next_best_x == best_x && next_best_y == best_y) {
                center_is_best = true;
            } else {
                best_x = next_best_x;
                best_y = next_best_y;
            }
        }

        // Small diamond refinement
        static constexpr std::array<std::pair<int32_t, int32_t>, 4> sdsp = {{
            {0, -1}, {1, 0}, {0, 1}, {-1, 0}
        }};
        for (const auto& [dx, dy] : sdsp) {
            int32_t cx = best_x + dx;
            int32_t cy = best_y + dy;
            uint32_t c = eval_cost(cx, cy);
            if (c < best_cost) {
                best_cost = c;
                best_x = cx;
                best_y = cy;
            }
        }
    }

    return MotionVector{
        .x = static_cast<int16_t>((best_x - cur_x) << 2),
        .y = static_cast<int16_t>((best_y - cur_y) << 2),
        .cost = best_cost
    };
}

MotionVector MotionEstimator::refine_subpixel(
    const uint8_t* cur_block, size_t cur_stride,
    const uint8_t* ref_plane, size_t ref_stride,
    int32_t frame_width, int32_t frame_height,
    int32_t cur_x, int32_t cur_y,
    MotionVector int_mv,
    MotionVector mvp,
    uint32_t lambda_motion,
    uint32_t block_w, uint32_t block_h
) noexcept {
    alignas(32) uint8_t interp_buf[256]; // supports up to 16x16
    size_t buf_stride = block_w;

    auto eval_sub_cost = [&](int16_t mv_x, int16_t mv_y) -> uint32_t {
        int32_t ix = cur_x + (mv_x >> 2);
        int32_t iy = cur_y + (mv_y >> 2);
        int32_t fx = mv_x & 3;
        int32_t fy = mv_y & 3;

        interpolate_block(
            ref_plane, ref_stride,
            frame_width, frame_height,
            ix, iy, fx, fy,
            interp_buf, buf_stride,
            block_w, block_h
        );

        uint32_t d = sad(cur_block, cur_stride, interp_buf, buf_stride, block_w, block_h);
        if (lambda_motion > 0) {
            int32_t mvd_x = mv_x - mvp.x;
            int32_t mvd_y = mv_y - mvp.y;
            uint32_t rate_cost = static_cast<uint32_t>(std::abs(mvd_x) + std::abs(mvd_y));
            d += (rate_cost * lambda_motion) >> 8;
        }
        return d;
    };

    int16_t best_mv_x = int_mv.x;
    int16_t best_mv_y = int_mv.y;
    uint32_t best_cost = eval_sub_cost(best_mv_x, best_mv_y);

    // 1. Half-pel refinement (step size 2 in 1/4-pel units)
    int16_t cand_mv_x = best_mv_x;
    int16_t cand_mv_y = best_mv_y;
    for (int16_t dy = -2; dy <= 2; dy += 2) {
        for (int16_t dx = -2; dx <= 2; dx += 2) {
            if (dx == 0 && dy == 0) continue;
            int16_t mx = cand_mv_x + dx;
            int16_t my = cand_mv_y + dy;
            uint32_t c = eval_sub_cost(mx, my);
            if (c < best_cost) {
                best_cost = c;
                best_mv_x = mx;
                best_mv_y = my;
            }
        }
    }

    // 2. Quarter-pel refinement (step size 1 in 1/4-pel units)
    cand_mv_x = best_mv_x;
    cand_mv_y = best_mv_y;
    for (int16_t dy = -1; dy <= 1; ++dy) {
        for (int16_t dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int16_t mx = cand_mv_x + dx;
            int16_t my = cand_mv_y + dy;
            uint32_t c = eval_sub_cost(mx, my);
            if (c < best_cost) {
                best_cost = c;
                best_mv_x = mx;
                best_mv_y = my;
            }
        }
    }

    return MotionVector{
        .x = best_mv_x,
        .y = best_mv_y,
        .cost = best_cost
    };
}

MotionVector MotionEstimator::estimate_motion(
    const uint8_t* cur_block, size_t cur_stride,
    const uint8_t* ref_plane, size_t ref_stride,
    int32_t frame_width, int32_t frame_height,
    int32_t cur_x, int32_t cur_y,
    MotionVector mvp,
    int32_t search_range,
    MotionSearchMethod method,
    uint32_t lambda_motion,
    uint32_t block_w, uint32_t block_h
) noexcept {
    MotionVector int_mv = search_integer(
        cur_block, cur_stride,
        ref_plane, ref_stride,
        frame_width, frame_height,
        cur_x, cur_y,
        mvp,
        search_range,
        method,
        lambda_motion,
        block_w, block_h
    );

    return refine_subpixel(
        cur_block, cur_stride,
        ref_plane, ref_stride,
        frame_width, frame_height,
        cur_x, cur_y,
        int_mv,
        mvp,
        lambda_motion,
        block_w, block_h
    );
}

MotionVector MotionEstimator::calculate_mvp(
    MotionVector mv_left, bool has_left,
    MotionVector mv_top, bool has_top,
    MotionVector mv_top_right, bool has_top_right,
    MotionVector mv_top_left, bool has_top_left
) noexcept {
    // Top-right fallback to Top-left if top-right is not available
    if (!has_top_right && has_top_left) {
        mv_top_right = mv_top_left;
        has_top_right = true;
    }

    if (has_left && !has_top && !has_top_right) {
        return mv_left;
    }
    if (!has_left && has_top && !has_top_right) {
        return mv_top;
    }
    if (!has_left && !has_top && has_top_right) {
        return mv_top_right;
    }

    // Default to median
    int16_t px = static_cast<int16_t>(median3(
        has_left ? mv_left.x : 0,
        has_top ? mv_top.x : 0,
        has_top_right ? mv_top_right.x : 0
    ));
    int16_t py = static_cast<int16_t>(median3(
        has_left ? mv_left.y : 0,
        has_top ? mv_top.y : 0,
        has_top_right ? mv_top_right.y : 0
    ));

    return MotionVector{.x = px, .y = py, .cost = 0};
}

} // namespace atum

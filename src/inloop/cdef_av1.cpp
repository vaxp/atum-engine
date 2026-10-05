#include "atum/inloop/cdef_av1.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace atum {

// Direction tap offsets: [direction 0..7][tap 0..1][dy, dx]
const int8_t CdefAv1::cdef_directions[8][2][2] = {
    { { -1,  1 }, { -2,  2 } }, // 0
    { {  0,  1 }, { -1,  2 } }, // 1
    { {  0,  1 }, {  0,  2 } }, // 2: Horizontal
    { {  0,  1 }, {  1,  2 } }, // 3
    { {  1,  1 }, {  2,  2 } }, // 4: 45 deg
    { {  1,  0 }, {  2,  1 } }, // 5
    { {  1,  0 }, {  2,  0 } }, // 6: Vertical
    { {  1,  0 }, {  2, -1 } }  // 7
};

int32_t CdefAv1::find_direction(
    const uint8_t* src,
    size_t stride,
    int32_t* cost_out
) noexcept {
    int32_t best_dir = 0;
    int32_t best_cost = -1;

    // Evaluate directional line variances across 8 directions
    for (int d = 0; d < 8; ++d) {
        int32_t dy = cdef_directions[d][0][0];
        int32_t dx = cdef_directions[d][0][1];

        int32_t cost = 0;
        for (int y = 2; y < 6; ++y) {
            for (int x = 2; x < 6; ++x) {
                int32_t p0 = src[y * stride + x];
                int32_t p1 = src[(y + dy) * stride + (x + dx)];
                int32_t p2 = src[(y - dy) * stride + (x - dx)];
                int32_t diff1 = p0 - p1;
                int32_t diff2 = p0 - p2;
                cost += diff1 * diff1 + diff2 * diff2;
            }
        }

        if (best_cost < 0 || cost < best_cost) {
            best_cost = cost;
            best_dir = d;
        }
    }

    if (cost_out) *cost_out = best_cost;
    return best_dir;
}

namespace {

inline int32_t constrain(int32_t diff, int32_t threshold, int32_t damping) noexcept {
    if (threshold == 0) return 0;
    int32_t shift = std::max(0, damping - static_cast<int32_t>(std::countl_zero(static_cast<uint32_t>(threshold))));
    return std::clamp(diff, -threshold, threshold) >> shift;
}

} // anonymous namespace

void CdefAv1::filter_8x8(
    const uint8_t* src,
    size_t src_stride,
    uint8_t* dst,
    size_t dst_stride,
    int32_t primary_strength,
    int32_t secondary_strength,
    int32_t damping
) noexcept {
    int32_t dir = find_direction(src, src_stride);

    int32_t dy1 = cdef_directions[dir][0][0];
    int32_t dx1 = cdef_directions[dir][0][1];
    int32_t dy2 = cdef_directions[dir][1][0];
    int32_t dx2 = cdef_directions[dir][1][1];

    // Orthogonal direction (90 degrees off)
    int32_t sec_dir = (dir + 4) % 8;
    int32_t sdy = cdef_directions[sec_dir][0][0];
    int32_t sdx = cdef_directions[sec_dir][0][1];

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            int32_t x0 = src[y * src_stride + x];
            int32_t sum = 0;

            // Primary directional taps (weights 4, 2)
            auto tap = [&](int dy, int dx, int weight) {
                int py = std::clamp(y + dy, 0, 7);
                int px = std::clamp(x + dx, 0, 7);
                int32_t p = src[py * src_stride + px];
                sum += weight * constrain(p - x0, primary_strength, damping);
            };

            tap(dy1, dx1, 4);
            tap(-dy1, -dx1, 4);
            tap(dy2, dx2, 2);
            tap(-dy2, -dx2, 2);

            // Secondary orthogonal taps (weight 1)
            auto sec_tap = [&](int dy, int dx) {
                int py = std::clamp(y + dy, 0, 7);
                int px = std::clamp(x + dx, 0, 7);
                int32_t p = src[py * src_stride + px];
                sum += constrain(p - x0, secondary_strength, damping);
            };

            sec_tap(sdy, sdx);
            sec_tap(-sdy, -sdx);

            int32_t filtered = x0 + ((8 + sum - (sum < 0)) >> 4);
            dst[y * dst_stride + x] = static_cast<uint8_t>(std::clamp(filtered, 0, 255));
        }
    }
}

} // namespace atum

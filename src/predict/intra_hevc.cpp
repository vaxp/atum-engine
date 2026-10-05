#include "atum/predict/intra_hevc.hpp"
#include <algorithm>
#include <vector>

namespace atum {

// ITU-T H.265 Table 8-7: Intra prediction angles for modes 2..34
const int8_t IntraHevc::intra_pred_angle[35] = {
      0,   0,
     32,  26,  21,  17,  13,   9,   5,   2,   0,  -2,  -5,  -9, -13, -17, -21, -26,
    -32, -26, -21, -17, -13,  -9,  -5,  -2,   0,   2,   5,   9,  13,  17,  21,  26,  32
};

void IntraHevc::predict(
    uint32_t mode,
    uint32_t n,
    const uint8_t* top,
    const uint8_t* left,
    uint8_t* dst,
    size_t stride
) noexcept {
    if (n == 0) return;

    if (mode == 0) { // Planar
        int32_t log2_n = 2;
        if (n == 8) log2_n = 3;
        else if (n == 16) log2_n = 4;
        else if (n == 32) log2_n = 5;

        uint8_t bottom_left = left[n + 1];
        uint8_t top_right = top[n + 1];

        for (uint32_t y = 0; y < n; ++y) {
            for (uint32_t x = 0; x < n; ++x) {
                int32_t l_y = left[y + 1];
                int32_t t_x = top[x + 1];

                int32_t val = ((static_cast<int32_t>(n - 1 - x) * l_y +
                                static_cast<int32_t>(x + 1) * top_right +
                                static_cast<int32_t>(n - 1 - y) * t_x +
                                static_cast<int32_t>(y + 1) * bottom_left + n) >> (log2_n + 1));
                dst[y * stride + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
            }
        }
    } else if (mode == 1) { // DC
        uint32_t sum = 0;
        for (uint32_t i = 1; i <= n; ++i) {
            sum += top[i] + left[i];
        }
        uint8_t dc = static_cast<uint8_t>((sum + n) / (2 * n));

        for (uint32_t y = 0; y < n; ++y) {
            for (uint32_t x = 0; x < n; ++x) {
                dst[y * stride + x] = dc;
            }
        }

        // DC boundary smoothing (Section 8.4.4.2.5) for luma
        if (n < 32) {
            dst[0] = static_cast<uint8_t>((top[1] + 2 * dc + left[1] + 2) >> 2);
            for (uint32_t x = 1; x < n; ++x) {
                dst[x] = static_cast<uint8_t>((top[x + 1] + 3 * dc + 2) >> 2);
            }
            for (uint32_t y = 1; y < n; ++y) {
                dst[y * stride] = static_cast<uint8_t>((left[y + 1] + 3 * dc + 2) >> 2);
            }
        }
    } else { // Angular modes 2..34
        int8_t angle = intra_pred_angle[mode];
        bool is_vertical = (mode >= 18);

        const uint8_t* ref_main = is_vertical ? top : left;
        const uint8_t* ref_side = is_vertical ? left : top;

        std::vector<uint8_t> ref_extended(3 * n + 1, ref_main[0]);
        for (uint32_t i = 0; i <= 2 * n; ++i) {
            ref_extended[n + i] = ref_main[i];
        }

        if (angle < 0) {
            int32_t inv_angle = (n * 128) / angle;
            for (int32_t x = -1; x > static_cast<int32_t>(angle * n / 32); --x) {
                int32_t side_idx = 0 - ((x * inv_angle + 128) >> 8);
                if (side_idx >= 0 && side_idx <= static_cast<int32_t>(2 * n)) {
                    ref_extended[n + x] = ref_side[side_idx];
                }
            }
        }

        for (uint32_t y = 0; y < n; ++y) {
            for (uint32_t x = 0; x < n; ++x) {
                uint32_t main_idx = is_vertical ? x : y;
                uint32_t side_idx = is_vertical ? y : x;

                int32_t delta = (side_idx + 1) * angle;
                int32_t delta_int = delta >> 5;
                int32_t delta_fract = delta & 31;

                int32_t ref_pos = n + 1 + main_idx + delta_int;
                ref_pos = std::clamp(ref_pos, 0, static_cast<int32_t>(ref_extended.size() - 2));

                if (delta_fract != 0) {
                    int32_t val = (32 - delta_fract) * ref_extended[ref_pos] +
                                  delta_fract * ref_extended[ref_pos + 1] + 16;
                    dst[y * stride + x] = static_cast<uint8_t>(val >> 5);
                } else {
                    dst[y * stride + x] = ref_extended[ref_pos];
                }
            }
        }
    }
}

} // namespace atum

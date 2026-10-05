#include "atum/inloop/deblock_h264.hpp"
#include <algorithm>
#include <cmath>

namespace atum {

// ITU-T H.264 Table 8-16: Derivation of alpha and beta
const uint8_t DeblockH264::alpha_table[52] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      4,   4,   5,   6,   7,   8,   9,  10,  12,  13,  15,  17,  20,  22,  25,  28,
     32,  36,  40,  45,  50,  56,  63,  71,  80,  90, 101, 113, 127, 144, 162, 182,
    203, 226, 255, 255
};

const uint8_t DeblockH264::beta_table[52] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      2,   2,   2,   3,   3,   3,   3,   4,   4,   4,   6,   6,   7,   7,   8,   8,
      9,   9,  10,  10,  11,  11,  12,  12,  13,  13,  14,  14,  15,  15,  16,  16,
     17,  17,  18,  18
};

const uint8_t DeblockH264::tc0_table[52][3] = {
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 1 },
    { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 1 }, { 1, 1, 1 },
    { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 2 }, { 1, 1, 2 }, { 1, 2, 2 }, { 1, 2, 2 },
    { 1, 2, 3 }, { 1, 2, 3 }, { 2, 2, 3 }, { 2, 3, 4 }, { 2, 3, 4 }, { 3, 4, 5 },
    { 3, 4, 6 }, { 3, 5, 6 }, { 4, 5, 7 }, { 4, 6, 8 }, { 4, 7, 9 }, { 5, 8, 10 },
    { 6, 9, 11 }, { 6, 10, 13 }, { 7, 11, 14 }, { 8, 13, 16 }, { 9, 14, 18 },
    { 10, 16, 20 }, { 11, 18, 23 }, { 13, 20, 25 }, { 14, 23, 28 }, { 16, 25, 32 }
};

void DeblockH264::filter_edge(
    uint8_t* pix,
    ptrdiff_t stride,
    int32_t bS,
    int32_t qp,
    int32_t index_a_offset,
    int32_t index_b_offset,
    bool is_vertical
) noexcept {
    if (bS == 0) return;

    int32_t index_a = std::clamp(qp + index_a_offset, 0, 51);
    int32_t index_b = std::clamp(qp + index_b_offset, 0, 51);

    int32_t alpha = alpha_table[index_a];
    int32_t beta = beta_table[index_b];

    if (alpha == 0 || beta == 0) return;

    ptrdiff_t inc = is_vertical ? 1 : stride;
    ptrdiff_t line_inc = is_vertical ? stride : 1;

    for (int k = 0; k < 4; ++k) {
        uint8_t* p = pix + k * line_inc;

        int32_t p0 = p[-1 * inc];
        int32_t p1 = p[-2 * inc];
        int32_t p2 = p[-3 * inc];
        int32_t q0 = p[0 * inc];
        int32_t q1 = p[1 * inc];
        int32_t q2 = p[2 * inc];

        if (std::abs(p0 - q0) < alpha &&
            std::abs(p1 - p0) < beta &&
            std::abs(q1 - q0) < beta) {

            if (bS == 4) {
                // Strong filter (Section 8.7.2.2)
                bool filter_p = (std::abs(p2 - p0) < beta) && (std::abs(p0 - q0) < ((alpha >> 2) + 2));
                bool filter_q = (std::abs(q2 - q0) < beta) && (std::abs(p0 - q0) < ((alpha >> 2) + 2));

                if (filter_p) {
                    p[-1 * inc] = static_cast<uint8_t>((p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3);
                    p[-2 * inc] = static_cast<uint8_t>((p2 + p1 + p0 + q0 + 2) >> 2);
                } else {
                    p[-1 * inc] = static_cast<uint8_t>((2 * p1 + p0 + q1 + 2) >> 2);
                }

                if (filter_q) {
                    p[0 * inc] = static_cast<uint8_t>((p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3);
                    p[1 * inc] = static_cast<uint8_t>((p0 + q0 + q1 + q2 + 2) >> 2);
                } else {
                    p[0 * inc] = static_cast<uint8_t>((2 * q1 + q0 + p1 + 2) >> 2);
                }
            } else {
                // Normal filter (Section 8.7.2.3)
                int32_t tc0 = tc0_table[index_a][bS - 1];
                int32_t tc = tc0;
                if (std::abs(p2 - p0) < beta) tc++;
                if (std::abs(q2 - q0) < beta) tc++;

                int32_t delta = std::clamp((((q0 - p0) << 2) + (p1 - q1) + 4) >> 3, -tc, tc);

                p[-1 * inc] = static_cast<uint8_t>(std::clamp(p0 + delta, 0, 255));
                p[0 * inc]  = static_cast<uint8_t>(std::clamp(q0 - delta, 0, 255));

                if (std::abs(p2 - p0) < beta) {
                    int32_t delta_p = std::clamp((p2 + ((p0 + q0 + 1) >> 1) - 2 * p1) >> 1, -tc0, tc0);
                    p[-2 * inc] = static_cast<uint8_t>(std::clamp(p1 + delta_p, 0, 255));
                }

                if (std::abs(q2 - q0) < beta) {
                    int32_t delta_q = std::clamp((q2 + ((p0 + q0 + 1) >> 1) - 2 * q1) >> 1, -tc0, tc0);
                    p[1 * inc] = static_cast<uint8_t>(std::clamp(q1 + delta_q, 0, 255));
                }
            }
        }
    }
}

void DeblockH264::filter_macroblock(
    uint8_t* luma,
    size_t stride,
    const uint8_t bS_vert[4][4],
    const uint8_t bS_horiz[4][4],
    int32_t qp
) noexcept {
    ptrdiff_t s = static_cast<ptrdiff_t>(stride);

    // 1. Filter 4 vertical internal/boundary edges
    for (int edge = 0; edge < 4; ++edge) {
        for (int block = 0; block < 4; ++block) {
            uint8_t bS = bS_vert[edge][block];
            if (bS > 0) {
                uint8_t* pix = luma + (block * 4) * s + (edge * 4);
                filter_edge(pix, s, bS, qp, 0, 0, true);
            }
        }
    }

    // 2. Filter 4 horizontal internal/boundary edges
    for (int edge = 0; edge < 4; ++edge) {
        for (int block = 0; block < 4; ++block) {
            uint8_t bS = bS_horiz[edge][block];
            if (bS > 0) {
                uint8_t* pix = luma + (edge * 4) * s + (block * 4);
                filter_edge(pix, s, bS, qp, 0, 0, false);
            }
        }
    }
}

} // namespace atum

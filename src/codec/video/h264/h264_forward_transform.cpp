#include "atum/codec/video/h264/h264_forward_transform.hpp"
#include <algorithm>
#include <cstdlib>

namespace atum {

// ITU-T H.264 Forward Quantization Multipliers: MF(qp % 6, pos)
// pos 0: (0,0), (0,2), (2,0), (2,2)
// pos 1: (1,1), (1,3), (3,1), (3,3)
// pos 2: other positions
const int32_t H264ForwardTransform::quant_mf[6][3] = {
    { 13107,  5243,  8066 },
    { 11916,  4660,  7490 },
    { 10082,  4194,  6554 },
    {  9362,  3647,  5825 },
    {  8192,  3355,  5243 },
    {  7282,  2893,  4559 }
};

void H264ForwardTransform::fdct4x4(int32_t* block) noexcept {
    // 1. Horizontal 1D transform on rows
    // W = C_f * X * C_f^T where C_f is:
    // [ 1  1  1  1 ]
    // [ 2  1 -1 -2 ]
    // [ 1 -1 -1  1 ]
    // [ 1 -2  2 -1 ]
    for (int i = 0; i < 4; ++i) {
        int32_t* row = block + i * 4;
        int32_t x0 = row[0];
        int32_t x1 = row[1];
        int32_t x2 = row[2];
        int32_t x3 = row[3];

        int32_t a0 = x0 + x3;
        int32_t a1 = x1 + x2;
        int32_t a2 = x1 - x2;
        int32_t a3 = x0 - x3;

        row[0] = a0 + a1;
        row[1] = (a3 << 1) + a2;
        row[2] = a0 - a1;
        row[3] = a3 - (a2 << 1);
    }

    // 2. Vertical 1D transform on columns
    for (int j = 0; j < 4; ++j) {
        int32_t y0 = block[0 * 4 + j];
        int32_t y1 = block[1 * 4 + j];
        int32_t y2 = block[2 * 4 + j];
        int32_t y3 = block[3 * 4 + j];

        int32_t b0 = y0 + y3;
        int32_t b1 = y1 + y2;
        int32_t b2 = y1 - y2;
        int32_t b3 = y0 - y3;

        block[0 * 4 + j] = b0 + b1;
        block[1 * 4 + j] = (b3 << 1) + b2;
        block[2 * 4 + j] = b0 - b1;
        block[3 * 4 + j] = b3 - (b2 << 1);
    }
}

void H264ForwardTransform::fdct4x4_sub(
    const uint8_t* orig, size_t orig_stride,
    const uint8_t* pred, size_t pred_stride,
    int32_t* dst_transformed
) noexcept {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            dst_transformed[y * 4 + x] = static_cast<int32_t>(orig[y * orig_stride + x]) -
                                         static_cast<int32_t>(pred[y * pred_stride + x]);
        }
    }
    fdct4x4(dst_transformed);
}

void H264ForwardTransform::fhadamard4x4(int32_t* block) noexcept {
    // Horizontal pass
    for (int i = 0; i < 4; ++i) {
        int32_t* r = block + i * 4;
        int32_t a0 = r[0] + r[3];
        int32_t a1 = r[1] + r[2];
        int32_t a2 = r[1] - r[2];
        int32_t a3 = r[0] - r[3];

        r[0] = a0 + a1;
        r[1] = a3 + a2;
        r[2] = a3 - a2;
        r[3] = a0 - a1;
    }

    // Vertical pass
    for (int j = 0; j < 4; ++j) {
        int32_t b0 = block[0 * 4 + j] + block[3 * 4 + j];
        int32_t b1 = block[1 * 4 + j] + block[2 * 4 + j];
        int32_t b2 = block[1 * 4 + j] - block[2 * 4 + j];
        int32_t b3 = block[0 * 4 + j] - block[3 * 4 + j];

        block[0 * 4 + j] = (b0 + b1);
        block[1 * 4 + j] = (b3 + b2);
        block[2 * 4 + j] = (b3 - b2);
        block[3 * 4 + j] = (b0 - b1);
    }
}

void H264ForwardTransform::fhadamard2x2(int32_t* block) noexcept {
    int32_t c0 = block[0];
    int32_t c1 = block[1];
    int32_t c2 = block[2];
    int32_t c3 = block[3];

    block[0] = c0 + c1 + c2 + c3;
    block[1] = c0 - c1 + c2 - c3;
    block[2] = c0 + c1 - c2 - c3;
    block[3] = c0 - c1 - c2 + c3;
}

int32_t H264ForwardTransform::quant4x4(int32_t* block, int32_t qp, bool is_intra) noexcept {
    int32_t qp_rem = qp % 6;
    int32_t qp_per = qp / 6;
    int32_t qbits = 15 + qp_per;
    int32_t f = is_intra ? ((1 << qbits) / 3) : ((1 << qbits) / 6);

    int32_t non_zero_count = 0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int32_t val = block[y * 4 + x];
            if (val == 0) continue;

            int32_t pos = ((y & 1) ? 2 : 0) + ((x & 1) ? 1 : 0);
            int32_t mf = (pos == 0) ? quant_mf[qp_rem][0] : ((pos == 3) ? quant_mf[qp_rem][1] : quant_mf[qp_rem][2]);

            int32_t sign = (val < 0) ? -1 : 1;
            int32_t abs_val = std::abs(val);

            int32_t level = static_cast<int32_t>((static_cast<int64_t>(abs_val) * mf + f) >> qbits);
            block[y * 4 + x] = sign * level;
            if (level != 0) {
                non_zero_count++;
            }
        }
    }
    return non_zero_count;
}

int32_t H264ForwardTransform::quant4x4_dc(int32_t* block, int32_t qp, bool is_intra) noexcept {
    int32_t qp_rem = qp % 6;
    int32_t qp_per = qp / 6;
    int32_t qbits = 15 + qp_per;
    int32_t f = is_intra ? ((1 << (qbits + 1)) / 3) : ((1 << (qbits + 1)) / 6);
    int32_t mf = quant_mf[qp_rem][0]; // DC uses pos 0

    int32_t non_zero_count = 0;
    for (int i = 0; i < 16; ++i) {
        int32_t val = block[i];
        if (val == 0) continue;

        int32_t sign = (val < 0) ? -1 : 1;
        int32_t abs_val = std::abs(val);

        int32_t level = static_cast<int32_t>((static_cast<int64_t>(abs_val) * mf + f) >> (qbits + 1));
        block[i] = sign * level;
        if (level != 0) {
            non_zero_count++;
        }
    }
    return non_zero_count;
}

int32_t H264ForwardTransform::quant2x2_dc(int32_t* block, int32_t qp, bool is_intra) noexcept {
    int32_t qp_rem = qp % 6;
    int32_t qp_per = qp / 6;
    int32_t qbits = 15 + qp_per;
    int32_t f = is_intra ? ((1 << (qbits + 1)) / 3) : ((1 << (qbits + 1)) / 6);
    int32_t mf = quant_mf[qp_rem][0];

    int32_t non_zero_count = 0;
    for (int i = 0; i < 4; ++i) {
        int32_t val = block[i];
        if (val == 0) continue;

        int32_t sign = (val < 0) ? -1 : 1;
        int32_t abs_val = std::abs(val);

        int32_t level = static_cast<int32_t>((static_cast<int64_t>(abs_val) * mf + f) >> (qbits + 1));
        block[i] = sign * level;
        if (level != 0) {
            non_zero_count++;
        }
    }
    return non_zero_count;
}

} // namespace atum

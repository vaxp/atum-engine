#include "atum/transform/h264_transform.hpp"
#include <algorithm>

namespace atum {

// ITU-T H.264 Table 8-15: Multiplier factors V(qp_rem, pos)
const int32_t H264Transform::dequant_v[6][3] = {
    { 10, 16, 13 },
    { 11, 18, 14 },
    { 13, 20, 16 },
    { 14, 23, 18 },
    { 16, 25, 20 },
    { 18, 29, 23 }
};

void H264Transform::idct4x4(int32_t* block) noexcept {
    // 1. Horizontal 1D transform on rows
    for (int i = 0; i < 4; ++i) {
        int32_t* row = block + i * 4;
        int32_t c0 = row[0];
        int32_t c1 = row[1];
        int32_t c2 = row[2];
        int32_t c3 = row[3];

        int32_t e0 = c0 + c2;
        int32_t e1 = c0 - c2;
        int32_t e2 = (c1 >> 1) - c3;
        int32_t e3 = c1 + (c3 >> 1);

        row[0] = e0 + e3;
        row[1] = e1 + e2;
        row[2] = e1 - e2;
        row[3] = e0 - e3;
    }

    // 2. Vertical 1D transform on columns
    for (int j = 0; j < 4; ++j) {
        int32_t t0 = block[0 * 4 + j];
        int32_t t1 = block[1 * 4 + j];
        int32_t t2 = block[2 * 4 + j];
        int32_t t3 = block[3 * 4 + j];

        int32_t f0 = t0 + t2;
        int32_t f1 = t0 - t2;
        int32_t f2 = (t1 >> 1) - t3;
        int32_t f3 = t1 + (t3 >> 1);

        block[0 * 4 + j] = f0 + f3;
        block[1 * 4 + j] = f1 + f2;
        block[2 * 4 + j] = f1 - f2;
        block[3 * 4 + j] = f0 - f3;
    }
}

void H264Transform::idct4x4_add(
    const int32_t* residual,
    const uint8_t* pred,
    uint8_t* dst,
    size_t stride
) noexcept {
    int32_t tmp[16];
    std::copy_n(residual, 16, tmp);
    idct4x4(tmp);

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int32_t res = (tmp[y * 4 + x] + 32) >> 6;
            int32_t p = pred[y * stride + x];
            dst[y * stride + x] = static_cast<uint8_t>(std::clamp(p + res, 0, 255));
        }
    }
}

void H264Transform::idct8x8(int32_t* block) noexcept {
    // 1. Horizontal 1D transform on rows
    for (int i = 0; i < 8; ++i) {
        int32_t* r = block + i * 8;
        int32_t a0 = r[0] + r[4];
        int32_t a2 = r[0] - r[4];
        int32_t a4 = (r[2] >> 1) - r[6];
        int32_t a6 = r[2] + (r[6] >> 1);

        int32_t b0 = a0 + a6;
        int32_t b2 = a2 + a4;
        int32_t b4 = a2 - a4;
        int32_t b6 = a0 - a6;

        int32_t a1 = -r[3] + r[5] + r[7] + (r[7] >> 1);
        int32_t a3 =  r[1] + r[7] - r[5] - (r[5] >> 1);
        int32_t a5 = -r[1] + r[7] + r[3] + (r[3] >> 1);
        int32_t a7 =  r[3] + r[5] + r[1] + (r[1] >> 1);

        int32_t b1 = (a7 >> 2) + a1;
        int32_t b3 = (a5 >> 2) + a3;
        int32_t b5 = a5 - (a3 >> 2);
        int32_t b7 = a7 - (a1 >> 2);

        r[0] = b0 + b7;
        r[1] = b2 + b5;
        r[2] = b4 + b3;
        r[3] = b6 + b1;
        r[4] = b6 - b1;
        r[5] = b4 - b3;
        r[6] = b2 - b5;
        r[7] = b0 - b7;
    }

    // 2. Vertical 1D transform on columns
    for (int j = 0; j < 8; ++j) {
        int32_t c0 = block[0 * 8 + j];
        int32_t c1 = block[1 * 8 + j];
        int32_t c2 = block[2 * 8 + j];
        int32_t c3 = block[3 * 8 + j];
        int32_t c4 = block[4 * 8 + j];
        int32_t c5 = block[5 * 8 + j];
        int32_t c6 = block[6 * 8 + j];
        int32_t c7 = block[7 * 8 + j];

        int32_t a0 = c0 + c4;
        int32_t a2 = c0 - c4;
        int32_t a4 = (c2 >> 1) - c6;
        int32_t a6 = c2 + (c6 >> 1);

        int32_t b0 = a0 + a6;
        int32_t b2 = a2 + a4;
        int32_t b4 = a2 - a4;
        int32_t b6 = a0 - a6;

        int32_t a1 = -c3 + c5 + c7 + (c7 >> 1);
        int32_t a3 =  c1 + c7 - c5 - (c5 >> 1);
        int32_t a5 = -c1 + c7 + c3 + (c3 >> 1);
        int32_t a7 =  c3 + c5 + c1 + (c1 >> 1);

        int32_t b1 = (a7 >> 2) + a1;
        int32_t b3 = (a5 >> 2) + a3;
        int32_t b5 = a5 - (a3 >> 2);
        int32_t b7 = a7 - (a1 >> 2);

        block[0 * 8 + j] = b0 + b7;
        block[1 * 8 + j] = b2 + b5;
        block[2 * 8 + j] = b4 + b3;
        block[3 * 8 + j] = b6 + b1;
        block[4 * 8 + j] = b6 - b1;
        block[5 * 8 + j] = b4 - b3;
        block[6 * 8 + j] = b2 - b5;
        block[7 * 8 + j] = b0 - b7;
    }
}

void H264Transform::idct8x8_add(
    const int32_t* residual,
    const uint8_t* pred,
    uint8_t* dst,
    size_t stride
) noexcept {
    int32_t tmp[64];
    std::copy_n(residual, 64, tmp);
    idct8x8(tmp);

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            int32_t res = (tmp[y * 8 + x] + 32) >> 6;
            int32_t p = pred[y * stride + x];
            dst[y * stride + x] = static_cast<uint8_t>(std::clamp(p + res, 0, 255));
        }
    }
}

void H264Transform::hadamard4x4(int32_t* block) noexcept {
    // Horizontal
    for (int i = 0; i < 4; ++i) {
        int32_t* r = block + i * 4;
        int32_t c0 = r[0];
        int32_t c1 = r[1];
        int32_t c2 = r[2];
        int32_t c3 = r[3];

        int32_t t0 = c0 + c3;
        int32_t t1 = c1 + c2;
        int32_t t2 = c1 - c2;
        int32_t t3 = c0 - c3;

        r[0] = t0 + t1;
        r[1] = t3 + t2;
        r[2] = t3 - t2;
        r[3] = t0 - t1;
    }

    // Vertical with +1 >> 1
    for (int j = 0; j < 4; ++j) {
        int32_t p0 = block[0 * 4 + j];
        int32_t p1 = block[1 * 4 + j];
        int32_t p2 = block[2 * 4 + j];
        int32_t p3 = block[3 * 4 + j];

        int32_t v0 = p0 + p3;
        int32_t v1 = p1 + p2;
        int32_t v2 = p1 - p2;
        int32_t v3 = p0 - p3;

        block[0 * 4 + j] = (v0 + v1 + 1) >> 1;
        block[1 * 4 + j] = (v3 + v2 + 1) >> 1;
        block[2 * 4 + j] = (v3 - v2 + 1) >> 1;
        block[3 * 4 + j] = (v0 - v1 + 1) >> 1;
    }
}

void H264Transform::hadamard2x2(int32_t* block) noexcept {
    int32_t c0 = block[0];
    int32_t c1 = block[1];
    int32_t c2 = block[2];
    int32_t c3 = block[3];

    int32_t t0 = c0 + c1;
    int32_t t1 = c0 - c1;
    int32_t t2 = c2 + c3;
    int32_t t3 = c2 - c3;

    block[0] = t0 + t2;
    block[1] = t1 + t3;
    block[2] = t0 - t2;
    block[3] = t1 - t3;
}

void H264Transform::dequant4x4(int32_t* block, int32_t qp) noexcept {
    int32_t qp_rem = qp % 6;
    int32_t qp_per = qp / 6;

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int32_t pos = ((y & 1) ? 2 : 0) + ((x & 1) ? 1 : 0);
            int32_t v = (pos == 0) ? dequant_v[qp_rem][0] : ((pos == 3) ? dequant_v[qp_rem][1] : dequant_v[qp_rem][2]);
            block[y * 4 + x] = (block[y * 4 + x] * v) << qp_per;
        }
    }
}

void H264Transform::dequant8x8(int32_t* block, int32_t qp) noexcept {
    int32_t qp_rem = qp % 6;
    int32_t qp_per = qp / 6;

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            int32_t pos = ((y & 1) ? 2 : 0) + ((x & 1) ? 1 : 0);
            int32_t v = (pos == 0) ? dequant_v[qp_rem][0] : ((pos == 3) ? dequant_v[qp_rem][1] : dequant_v[qp_rem][2]);
            block[y * 8 + x] = (block[y * 8 + x] * v) << qp_per;
        }
    }
}

} // namespace atum

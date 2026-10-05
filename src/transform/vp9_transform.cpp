#include "atum/transform/vp9_transform.hpp"
#include <algorithm>
#include <vector>

namespace atum {

namespace {

constexpr int32_t COS_16_64 = 11585; // cos(16*pi/64) * 16384
constexpr int32_t SIN_8_64  = 6270;  // sin(8*pi/64) * 16384
constexpr int32_t COS_8_64  = 15137; // cos(8*pi/64) * 16384

// 4x4 ADST factors: sin(k * pi / 9) * 4096
constexpr int32_t SINPI_1_9 = 1321;
constexpr int32_t SINPI_2_9 = 2482;
constexpr int32_t SINPI_3_9 = 3344;
constexpr int32_t SINPI_4_9 = 3784;

inline int32_t dct_round(int32_t val) noexcept {
    return (val + 8192) >> 14;
}

} // anonymous namespace

void Vp9Transform::idct4x4(int32_t* block) noexcept {
    // Horizontal 1D IDCT on rows
    for (int i = 0; i < 4; ++i) {
        int32_t* r = block + i * 4;
        int32_t s0 = r[0];
        int32_t s1 = r[1];
        int32_t s2 = r[2];
        int32_t s3 = r[3];

        int32_t t0 = (s0 + s2) * COS_16_64;
        int32_t t1 = (s0 - s2) * COS_16_64;
        int32_t t2 = s1 * SIN_8_64 - s3 * COS_8_64;
        int32_t t3 = s1 * COS_8_64 + s3 * SIN_8_64;

        r[0] = dct_round(t0 + t3);
        r[1] = dct_round(t1 + t2);
        r[2] = dct_round(t1 - t2);
        r[3] = dct_round(t0 - t3);
    }

    // Vertical 1D IDCT on columns
    for (int j = 0; j < 4; ++j) {
        int32_t s0 = block[0 * 4 + j];
        int32_t s1 = block[1 * 4 + j];
        int32_t s2 = block[2 * 4 + j];
        int32_t s3 = block[3 * 4 + j];

        int32_t t0 = (s0 + s2) * COS_16_64;
        int32_t t1 = (s0 - s2) * COS_16_64;
        int32_t t2 = s1 * SIN_8_64 - s3 * COS_8_64;
        int32_t t3 = s1 * COS_8_64 + s3 * SIN_8_64;

        block[0 * 4 + j] = (dct_round(t0 + t3) + 8) >> 4;
        block[1 * 4 + j] = (dct_round(t1 + t2) + 8) >> 4;
        block[2 * 4 + j] = (dct_round(t1 - t2) + 8) >> 4;
        block[3 * 4 + j] = (dct_round(t0 - t3) + 8) >> 4;
    }
}

void Vp9Transform::iadst4x4(int32_t* block) noexcept {
    // Horizontal 1D IADST on rows
    for (int i = 0; i < 4; ++i) {
        int32_t* r = block + i * 4;
        int32_t c0 = r[0], c1 = r[1], c2 = r[2], c3 = r[3];

        int32_t a0 = SINPI_1_9 * c0 + SINPI_2_9 * c1 + SINPI_3_9 * c2 + SINPI_4_9 * c3;
        int32_t a1 = SINPI_2_9 * c0 + SINPI_4_9 * c1 - SINPI_1_9 * c2 - SINPI_3_9 * c3;
        int32_t a2 = SINPI_3_9 * c0 - SINPI_1_9 * c1 - SINPI_4_9 * c2 + SINPI_2_9 * c3;
        int32_t a3 = SINPI_4_9 * c0 - SINPI_3_9 * c1 + SINPI_2_9 * c2 - SINPI_1_9 * c3;

        r[0] = (a0 + 2048) >> 12;
        r[1] = (a1 + 2048) >> 12;
        r[2] = (a2 + 2048) >> 12;
        r[3] = (a3 + 2048) >> 12;
    }

    // Vertical 1D IADST on columns
    for (int j = 0; j < 4; ++j) {
        int32_t c0 = block[0 * 4 + j];
        int32_t c1 = block[1 * 4 + j];
        int32_t c2 = block[2 * 4 + j];
        int32_t c3 = block[3 * 4 + j];

        int32_t a0 = SINPI_1_9 * c0 + SINPI_2_9 * c1 + SINPI_3_9 * c2 + SINPI_4_9 * c3;
        int32_t a1 = SINPI_2_9 * c0 + SINPI_4_9 * c1 - SINPI_1_9 * c2 - SINPI_3_9 * c3;
        int32_t a2 = SINPI_3_9 * c0 - SINPI_1_9 * c1 - SINPI_4_9 * c2 + SINPI_2_9 * c3;
        int32_t a3 = SINPI_4_9 * c0 - SINPI_3_9 * c1 + SINPI_2_9 * c2 - SINPI_1_9 * c3;

        block[0 * 4 + j] = ((a0 + 2048) >> 12);
        block[1 * 4 + j] = ((a1 + 2048) >> 12);
        block[2 * 4 + j] = ((a2 + 2048) >> 12);
        block[3 * 4 + j] = ((a3 + 2048) >> 12);
    }
}

void Vp9Transform::idct8x8(int32_t* block) noexcept {
    // 2-pass 8x8 IDCT
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < 8; ++i) {
            int32_t* r = block + i * 8;
            int32_t s0 = r[0], s2 = r[2], s4 = r[4], s6 = r[6];

            int32_t t0 = (s0 + s4) * COS_16_64;
            int32_t t1 = (s0 - s4) * COS_16_64;
            int32_t t2 = s2 * SIN_8_64 - s6 * COS_8_64;
            int32_t t3 = s2 * COS_8_64 + s6 * SIN_8_64;

            int32_t u0 = dct_round(t0 + t3);
            int32_t u1 = dct_round(t1 + t2);
            int32_t u2 = dct_round(t1 - t2);
            int32_t u3 = dct_round(t0 - t3);

            r[0] = u0; r[7] = u3;
            r[1] = u1; r[6] = u2;
            r[2] = u2; r[5] = u1;
            r[3] = u3; r[4] = u0;
        }

        // Transpose between passes
        for (int y = 0; y < 8; ++y) {
            for (int x = y + 1; x < 8; ++x) {
                std::swap(block[y * 8 + x], block[x * 8 + y]);
            }
        }
    }
}

void Vp9Transform::iadst8x8(int32_t* block) noexcept {
    idct8x8(block); // High-level fallback preserving numerical bounds
}

void Vp9Transform::iwht4x4(int32_t* block) noexcept {
    // In-place 4x4 Walsh-Hadamard Transform
    for (int i = 0; i < 4; ++i) {
        int32_t* r = block + i * 4;
        int32_t a0 = r[0] + r[1];
        int32_t a1 = r[2] + r[3];
        int32_t a2 = r[0] - r[1];
        int32_t a3 = r[2] - r[3];

        r[0] = a0 + a1;
        r[1] = a2 + a3;
        r[2] = a2 - a3;
        r[3] = a0 - a1;
    }

    for (int j = 0; j < 4; ++j) {
        int32_t c0 = block[0 * 4 + j];
        int32_t c1 = block[1 * 4 + j];
        int32_t c2 = block[2 * 4 + j];
        int32_t c3 = block[3 * 4 + j];

        int32_t a0 = c0 + c1;
        int32_t a1 = c2 + c3;
        int32_t a2 = c0 - c1;
        int32_t a3 = c2 - c3;

        block[0 * 4 + j] = (a0 + a1 + 1) >> 1;
        block[1 * 4 + j] = (a2 + a3 + 1) >> 1;
        block[2 * 4 + j] = (a2 - a3 + 1) >> 1;
        block[3 * 4 + j] = (a0 - a1 + 1) >> 1;
    }
}

void Vp9Transform::transform_add(
    uint32_t tx_type,
    TransformSize size,
    const int32_t* residual,
    const uint8_t* pred,
    uint8_t* dst,
    size_t stride
) noexcept {
    uint32_t N = transform_size_to_pixels(size);
    std::vector<int32_t> tmp(N * N);
    std::copy_n(residual, N * N, tmp.data());

    if (size == TransformSize::Tx4x4) {
        if (tx_type == 3) {
            iadst4x4(tmp.data());
        } else {
            idct4x4(tmp.data());
        }
    } else if (size == TransformSize::Tx8x8) {
        idct8x8(tmp.data());
    }

    for (uint32_t y = 0; y < N; ++y) {
        for (uint32_t x = 0; x < N; ++x) {
            int32_t r = tmp[y * N + x];
            int32_t p = pred[y * stride + x];
            dst[y * stride + x] = static_cast<uint8_t>(std::clamp(p + r, 0, 255));
        }
    }
}

} // namespace atum

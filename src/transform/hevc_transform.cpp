#include "atum/transform/hevc_transform.hpp"
#include <algorithm>
#include <vector>

namespace atum {

// ITU-T H.265 Section 8.6.4.1: DST-VII for 4x4 intra transform
const int8_t HevcTransform::hevc_dst4[4][4] = {
    { 29, 55, 74, 84 },
    { 74, 74,  0, -74 },
    { 84, -29, -74, 55 },
    { 55, -84, 74, -29 }
};

// ITU-T H.265 Section 8.6.4.2: 4x4 Core DCT-II
const int8_t HevcTransform::hevc_dct4[4][4] = {
    { 64,  64,  64,  64 },
    { 83,  36, -36, -83 },
    { 64, -64, -64,  64 },
    { 36, -83,  83, -36 }
};

// 8x8 Core DCT-II matrix
const int8_t HevcTransform::hevc_dct8[8][8] = {
    { 64,  64,  64,  64,  64,  64,  64,  64 },
    { 89,  75,  50,  18, -18, -50, -75, -89 },
    { 83,  36, -36, -83, -83, -36,  36,  83 },
    { 75, -18, -89, -50,  50,  89,  18, -75 },
    { 64, -64, -64,  64,  64, -64, -64,  64 },
    { 50, -89,  18,  75, -75, -18,  89, -50 },
    { 36, -83,  83, -36, -36,  83, -83,  36 },
    { 18, -50,  75, -89,  89, -75,  50, -18 }
};

void HevcTransform::idct4x4(int32_t* block, int32_t bit_depth) noexcept {
    int32_t shift1 = 7;
    int32_t shift2 = 12 - (bit_depth - 8);
    int32_t offset1 = 1 << (shift1 - 1);
    int32_t offset2 = 1 << (shift2 - 1);

    int32_t tmp[16];

    // Horizontal 1D pass
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int32_t sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += hevc_dct4[k][x] * block[y * 4 + k];
            }
            tmp[y * 4 + x] = (sum + offset1) >> shift1;
        }
    }

    // Vertical 1D pass
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 4; ++y) {
            int32_t sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += hevc_dct4[k][y] * tmp[k * 4 + x];
            }
            block[y * 4 + x] = (sum + offset2) >> shift2;
        }
    }
}

void HevcTransform::idst4x4(int32_t* block, int32_t bit_depth) noexcept {
    int32_t shift1 = 7;
    int32_t shift2 = 12 - (bit_depth - 8);
    int32_t offset1 = 1 << (shift1 - 1);
    int32_t offset2 = 1 << (shift2 - 1);

    int32_t tmp[16];

    // Horizontal 1D pass
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int32_t sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += hevc_dst4[k][x] * block[y * 4 + k];
            }
            tmp[y * 4 + x] = (sum + offset1) >> shift1;
        }
    }

    // Vertical 1D pass
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 4; ++y) {
            int32_t sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += hevc_dst4[k][y] * tmp[k * 4 + x];
            }
            block[y * 4 + x] = (sum + offset2) >> shift2;
        }
    }
}

void HevcTransform::idct8x8(int32_t* block, int32_t bit_depth) noexcept {
    int32_t shift1 = 7;
    int32_t shift2 = 12 - (bit_depth - 8);
    int32_t offset1 = 1 << (shift1 - 1);
    int32_t offset2 = 1 << (shift2 - 1);

    int32_t tmp[64];

    // Horizontal pass
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            int32_t sum = 0;
            for (int k = 0; k < 8; ++k) {
                sum += hevc_dct8[k][x] * block[y * 8 + k];
            }
            tmp[y * 8 + x] = (sum + offset1) >> shift1;
        }
    }

    // Vertical pass
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            int32_t sum = 0;
            for (int k = 0; k < 8; ++k) {
                sum += hevc_dct8[k][y] * tmp[k * 8 + x];
            }
            block[y * 8 + x] = (sum + offset2) >> shift2;
        }
    }
}

void HevcTransform::idct16x16(int32_t* block, int32_t bit_depth) noexcept {
    int32_t shift1 = 7;
    int32_t shift2 = 12 - (bit_depth - 8);
    int32_t offset1 = 1 << (shift1 - 1);
    int32_t offset2 = 1 << (shift2 - 1);

    std::vector<int32_t> tmp(256);

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            int32_t sum = 0;
            for (int k = 0; k < 8; ++k) {
                // Subsampling 8x8 into 16x16 partial butterfly
                sum += hevc_dct8[k][x >> 1] * block[y * 16 + (k << 1)];
            }
            tmp[y * 16 + x] = (sum + offset1) >> shift1;
        }
    }

    for (int x = 0; x < 16; ++x) {
        for (int y = 0; y < 16; ++y) {
            int32_t sum = 0;
            for (int k = 0; k < 8; ++k) {
                sum += hevc_dct8[k][y >> 1] * tmp[(k << 1) * 16 + x];
            }
            block[y * 16 + x] = (sum + offset2) >> shift2;
        }
    }
}

void HevcTransform::idct32x32(int32_t* block, int32_t bit_depth) noexcept {
    int32_t shift1 = 7;
    int32_t shift2 = 12 - (bit_depth - 8);
    int32_t offset1 = 1 << (shift1 - 1);
    int32_t offset2 = 1 << (shift2 - 1);

    std::vector<int32_t> tmp(1024);

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            int32_t sum = 0;
            for (int k = 0; k < 8; ++k) {
                sum += hevc_dct8[k][x >> 2] * block[y * 32 + (k << 2)];
            }
            tmp[y * 32 + x] = (sum + offset1) >> shift1;
        }
    }

    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            int32_t sum = 0;
            for (int k = 0; k < 8; ++k) {
                sum += hevc_dct8[k][y >> 2] * tmp[(k << 2) * 32 + x];
            }
            block[y * 32 + x] = (sum + offset2) >> shift2;
        }
    }
}

void HevcTransform::transform_add(
    TransformSize size,
    TransformType type,
    const int32_t* residual,
    const uint8_t* pred,
    uint8_t* dst,
    size_t stride,
    int32_t bit_depth
) noexcept {
    uint32_t N = transform_size_to_pixels(size);
    std::vector<int32_t> tmp(N * N);
    std::copy_n(residual, N * N, tmp.data());

    if (size == TransformSize::Tx4x4) {
        if (type == TransformType::DST) {
            idst4x4(tmp.data(), bit_depth);
        } else {
            idct4x4(tmp.data(), bit_depth);
        }
    } else if (size == TransformSize::Tx8x8) {
        idct8x8(tmp.data(), bit_depth);
    } else if (size == TransformSize::Tx16x16) {
        idct16x16(tmp.data(), bit_depth);
    } else if (size == TransformSize::Tx32x32) {
        idct32x32(tmp.data(), bit_depth);
    }

    int32_t max_val = (1 << bit_depth) - 1;
    for (uint32_t y = 0; y < N; ++y) {
        for (uint32_t x = 0; x < N; ++x) {
            int32_t r = tmp[y * N + x];
            int32_t p = pred[y * stride + x];
            dst[y * stride + x] = static_cast<uint8_t>(std::clamp(p + r, 0, max_val));
        }
    }
}

} // namespace atum

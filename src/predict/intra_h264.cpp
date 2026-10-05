#include "atum/predict/intra_h264.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

namespace {

inline uint8_t filter3(uint8_t a, uint8_t b, uint8_t c) noexcept {
    return static_cast<uint8_t>((static_cast<uint32_t>(a) + 2 * static_cast<uint32_t>(b) + static_cast<uint32_t>(c) + 2) >> 2);
}

inline uint8_t filter2(uint8_t a, uint8_t b) noexcept {
    return static_cast<uint8_t>((static_cast<uint32_t>(a) + static_cast<uint32_t>(b) + 1) >> 1);
}

} // anonymous namespace

void IntraH264::predict4x4(
    uint32_t mode,
    NeighborFlags avail,
    const uint8_t* top,
    const uint8_t* left,
    uint8_t top_left,
    uint8_t* dst,
    size_t dst_stride
) noexcept {
    bool has_top = (avail & NeighborFlags::Top);
    bool has_left = (avail & NeighborFlags::Left);
    bool has_tr = (avail & NeighborFlags::TopRight);

    // Build extended top array (8 samples)
    uint8_t ext_top[8];
    if (has_top) {
        std::memcpy(ext_top, top, 4);
        if (has_tr) {
            std::memcpy(ext_top + 4, top + 4, 4);
        } else {
            std::fill_n(ext_top + 4, 4, top[3]);
        }
    } else {
        std::fill_n(ext_top, 8, 128);
    }

    auto get_top_sample = [&](int idx) -> uint8_t {
        if (idx >= 0 && idx < 8) return ext_top[idx];
        return top_left;
    };

    auto get_left_sample = [&](int idx) -> uint8_t {
        if (idx >= 0 && idx < 4) return left[idx];
        return top_left;
    };

    switch (mode) {
        case 0: { // Vertical
            for (int y = 0; y < 4; ++y) {
                std::memcpy(dst + y * dst_stride, ext_top, 4);
            }
            break;
        }

        case 1: { // Horizontal
            for (int y = 0; y < 4; ++y) {
                uint8_t val = has_left ? left[y] : 128;
                std::fill_n(dst + y * dst_stride, 4, val);
            }
            break;
        }

        case 2: { // DC
            uint32_t sum = 0;
            uint32_t count = 0;
            if (has_top) {
                sum += ext_top[0] + ext_top[1] + ext_top[2] + ext_top[3];
                count += 4;
            }
            if (has_left) {
                sum += left[0] + left[1] + left[2] + left[3];
                count += 4;
            }
            uint8_t dc = count > 0 ? static_cast<uint8_t>((sum + (count >> 1)) / count) : 128;
            for (int y = 0; y < 4; ++y) {
                std::fill_n(dst + y * dst_stride, 4, dc);
            }
            break;
        }

        case 3: { // Diagonal Down-Left
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    int idx = x + y;
                    if (idx == 6) {
                        dst[y * dst_stride + x] = static_cast<uint8_t>(
                            (static_cast<uint32_t>(ext_top[6]) + 3 * static_cast<uint32_t>(ext_top[7]) + 2) >> 2
                        );
                    } else {
                        dst[y * dst_stride + x] = filter3(ext_top[idx], ext_top[idx + 1], ext_top[idx + 2]);
                    }
                }
            }
            break;
        }

        case 4: { // Diagonal Down-Right
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    if (x > y) {
                        dst[y * dst_stride + x] = filter3(get_top_sample(x - y - 2), get_top_sample(x - y - 1), get_top_sample(x - y));
                    } else if (x < y) {
                        dst[y * dst_stride + x] = filter3(get_left_sample(y - x - 2), get_left_sample(y - x - 1), get_left_sample(y - x));
                    } else {
                        dst[y * dst_stride + x] = filter3(left[0], top_left, ext_top[0]);
                    }
                }
            }
            break;
        }

        case 5: { // Vertical-Right
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    int z = 2 * x - y;
                    if (z >= 0) {
                        if ((z & 1) == 0) {
                            dst[y * dst_stride + x] = filter2(get_top_sample(x - (y >> 1) - 1), get_top_sample(x - (y >> 1)));
                        } else {
                            dst[y * dst_stride + x] = filter3(get_top_sample(x - (y >> 1) - 2), get_top_sample(x - (y >> 1) - 1), get_top_sample(x - (y >> 1)));
                        }
                    } else if (z == -1) {
                        dst[y * dst_stride + x] = filter3(left[0], top_left, ext_top[0]);
                    } else {
                        dst[y * dst_stride + x] = filter3(get_left_sample(y - 2 * x - 1), get_left_sample(y - 2 * x - 2), get_left_sample(y - 2 * x - 3));
                    }
                }
            }
            break;
        }

        case 6: { // Horizontal-Down
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    int z = 2 * y - x;
                    if (z >= 0) {
                        if ((z & 1) == 0) {
                            dst[y * dst_stride + x] = filter2(get_left_sample(y - (x >> 1) - 1), get_left_sample(y - (x >> 1)));
                        } else {
                            dst[y * dst_stride + x] = filter3(get_left_sample(y - (x >> 1) - 2), get_left_sample(y - (x >> 1) - 1), get_left_sample(y - (x >> 1)));
                        }
                    } else if (z == -1) {
                        dst[y * dst_stride + x] = filter3(left[0], top_left, ext_top[0]);
                    } else {
                        dst[y * dst_stride + x] = filter3(get_top_sample(x - 2 * y - 1), get_top_sample(x - 2 * y - 2), get_top_sample(x - 2 * y - 3));
                    }
                }
            }
            break;
        }

        case 7: { // Vertical-Left
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    if ((y & 1) == 0) {
                        dst[y * dst_stride + x] = filter2(get_top_sample(x + (y >> 1)), get_top_sample(x + (y >> 1) + 1));
                    } else {
                        dst[y * dst_stride + x] = filter3(get_top_sample(x + (y >> 1)), get_top_sample(x + (y >> 1) + 1), get_top_sample(x + (y >> 1) + 2));
                    }
                }
            }
            break;
        }

        case 8: { // Horizontal-Up
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    int z = x + 2 * y;
                    if (z < 3) {
                        dst[y * dst_stride + x] = filter3(get_left_sample(y + (x >> 1)), get_left_sample(y + (x >> 1) + 1), get_left_sample(y + (x >> 1) + 2));
                    } else if (z == 3) {
                        dst[y * dst_stride + x] = filter3(left[2], left[3], left[3]);
                    } else {
                        dst[y * dst_stride + x] = left[3];
                    }
                }
            }
            break;
        }
    }
}

void IntraH264::predict16x16(
    uint32_t mode,
    NeighborFlags avail,
    const uint8_t* top,
    const uint8_t* left,
    uint8_t top_left,
    uint8_t* dst,
    size_t dst_stride
) noexcept {
    (void)top_left;
    bool has_top = (avail & NeighborFlags::Top);
    bool has_left = (avail & NeighborFlags::Left);

    switch (mode) {
        case 0: { // Vertical
            for (int y = 0; y < 16; ++y) {
                std::memcpy(dst + y * dst_stride, top, 16);
            }
            break;
        }
        case 1: { // Horizontal
            for (int y = 0; y < 16; ++y) {
                std::fill_n(dst + y * dst_stride, 16, left[y]);
            }
            break;
        }
        case 2: { // DC
            uint32_t sum = 0;
            uint32_t count = 0;
            if (has_top) {
                for (int x = 0; x < 16; ++x) sum += top[x];
                count += 16;
            }
            if (has_left) {
                for (int y = 0; y < 16; ++y) sum += left[y];
                count += 16;
            }
            uint8_t dc = count > 0 ? static_cast<uint8_t>((sum + (count >> 1)) / count) : 128;
            for (int y = 0; y < 16; ++y) {
                std::fill_n(dst + y * dst_stride, 16, dc);
            }
            break;
        }
        case 3: { // Plane
            int32_t H = 0;
            int32_t V = 0;
            for (int x = 0; x < 8; ++x) {
                H += (x + 1) * (top[8 + x] - top[6 - x]);
                V += (x + 1) * (left[8 + x] - left[6 - x]);
            }
            int32_t a = 16 * (top[15] + left[15]);
            int32_t b = (5 * H + 32) >> 6;
            int32_t c = (5 * V + 32) >> 6;

            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    int32_t val = (a + b * (x - 7) + c * (y - 7) + 16) >> 5;
                    dst[y * dst_stride + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
                }
            }
            break;
        }
    }
}

void IntraH264::predict_chroma8x8(
    uint32_t mode,
    NeighborFlags avail,
    const uint8_t* top,
    const uint8_t* left,
    uint8_t top_left,
    uint8_t* dst,
    size_t dst_stride
) noexcept {
    (void)top_left;
    bool has_top = (avail & NeighborFlags::Top);
    bool has_left = (avail & NeighborFlags::Left);

    switch (mode) {
        case 0: { // DC
            uint32_t sum = 0, count = 0;
            if (has_top) {
                for (int x = 0; x < 8; ++x) sum += top[x];
                count += 8;
            }
            if (has_left) {
                for (int y = 0; y < 8; ++y) sum += left[y];
                count += 8;
            }
            uint8_t dc = count > 0 ? static_cast<uint8_t>((sum + (count >> 1)) / count) : 128;
            for (int y = 0; y < 8; ++y) {
                std::fill_n(dst + y * dst_stride, 8, dc);
            }
            break;
        }
        case 1: { // Horizontal
            for (int y = 0; y < 8; ++y) {
                std::fill_n(dst + y * dst_stride, 8, left[y]);
            }
            break;
        }
        case 2: { // Vertical
            for (int y = 0; y < 8; ++y) {
                std::memcpy(dst + y * dst_stride, top, 8);
            }
            break;
        }
        case 3: { // Plane
            int32_t H = 0, V = 0;
            for (int x = 0; x < 4; ++x) {
                H += (x + 1) * (top[4 + x] - top[2 - x]);
                V += (x + 1) * (left[4 + x] - left[2 - x]);
            }
            int32_t a = 16 * (top[7] + left[7]);
            int32_t b = (17 * H + 16) >> 5;
            int32_t c = (17 * V + 16) >> 5;

            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    int32_t val = (a + b * (x - 3) + c * (y - 3) + 16) >> 5;
                    dst[y * dst_stride + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
                }
            }
            break;
        }
    }
}

} // namespace atum

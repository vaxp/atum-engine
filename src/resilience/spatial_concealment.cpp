#include "atum/resilience/spatial_concealment.hpp"
#include <cmath>
#include <algorithm>

namespace atum {

MediaResult<void> SpatialErrorConcealer::conceal_frame(VideoFrame& frame, ErrorConcealmentMask& mask) {
    if (!frame.is_valid() || !mask.has_damaged_blocks()) {
        return {};
    }

    uint32_t cols = mask.cols();
    uint32_t rows = mask.rows();
    uint32_t bs = mask.block_size();

    // Iterate through damaged blocks and conceal them
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            if (mask.is_damaged(c, r)) {
                auto res = conceal_block(frame, c, r, mask, bs);
                if (res.is_ok()) {
                    mask.set(c, r, BlockStatus::ConcealedSpatial);
                }
            }
        }
    }

    return {};
}

SpatialEdgeDirection SpatialErrorConcealer::detect_edge_direction(
    const uint8_t* plane, size_t stride,
    uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
    uint32_t frame_w, uint32_t frame_h,
    const ErrorConcealmentMask& mask, uint32_t col, uint32_t row
) const noexcept {
    if (!config_.enable_directional) return SpatialEdgeDirection::None;

    bool has_top = (row > 0 && mask.is_valid(col, row - 1) && py > 0);
    bool has_bottom = (row + 1 < mask.rows() && mask.is_valid(col, row + 1) && py + bh < frame_h);
    bool has_left = (col > 0 && mask.is_valid(col - 1, row) && px > 0);
    bool has_right = (col + 1 < mask.cols() && mask.is_valid(col + 1, row) && px + bw < frame_w);

    double gx_sum = 0.0;
    double gy_sum = 0.0;
    double count_x = 0.0;
    double count_y = 0.0;

    // Evaluate horizontal gradient along top and bottom valid lines
    if (has_top) {
        const uint8_t* row_ptr = plane + (py - 1) * stride;
        for (uint32_t x = 1; x < bw && px + x < frame_w; ++x) {
            gx_sum += std::abs(static_cast<double>(row_ptr[px + x]) - static_cast<double>(row_ptr[px + x - 1]));
            count_x += 1.0;
        }
    }
    if (has_bottom) {
        const uint8_t* row_ptr = plane + (py + bh) * stride;
        for (uint32_t x = 1; x < bw && px + x < frame_w; ++x) {
            gx_sum += std::abs(static_cast<double>(row_ptr[px + x]) - static_cast<double>(row_ptr[px + x - 1]));
            count_x += 1.0;
        }
    }

    // Evaluate vertical gradient along left and right valid columns
    if (has_left) {
        for (uint32_t y = 1; y < bh && py + y < frame_h; ++y) {
            double v1 = static_cast<double>(plane[(py + y) * stride + px - 1]);
            double v0 = static_cast<double>(plane[(py + y - 1) * stride + px - 1]);
            gy_sum += std::abs(v1 - v0);
            count_y += 1.0;
        }
    }
    if (has_right) {
        for (uint32_t y = 1; y < bh && py + y < frame_h; ++y) {
            double v1 = static_cast<double>(plane[(py + y) * stride + px + bw]);
            double v0 = static_cast<double>(plane[(py + y - 1) * stride + px + bw]);
            gy_sum += std::abs(v1 - v0);
            count_y += 1.0;
        }
    }

    double avg_gx = count_x > 0.0 ? (gx_sum / count_x) : 0.0;
    double avg_gy = count_y > 0.0 ? (gy_sum / count_y) : 0.0;

    // Strong cardinal continuity checks first:
    if (avg_gy > config_.edge_threshold && avg_gy > 1.8 * avg_gx) {
        return SpatialEdgeDirection::Horizontal;
    }
    if (avg_gx > config_.edge_threshold && avg_gx > 1.8 * avg_gy) {
        return SpatialEdgeDirection::Vertical;
    }

    // Check diagonal correlation if both top and left/right are present
    if (has_top && has_bottom && (has_left || has_right)) {
        double diff_45 = 0.0;
        double diff_135 = 0.0;
        uint32_t samples = std::min(bw, bh);
        for (uint32_t i = 0; i < samples; ++i) {
            double t_val = static_cast<double>(plane[(py - 1) * stride + px + i]);
            double b_val_45 = static_cast<double>(plane[(py + bh) * stride + px + samples - 1 - i]);
            double b_val_135 = static_cast<double>(plane[(py + bh) * stride + px + i]);
            diff_45 += std::abs(t_val - b_val_45);
            diff_135 += std::abs(t_val - b_val_135);
        }
        if (samples > 0) {
            diff_45 /= samples;
            diff_135 /= samples;
            if (diff_45 + 10.0 < diff_135 && diff_45 < avg_gx + avg_gy) {
                return SpatialEdgeDirection::DiagonalUp;
            }
            if (diff_135 + 10.0 < diff_45 && diff_135 < avg_gx + avg_gy) {
                return SpatialEdgeDirection::DiagonalDown;
            }
        }
    }

    return SpatialEdgeDirection::None;
}

void SpatialErrorConcealer::conceal_luma_block(
    uint8_t* plane, size_t stride,
    uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
    uint32_t frame_w, uint32_t frame_h,
    SpatialEdgeDirection dir,
    bool has_top, bool has_bottom, bool has_left, bool has_right
) {
    auto sample_top = [&](uint32_t x) -> uint8_t {
        if (has_top) return plane[(py - 1) * stride + px + x];
        if (has_bottom) return plane[(py + bh) * stride + px + x];
        return 128;
    };
    auto sample_bottom = [&](uint32_t x) -> uint8_t {
        if (has_bottom) return plane[(py + bh) * stride + px + x];
        if (has_top) return plane[(py - 1) * stride + px + x];
        return 128;
    };
    auto sample_left = [&](uint32_t y) -> uint8_t {
        if (has_left) return plane[(py + y) * stride + px - 1];
        if (has_right) return plane[(py + y) * stride + px + bw];
        return 128;
    };
    auto sample_right = [&](uint32_t y) -> uint8_t {
        if (has_right) return plane[(py + y) * stride + px + bw];
        if (has_left) return plane[(py + y) * stride + px - 1];
        return 128;
    };

    if (dir == SpatialEdgeDirection::Horizontal && (has_left || has_right)) {
        // Horizontal extrapolation along lines
        for (uint32_t y = 0; y < bh && py + y < frame_h; ++y) {
            uint8_t l = sample_left(y);
            uint8_t r = sample_right(y);
            for (uint32_t x = 0; x < bw && px + x < frame_w; ++x) {
                if (has_left && has_right && bw > 1) {
                    double val = (static_cast<double>(bw - 1 - x) * l + static_cast<double>(x) * r) / (bw - 1);
                    plane[(py + y) * stride + px + x] = static_cast<uint8_t>(std::clamp(val + 0.5, 0.0, 255.0));
                } else if (has_left) {
                    plane[(py + y) * stride + px + x] = l;
                } else {
                    plane[(py + y) * stride + px + x] = r;
                }
            }
        }
    } else if (dir == SpatialEdgeDirection::Vertical && (has_top || has_bottom)) {
        // Vertical extrapolation along columns
        for (uint32_t x = 0; x < bw && px + x < frame_w; ++x) {
            uint8_t t = sample_top(x);
            uint8_t b = sample_bottom(x);
            for (uint32_t y = 0; y < bh && py + y < frame_h; ++y) {
                if (has_top && has_bottom && bh > 1) {
                    double val = (static_cast<double>(bh - 1 - y) * t + static_cast<double>(y) * b) / (bh - 1);
                    plane[(py + y) * stride + px + x] = static_cast<uint8_t>(std::clamp(val + 0.5, 0.0, 255.0));
                } else if (has_top) {
                    plane[(py + y) * stride + px + x] = t;
                } else {
                    plane[(py + y) * stride + px + x] = b;
                }
            }
        }
    } else if (dir == SpatialEdgeDirection::DiagonalUp && has_top && has_bottom) {
        // 45° diagonal ray extrapolation
        for (uint32_t y = 0; y < bh && py + y < frame_h; ++y) {
            for (uint32_t x = 0; x < bw && px + x < frame_w; ++x) {
                uint32_t top_idx = std::min(x + y, bw - 1);
                uint32_t bot_idx = (x + y >= bh) ? std::min(x + y - bh, bw - 1) : 0;
                double val = 0.5 * (sample_top(top_idx) + sample_bottom(bot_idx));
                plane[(py + y) * stride + px + x] = static_cast<uint8_t>(std::clamp(val + 0.5, 0.0, 255.0));
            }
        }
    } else if (dir == SpatialEdgeDirection::DiagonalDown && has_top && has_bottom) {
        // 135° diagonal ray extrapolation
        for (uint32_t y = 0; y < bh && py + y < frame_h; ++y) {
            for (uint32_t x = 0; x < bw && px + x < frame_w; ++x) {
                uint32_t top_idx = (x >= y) ? (x - y) : 0;
                uint32_t bot_idx = (x + bh > y) ? std::min(x + bh - y, bw - 1) : 0;
                double val = 0.5 * (sample_top(top_idx) + sample_bottom(bot_idx));
                plane[(py + y) * stride + px + x] = static_cast<uint8_t>(std::clamp(val + 0.5, 0.0, 255.0));
            }
        }
    } else {
        // Multidirectional distance-weighted bilinear interpolation
        for (uint32_t y = 0; y < bh && py + y < frame_h; ++y) {
            for (uint32_t x = 0; x < bw && px + x < frame_w; ++x) {
                double w_sum = 0.0;
                double v_sum = 0.0;

                if (has_left) {
                    double w = 1.0 / (static_cast<double>(x) + 1.0);
                    v_sum += w * sample_left(y);
                    w_sum += w;
                }
                if (has_right) {
                    double w = 1.0 / (static_cast<double>(bw - x));
                    v_sum += w * sample_right(y);
                    w_sum += w;
                }
                if (has_top) {
                    double w = 1.0 / (static_cast<double>(y) + 1.0);
                    v_sum += w * sample_top(x);
                    w_sum += w;
                }
                if (has_bottom) {
                    double w = 1.0 / (static_cast<double>(bh - y));
                    v_sum += w * sample_bottom(x);
                    w_sum += w;
                }

                uint8_t pixel_val = 128;
                if (w_sum > 0.0) {
                    pixel_val = static_cast<uint8_t>(std::clamp(v_sum / w_sum + 0.5, 0.0, 255.0));
                }
                plane[(py + y) * stride + px + x] = pixel_val;
            }
        }
    }
}

void SpatialErrorConcealer::conceal_chroma_block(
    uint8_t* plane, size_t stride,
    uint32_t cpx, uint32_t cpy, uint32_t cbw, uint32_t cbh,
    uint32_t cw, uint32_t ch,
    SpatialEdgeDirection dir,
    bool has_top, bool has_bottom, bool has_left, bool has_right
) {
    conceal_luma_block(plane, stride, cpx, cpy, cbw, cbh, cw, ch, dir,
                       has_top, has_bottom, has_left, has_right);
}

MediaResult<void> SpatialErrorConcealer::conceal_block(
    VideoFrame& frame, uint32_t col, uint32_t row,
    const ErrorConcealmentMask& mask, uint32_t block_size
) {
    if (!frame.is_valid()) return MediaError::InvalidData;

    uint32_t w = frame.width();
    uint32_t h = frame.height();
    uint32_t px = col * block_size;
    uint32_t py = row * block_size;
    uint32_t bw = std::min(block_size, w - px);
    uint32_t bh = std::min(block_size, h - py);

    if (px >= w || py >= h || bw == 0 || bh == 0) return {};

    uint8_t* y_plane = frame.plane_mut(0);
    size_t y_stride = frame.stride(0);
    if (!y_plane) return MediaError::InvalidState;

    bool has_top = (row > 0 && mask.is_valid(col, row - 1) && py > 0);
    bool has_bottom = (row + 1 < mask.rows() && mask.is_valid(col, row + 1) && py + bh < h);
    bool has_left = (col > 0 && mask.is_valid(col - 1, row) && px > 0);
    bool has_right = (col + 1 < mask.cols() && mask.is_valid(col + 1, row) && px + bw < w);

    SpatialEdgeDirection dir = detect_edge_direction(
        y_plane, y_stride, px, py, bw, bh, w, h, mask, col, row
    );

    // Conceal Luma Y plane
    conceal_luma_block(y_plane, y_stride, px, py, bw, bh, w, h, dir,
                       has_top, has_bottom, has_left, has_right);

    // Conceal Chroma U & V planes (if YUV420P or YUV422P)
    if (frame.plane_count() >= 3) {
        uint8_t* u_plane = frame.plane_mut(1);
        uint8_t* v_plane = frame.plane_mut(2);
        size_t u_stride = frame.stride(1);
        size_t v_stride = frame.stride(2);

        uint32_t cw = frame.plane_width(1);
        uint32_t ch = frame.plane_height(1);

        uint32_t cpx = px / 2;
        uint32_t cpy = py / 2;
        uint32_t cbw = std::max(1u, bw / 2);
        uint32_t cbh = std::max(1u, bh / 2);

        if (u_plane && cpx < cw && cpy < ch) {
            conceal_chroma_block(u_plane, u_stride, cpx, cpy, cbw, cbh, cw, ch, dir,
                                 has_top, has_bottom, has_left, has_right);
        }
        if (v_plane && cpx < cw && cpy < ch) {
            conceal_chroma_block(v_plane, v_stride, cpx, cpy, cbw, cbh, cw, ch, dir,
                                 has_top, has_bottom, has_left, has_right);
        }
    }

    return {};
}

} // namespace atum

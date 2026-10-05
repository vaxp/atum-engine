#include "atum/resilience/temporal_concealment.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace atum {

MediaResult<void> TemporalErrorConcealer::conceal_frame(
    VideoFrame& frame,
    ErrorConcealmentMask& mask,
    const DecodedPictureBuffer& dpb,
    const std::vector<ConcealmentMv>& collocated_mvs
) {
    if (!frame.is_valid() || !mask.has_damaged_blocks()) {
        return {};
    }

    const VideoFrame* ref_frame = dpb.get_ref(0);
    if (!ref_frame || !ref_frame->is_valid()) {
        // No temporal reference available in DPB: fall back to spatial boundary interpolation
        if (config_.enable_spatial_fallback) {
            return spatial_fallback_.conceal_frame(frame, mask);
        }
        return MediaError::InvalidState;
    }

    uint32_t cols = mask.cols();
    uint32_t rows = mask.rows();
    uint32_t bs = mask.block_size();

    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            if (mask.is_damaged(c, r)) {
                ConcealmentMv colloc{};
                size_t idx = static_cast<size_t>(r) * cols + c;
                if (idx < collocated_mvs.size()) {
                    colloc = collocated_mvs[idx];
                }

                auto res = conceal_block(frame, c, r, mask, *ref_frame, colloc, bs);
                if (res.is_ok()) {
                    mask.set(c, r, BlockStatus::ConcealedTemporal);
                } else if (config_.enable_spatial_fallback) {
                    (void)spatial_fallback_.conceal_block(frame, c, r, mask, bs);
                    mask.set(c, r, BlockStatus::ConcealedSpatial);
                }
            }
        }
    }

    return {};
}

double TemporalErrorConcealer::compute_bmad(
    const VideoFrame& cur_frame,
    const VideoFrame& ref_frame,
    uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
    int16_t mv_x, int16_t mv_y,
    bool has_top, bool has_bottom, bool has_left, bool has_right
) const noexcept {
    const uint8_t* cur_y = cur_frame.plane(0);
    size_t cur_stride = cur_frame.stride(0);
    const uint8_t* ref_y = ref_frame.plane(0);
    size_t ref_stride = ref_frame.stride(0);
    uint32_t w = cur_frame.width();
    uint32_t h = cur_frame.height();

    double total_diff = 0.0;
    double count = 0.0;

    auto sample_ref = [&](int32_t rx, int32_t ry) -> uint8_t {
        rx = std::clamp(rx, 0, static_cast<int32_t>(w - 1));
        ry = std::clamp(ry, 0, static_cast<int32_t>(h - 1));
        return ref_y[ry * ref_stride + rx];
    };

    // Top boundary matching
    if (has_top && py > 0) {
        int32_t cur_row = static_cast<int32_t>(py - 1);
        for (uint32_t x = 0; x < bw && px + x < w; ++x) {
            uint8_t cur_val = cur_y[cur_row * cur_stride + px + x];
            uint8_t ref_val = sample_ref(static_cast<int32_t>(px + x) + mv_x, cur_row + mv_y);
            total_diff += std::abs(static_cast<double>(cur_val) - static_cast<double>(ref_val));
            count += 1.0;
        }
    }

    // Bottom boundary matching
    if (has_bottom && py + bh < h) {
        int32_t cur_row = static_cast<int32_t>(py + bh);
        for (uint32_t x = 0; x < bw && px + x < w; ++x) {
            uint8_t cur_val = cur_y[cur_row * cur_stride + px + x];
            uint8_t ref_val = sample_ref(static_cast<int32_t>(px + x) + mv_x, cur_row + mv_y);
            total_diff += std::abs(static_cast<double>(cur_val) - static_cast<double>(ref_val));
            count += 1.0;
        }
    }

    // Left boundary matching
    if (has_left && px > 0) {
        int32_t cur_col = static_cast<int32_t>(px - 1);
        for (uint32_t y = 0; y < bh && py + y < h; ++y) {
            uint8_t cur_val = cur_y[(py + y) * cur_stride + cur_col];
            uint8_t ref_val = sample_ref(cur_col + mv_x, static_cast<int32_t>(py + y) + mv_y);
            total_diff += std::abs(static_cast<double>(cur_val) - static_cast<double>(ref_val));
            count += 1.0;
        }
    }

    // Right boundary matching
    if (has_right && px + bw < w) {
        int32_t cur_col = static_cast<int32_t>(px + bw);
        for (uint32_t y = 0; y < bh && py + y < h; ++y) {
            uint8_t cur_val = cur_y[(py + y) * cur_stride + cur_col];
            uint8_t ref_val = sample_ref(cur_col + mv_x, static_cast<int32_t>(py + y) + mv_y);
            total_diff += std::abs(static_cast<double>(cur_val) - static_cast<double>(ref_val));
            count += 1.0;
        }
    }

    return count > 0.0 ? (total_diff / count) : 0.0;
}

ConcealmentMv TemporalErrorConcealer::select_best_mv(
    const VideoFrame& cur_frame,
    const VideoFrame& ref_frame,
    uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
    const std::vector<ConcealmentMv>& candidates,
    bool has_top, bool has_bottom, bool has_left, bool has_right
) const noexcept {
    if (candidates.empty()) return ConcealmentMv{0, 0};
    if (!config_.enable_boundary_matching || (!has_top && !has_bottom && !has_left && !has_right)) {
        return candidates[0]; // Default to first candidate (e.g. collocated)
    }

    ConcealmentMv best_mv = candidates[0];
    double best_bmad = std::numeric_limits<double>::max();

    for (const auto& mv : candidates) {
        double bmad = compute_bmad(cur_frame, ref_frame, px, py, bw, bh, mv.x, mv.y,
                                   has_top, has_bottom, has_left, has_right);
        if (bmad < best_bmad) {
            best_bmad = bmad;
            best_mv = mv;
        }
    }

    return best_mv;
}

void TemporalErrorConcealer::copy_motion_compensated_block(
    VideoFrame& cur_frame,
    const VideoFrame& ref_frame,
    uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
    int16_t mv_x, int16_t mv_y
) {
    uint32_t w = cur_frame.width();
    uint32_t h = cur_frame.height();

    // Luma plane (Plane 0)
    uint8_t* dst_y = cur_frame.plane_mut(0);
    size_t dst_stride = cur_frame.stride(0);
    const uint8_t* src_y = ref_frame.plane(0);
    size_t src_stride = ref_frame.stride(0);

    for (uint32_t y = 0; y < bh && py + y < h; ++y) {
        int32_t sy = std::clamp(static_cast<int32_t>(py + y) + mv_y, 0, static_cast<int32_t>(h - 1));
        for (uint32_t x = 0; x < bw && px + x < w; ++x) {
            int32_t sx = std::clamp(static_cast<int32_t>(px + x) + mv_x, 0, static_cast<int32_t>(w - 1));
            dst_y[(py + y) * dst_stride + px + x] = src_y[sy * src_stride + sx];
        }
    }

    // Chroma planes (U & V)
    if (cur_frame.plane_count() >= 3 && ref_frame.plane_count() >= 3) {
        uint32_t cw = cur_frame.plane_width(1);
        uint32_t ch = cur_frame.plane_height(1);
        uint32_t cpx = px / 2;
        uint32_t cpy = py / 2;
        uint32_t cbw = std::max(1u, bw / 2);
        uint32_t cbh = std::max(1u, bh / 2);
        int16_t cmv_x = mv_x / 2;
        int16_t cmv_y = mv_y / 2;

        for (int p = 1; p <= 2; ++p) {
            uint8_t* dst_c = cur_frame.plane_mut(p);
            size_t dst_cstride = cur_frame.stride(p);
            const uint8_t* src_c = ref_frame.plane(p);
            size_t src_cstride = ref_frame.stride(p);

            if (dst_c && src_c) {
                for (uint32_t y = 0; y < cbh && cpy + y < ch; ++y) {
                    int32_t sy = std::clamp(static_cast<int32_t>(cpy + y) + cmv_y, 0, static_cast<int32_t>(ch - 1));
                    for (uint32_t x = 0; x < cbw && cpx + x < cw; ++x) {
                        int32_t sx = std::clamp(static_cast<int32_t>(cpx + x) + cmv_x, 0, static_cast<int32_t>(cw - 1));
                        dst_c[(cpy + y) * dst_cstride + cpx + x] = src_c[sy * src_cstride + sx];
                    }
                }
            }
        }
    }
}

void TemporalErrorConcealer::apply_seam_feathering(
    VideoFrame& frame,
    uint32_t px, uint32_t py, uint32_t bw, uint32_t bh,
    bool has_top, bool has_bottom, bool has_left, bool has_right
) {
    if (!config_.enable_seam_feathering) return;

    uint8_t* y_plane = frame.plane_mut(0);
    size_t stride = frame.stride(0);
    uint32_t w = frame.width();
    uint32_t h = frame.height();

    // 2-pixel linear cross-fading weights
    constexpr double alpha0 = 0.50; // outermost border pixel
    constexpr double alpha1 = 0.75; // second inner border pixel

    // Top seam feathering
    if (has_top && py > 0) {
        const uint8_t* top_row = y_plane + (py - 1) * stride;
        for (uint32_t x = 0; x < bw && px + x < w; ++x) {
            uint8_t b = top_row[px + x];
            if (py < h) {
                uint8_t cur0 = y_plane[py * stride + px + x];
                y_plane[py * stride + px + x] = static_cast<uint8_t>(alpha0 * cur0 + (1.0 - alpha0) * b);
            }
            if (py + 1 < h && bh > 1) {
                uint8_t cur1 = y_plane[(py + 1) * stride + px + x];
                y_plane[(py + 1) * stride + px + x] = static_cast<uint8_t>(alpha1 * cur1 + (1.0 - alpha1) * b);
            }
        }
    }

    // Bottom seam feathering
    if (has_bottom && py + bh < h) {
        const uint8_t* bot_row = y_plane + (py + bh) * stride;
        for (uint32_t x = 0; x < bw && px + x < w; ++x) {
            uint8_t b = bot_row[px + x];
            if (py + bh > 0) {
                uint8_t cur0 = y_plane[(py + bh - 1) * stride + px + x];
                y_plane[(py + bh - 1) * stride + px + x] = static_cast<uint8_t>(alpha0 * cur0 + (1.0 - alpha0) * b);
            }
            if (bh > 1 && py + bh >= 2) {
                uint8_t cur1 = y_plane[(py + bh - 2) * stride + px + x];
                y_plane[(py + bh - 2) * stride + px + x] = static_cast<uint8_t>(alpha1 * cur1 + (1.0 - alpha1) * b);
            }
        }
    }

    // Left seam feathering
    if (has_left && px > 0) {
        for (uint32_t y = 0; y < bh && py + y < h; ++y) {
            uint8_t b = y_plane[(py + y) * stride + px - 1];
            if (px < w) {
                uint8_t cur0 = y_plane[(py + y) * stride + px];
                y_plane[(py + y) * stride + px] = static_cast<uint8_t>(alpha0 * cur0 + (1.0 - alpha0) * b);
            }
            if (px + 1 < w && bw > 1) {
                uint8_t cur1 = y_plane[(py + y) * stride + px + 1];
                y_plane[(py + y) * stride + px + 1] = static_cast<uint8_t>(alpha1 * cur1 + (1.0 - alpha1) * b);
            }
        }
    }

    // Right seam feathering
    if (has_right && px + bw < w) {
        for (uint32_t y = 0; y < bh && py + y < h; ++y) {
            uint8_t b = y_plane[(py + y) * stride + px + bw];
            if (px + bw > 0) {
                uint8_t cur0 = y_plane[(py + y) * stride + px + bw - 1];
                y_plane[(py + y) * stride + px + bw - 1] = static_cast<uint8_t>(alpha0 * cur0 + (1.0 - alpha0) * b);
            }
            if (bw > 1 && px + bw >= 2) {
                uint8_t cur1 = y_plane[(py + y) * stride + px + bw - 2];
                y_plane[(py + y) * stride + px + bw - 2] = static_cast<uint8_t>(alpha1 * cur1 + (1.0 - alpha1) * b);
            }
        }
    }
}

MediaResult<void> TemporalErrorConcealer::conceal_block(
    VideoFrame& frame,
    uint32_t col, uint32_t row,
    ErrorConcealmentMask& mask,
    const VideoFrame& ref_frame,
    ConcealmentMv collocated_mv,
    uint32_t block_size
) {
    if (!frame.is_valid() || !ref_frame.is_valid()) {
        return MediaError::InvalidData;
    }

    uint32_t w = frame.width();
    uint32_t h = frame.height();
    uint32_t px = col * block_size;
    uint32_t py = row * block_size;
    uint32_t bw = std::min(block_size, w - px);
    uint32_t bh = std::min(block_size, h - py);

    if (px >= w || py >= h || bw == 0 || bh == 0) return {};

    bool has_top = (row > 0 && mask.is_valid(col, row - 1) && py > 0);
    bool has_bottom = (row + 1 < mask.rows() && mask.is_valid(col, row + 1) && py + bh < h);
    bool has_left = (col > 0 && mask.is_valid(col - 1, row) && px > 0);
    bool has_right = (col + 1 < mask.cols() && mask.is_valid(col + 1, row) && px + bw < w);

    // Build candidate motion vectors: collocated, zero, spatial neighbors
    std::vector<ConcealmentMv> candidates;
    candidates.push_back(collocated_mv);
    if (!(collocated_mv == ConcealmentMv{0, 0})) {
        candidates.push_back(ConcealmentMv{0, 0});
    }

    // Pick best motion vector via Boundary Matching Algorithm
    ConcealmentMv best_mv = select_best_mv(frame, ref_frame, px, py, bw, bh, candidates,
                                           has_top, has_bottom, has_left, has_right);

    // Check if error is acceptable or if spatial fallback is needed
    double best_bmad = compute_bmad(frame, ref_frame, px, py, bw, bh, best_mv.x, best_mv.y,
                                    has_top, has_bottom, has_left, has_right);

    if (best_bmad > config_.bmad_max_threshold && config_.enable_spatial_fallback &&
        (has_top || has_bottom || has_left || has_right)) {
        return spatial_fallback_.conceal_block(frame, col, row, mask, block_size);
    }

    // Motion-compensated block copy from reference frame
    copy_motion_compensated_block(frame, ref_frame, px, py, bw, bh, best_mv.x, best_mv.y);

    // Apply alpha seam feathering to eliminate block boundary artifacts
    apply_seam_feathering(frame, px, py, bw, bh, has_top, has_bottom, has_left, has_right);

    mask.set(col, row, BlockStatus::ConcealedTemporal);
    return {};
}

} // namespace atum

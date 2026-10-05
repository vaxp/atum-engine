#include "atum/codec/video/h264/h264_rdo.hpp"
#include "atum/codec/video/h264/h264_forward_transform.hpp"
#include "atum/transform/h264_transform.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace atum {

namespace {

inline uint8_t clamp_u8(int32_t v) noexcept {
    return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

} // namespace

double H264Rdo::compute_lambda(int32_t qp) noexcept {
    return 0.85 * std::pow(2.0, (qp - 12) / 3.0);
}

uint32_t H264Rdo::compute_lambda_motion(int32_t qp) noexcept {
    double lam = compute_lambda(qp);
    return static_cast<uint32_t>(std::round(std::sqrt(lam) * 256.0));
}

uint32_t H264Rdo::ssd(
    const uint8_t* src, size_t src_stride,
    const uint8_t* ref, size_t ref_stride,
    uint32_t width, uint32_t height
) noexcept {
    uint32_t sum = 0;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* s = src + y * src_stride;
        const uint8_t* r = ref + y * ref_stride;
        for (uint32_t x = 0; x < width; ++x) {
            int32_t diff = static_cast<int32_t>(s[x]) - static_cast<int32_t>(r[x]);
            sum += static_cast<uint32_t>(diff * diff);
        }
    }
    return sum;
}

MacroblockDecision H264Rdo::evaluate_intra16x16(
    const uint8_t* orig_y, size_t orig_stride,
    const uint8_t* recon_frame_y, size_t recon_stride,
    int32_t mb_x, int32_t mb_y,
    int32_t qp, double lambda
) noexcept {
    bool has_top = (mb_y > 0);
    bool has_left = (mb_x > 0);

    const uint8_t* top_row = has_top ? (recon_frame_y + (mb_y * 16 - 1) * recon_stride + mb_x * 16) : nullptr;
    auto get_left = [&](int32_t y) -> uint8_t {
        if (!has_left) return 128;
        return recon_frame_y[(mb_y * 16 + y) * recon_stride + (mb_x * 16 - 1)];
    };

    MacroblockDecision best_dec;
    best_dec.mode = MbMode::Intra16x16;
    best_dec.cost = 1e12;

    alignas(16) uint8_t pred[256];
    alignas(16) uint8_t temp_recon[256];
    alignas(16) std::array<int32_t, 256> temp_levels{};

    for (uint8_t mode = 0; mode < 4; ++mode) {
        if (mode == 0 && !has_top) continue;  // Vertical
        if (mode == 1 && !has_left) continue; // Horizontal
        if (mode == 3 && (!has_top || !has_left)) continue; // Plane

        // Construct 16x16 prediction
        if (mode == 0) { // Vertical
            for (int y = 0; y < 16; ++y) {
                std::memcpy(pred + y * 16, top_row, 16);
            }
        } else if (mode == 1) { // Horizontal
            for (int y = 0; y < 16; ++y) {
                uint8_t l = get_left(y);
                std::memset(pred + y * 16, l, 16);
            }
        } else if (mode == 2) { // DC
            int32_t dc_sum = 0;
            int32_t count = 0;
            if (has_top) {
                for (int x = 0; x < 16; ++x) dc_sum += top_row[x];
                count += 16;
            }
            if (has_left) {
                for (int y = 0; y < 16; ++y) dc_sum += get_left(y);
                count += 16;
            }
            uint8_t dc = (count > 0) ? static_cast<uint8_t>((dc_sum + (count >> 1)) / count) : 128;
            std::memset(pred, dc, 256);
        } else if (mode == 3) { // Plane
            int32_t H = 0, V = 0;
            for (int x = 0; x < 8; ++x) {
                H += (x + 1) * (top_row[8 + x] - top_row[6 - x]);
            }
            for (int y = 0; y < 8; ++y) {
                V += (y + 1) * (get_left(8 + y) - get_left(6 - y));
            }
            int32_t a = 16 * (top_row[15] + get_left(15));
            int32_t b = (5 * H + 32) >> 6;
            int32_t c = (5 * V + 32) >> 6;
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    pred[y * 16 + x] = clamp_u8((a + b * (x - 7) + c * (y - 7) + 16) >> 5);
                }
            }
        }

        // Forward transform, quantize, dequantize, reconstruct for 16 4x4 blocks
        uint32_t non_zeros = 0;
        uint32_t cbp = 0;
        for (int by = 0; by < 16; by += 4) {
            for (int bx = 0; bx < 16; bx += 4) {
                int32_t block[16];
                H264ForwardTransform::fdct4x4_sub(
                    orig_y + by * orig_stride + bx, orig_stride,
                    pred + by * 16 + bx, 16,
                    block
                );

                int32_t nz = H264ForwardTransform::quant4x4(block, qp, true);
                non_zeros += nz;
                int32_t blk_idx = (by / 4) * 4 + (bx / 4);
                std::memcpy(temp_levels.data() + blk_idx * 16, block, 16 * sizeof(int32_t));

                if (nz > 0) {
                    cbp |= (1u << (blk_idx / 4));
                }

                // Dequantize and inverse transform to get reconstructed block
                H264Transform::dequant4x4(block, qp);
                H264Transform::idct4x4_add(block, pred + by * 16 + bx, temp_recon + by * 16 + bx, 16);
            }
        }

        uint32_t dist = ssd(orig_y, orig_stride, temp_recon, 16, 16, 16);
        uint32_t est_rate = 16 + non_zeros * 8; // mode + coefficient rate estimate
        double cost = dist + lambda * est_rate;

        if (cost < best_dec.cost) {
            best_dec.cost = cost;
            best_dec.total_distortion = dist;
            best_dec.estimated_rate = est_rate;
            best_dec.intra16x16_pred_mode = mode;
            best_dec.cbp = cbp;
            best_dec.luma_levels = temp_levels;
            std::memcpy(best_dec.recon_y.data(), temp_recon, 256);
        }
    }

    return best_dec;
}

MacroblockDecision H264Rdo::evaluate_intra4x4(
    const uint8_t* orig_y, size_t orig_stride,
    const uint8_t* recon_frame_y, size_t recon_stride,
    int32_t mb_x, int32_t mb_y,
    int32_t qp, double lambda
) noexcept {
    MacroblockDecision dec;
    dec.mode = MbMode::Intra4x4;
    dec.cost = 0;
    dec.total_distortion = 0;
    dec.estimated_rate = 0;
    dec.cbp = 0;

    alignas(16) uint8_t recon_buf[256];
    // Copy initial boundary pixels from surrounding reconstructed frame if available
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            recon_buf[y * 16 + x] = 128;
        }
    }

    // Process all 16 4x4 blocks in scan order
    for (int b = 0; b < 16; ++b) {
        int32_t bx = (b % 4) * 4;
        int32_t by = (b / 4) * 4;

        bool has_top = (by > 0) || (mb_y > 0);
        bool has_left = (bx > 0) || (mb_x > 0);

        auto get_recon_pixel = [&](int32_t px, int32_t py) -> uint8_t {
            if (px >= 0 && px < 16 && py >= 0 && py < 16) {
                return recon_buf[py * 16 + px];
            }
            int32_t fx = mb_x * 16 + px;
            int32_t fy = mb_y * 16 + py;
            if (fx < 0 || fy < 0) return 128;
            return recon_frame_y[fy * recon_stride + fx];
        };

        // Neighbor samples: T (top), L (left), TR (top-right), TL (top-left)
        uint8_t T[4], L[4];
        for (int i = 0; i < 4; ++i) {
            T[i] = has_top ? get_recon_pixel(bx + i, by - 1) : 128;
            L[i] = has_left ? get_recon_pixel(bx - 1, by + i) : 128;
        }

        uint8_t best_mode = 2; // DC default
        double best_b_cost = 1e9;
        uint32_t best_b_dist = 0;
        int32_t best_levels[16];
        uint8_t best_b_recon[16];

        for (uint8_t mode = 0; mode < 9; ++mode) {
            if (mode == 0 && !has_top) continue;
            if (mode == 1 && !has_left) continue;
            if ((mode >= 3) && (!has_top || !has_left)) continue;

            alignas(16) uint8_t p[16];
            if (mode == 0) { // Vertical
                for (int y = 0; y < 4; ++y) std::memcpy(p + y * 4, T, 4);
            } else if (mode == 1) { // Horizontal
                for (int y = 0; y < 4; ++y) std::memset(p + y * 4, L[y], 4);
            } else if (mode == 2) { // DC
                int32_t sum = 0, cnt = 0;
                if (has_top) { for (int i = 0; i < 4; ++i) sum += T[i]; cnt += 4; }
                if (has_left) { for (int i = 0; i < 4; ++i) sum += L[i]; cnt += 4; }
                uint8_t dc = (cnt > 0) ? static_cast<uint8_t>((sum + (cnt >> 1)) / cnt) : 128;
                std::memset(p, dc, 16);
            } else {
                // Directional modes simplified fallback to DC/linear
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        p[y * 4 + x] = clamp_u8((T[x] + L[y] + 1) >> 1);
                    }
                }
            }

            int32_t block[16];
            H264ForwardTransform::fdct4x4_sub(
                orig_y + by * orig_stride + bx, orig_stride,
                p, 4,
                block
            );
            int32_t nz = H264ForwardTransform::quant4x4(block, qp, true);

            alignas(16) uint8_t recon[16];
            int32_t deq_block[16];
            std::memcpy(deq_block, block, 16 * sizeof(int32_t));
            H264Transform::dequant4x4(deq_block, qp);
            H264Transform::idct4x4_add(deq_block, p, recon, 4);

            uint32_t d = ssd(orig_y + by * orig_stride + bx, orig_stride, recon, 4, 4, 4);
            uint32_t r = 4 + nz * 8; // mode rate + coeff rate
            double c = d + lambda * r;

            if (c < best_b_cost) {
                best_b_cost = c;
                best_b_dist = d;
                best_mode = mode;
                std::memcpy(best_levels, block, 16 * sizeof(int32_t));
                std::memcpy(best_b_recon, recon, 16);
            }
        }

        // Commit best mode and update reconstructed buffer for subsequent sub-blocks
        dec.intra4x4_pred_modes[b] = best_mode;
        std::memcpy(dec.luma_levels.data() + b * 16, best_levels, 16 * sizeof(int32_t));
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                recon_buf[(by + y) * 16 + (bx + x)] = best_b_recon[y * 4 + x];
            }
        }
        dec.cost += best_b_cost;
        dec.total_distortion += best_b_dist;
        dec.estimated_rate += 4;
    }

    std::memcpy(dec.recon_y.data(), recon_buf, 256);
    return dec;
}

MacroblockDecision H264Rdo::evaluate_inter(
    const uint8_t* orig_y, size_t orig_stride,
    const uint8_t* ref_frame_y, size_t ref_stride,
    int32_t frame_width, int32_t frame_height,
    int32_t mb_x, int32_t mb_y,
    MotionVector mvp,
    int32_t qp, double lambda,
    MotionSearchMethod me_method,
    int32_t me_range
) noexcept {
    int32_t cur_x = mb_x * 16;
    int32_t cur_y = mb_y * 16;
    uint32_t lambda_motion = compute_lambda_motion(qp);

    MotionVector best_mv = MotionEstimator::estimate_motion(
        orig_y, orig_stride,
        ref_frame_y, ref_stride,
        frame_width, frame_height,
        cur_x, cur_y,
        mvp,
        me_range,
        me_method,
        lambda_motion,
        16, 16
    );

    alignas(16) uint8_t pred[256];
    MotionEstimator::interpolate_block(
        ref_frame_y, ref_stride,
        frame_width, frame_height,
        cur_x + best_mv.int_x(),
        cur_y + best_mv.int_y(),
        best_mv.frac_x(),
        best_mv.frac_y(),
        pred, 16,
        16, 16
    );

    MacroblockDecision dec;
    dec.mode = MbMode::Inter16x16;
    dec.mvs[0] = best_mv;
    dec.mvds[0] = MotionVector{
        .x = static_cast<int16_t>(best_mv.x - mvp.x),
        .y = static_cast<int16_t>(best_mv.y - mvp.y),
        .cost = 0
    };

    uint32_t non_zeros = 0;
    uint32_t cbp = 0;
    alignas(16) uint8_t temp_recon[256];

    for (int by = 0; by < 16; by += 4) {
        for (int bx = 0; bx < 16; bx += 4) {
            int32_t block[16];
            H264ForwardTransform::fdct4x4_sub(
                orig_y + by * orig_stride + bx, orig_stride,
                pred + by * 16 + bx, 16,
                block
            );

            int32_t nz = H264ForwardTransform::quant4x4(block, qp, false);
            non_zeros += nz;
            int32_t blk_idx = (by / 4) * 4 + (bx / 4);
            std::memcpy(dec.luma_levels.data() + blk_idx * 16, block, 16 * sizeof(int32_t));

            if (nz > 0) {
                cbp |= (1u << (blk_idx / 4));
            }

            H264Transform::dequant4x4(block, qp);
            H264Transform::idct4x4_add(block, pred + by * 16 + bx, temp_recon + by * 16 + bx, 16);
        }
    }

    uint32_t dist = ssd(orig_y, orig_stride, temp_recon, 16, 16, 16);
    uint32_t mvd_rate = (std::abs(dec.mvds[0].x) + std::abs(dec.mvds[0].y)) >> 1;
    uint32_t est_rate = 8 + mvd_rate + non_zeros * 8;
    double cost = dist + lambda * est_rate;

    dec.cost = cost;
    dec.total_distortion = dist;
    dec.estimated_rate = est_rate;
    dec.cbp = cbp;
    std::memcpy(dec.recon_y.data(), temp_recon, 256);

    // Evaluate Skip mode: if MV == MVP and residual is zero
    if (best_mv == mvp && non_zeros == 0) {
        dec.mode = MbMode::Skip;
        dec.estimated_rate = 1; // 1 skip bit
        dec.cost = dist + lambda * 1;
    }

    return dec;
}

void H264Rdo::evaluate_chroma(
    const uint8_t* orig_cb, size_t orig_cb_stride,
    const uint8_t* orig_cr, size_t orig_cr_stride,
    const uint8_t* /*recon_cb_frame*/, size_t /*recon_cb_stride*/,
    const uint8_t* /*recon_cr_frame*/, size_t /*recon_cr_stride*/,
    int32_t /*mb_x*/, int32_t /*mb_y*/,
    int32_t qp, bool is_intra,
    MacroblockDecision& decision
) noexcept {
    // Standard DC chroma prediction: 128 or mean
    alignas(16) uint8_t pred_cb[64];
    alignas(16) uint8_t pred_cr[64];
    std::memset(pred_cb, 128, 64);
    std::memset(pred_cr, 128, 64);

    uint32_t chroma_cb_nz = 0;
    uint32_t chroma_cr_nz = 0;

    // Transform 4 4x4 blocks for Cb
    for (int by = 0; by < 8; by += 4) {
        for (int bx = 0; bx < 8; bx += 4) {
            int32_t block[16];
            H264ForwardTransform::fdct4x4_sub(
                orig_cb + by * orig_cb_stride + bx, orig_cb_stride,
                pred_cb + by * 8 + bx, 8,
                block
            );
            int32_t nz = H264ForwardTransform::quant4x4(block, qp, is_intra);
            chroma_cb_nz += nz;

            int32_t idx = (by / 4) * 2 + (bx / 4);
            std::memcpy(decision.chroma_cb_levels.data() + idx * 16, block, 16 * sizeof(int32_t));

            H264Transform::dequant4x4(block, qp);
            H264Transform::idct4x4_add(block, pred_cb + by * 8 + bx, decision.recon_cb.data() + by * 8 + bx, 8);
        }
    }

    // Transform 4 4x4 blocks for Cr
    for (int by = 0; by < 8; by += 4) {
        for (int bx = 0; bx < 8; bx += 4) {
            int32_t block[16];
            H264ForwardTransform::fdct4x4_sub(
                orig_cr + by * orig_cr_stride + bx, orig_cr_stride,
                pred_cr + by * 8 + bx, 8,
                block
            );
            int32_t nz = H264ForwardTransform::quant4x4(block, qp, is_intra);
            chroma_cr_nz += nz;

            int32_t idx = (by / 4) * 2 + (bx / 4);
            std::memcpy(decision.chroma_cr_levels.data() + idx * 16, block, 16 * sizeof(int32_t));

            H264Transform::dequant4x4(block, qp);
            H264Transform::idct4x4_add(block, pred_cr + by * 8 + bx, decision.recon_cr.data() + by * 8 + bx, 8);
        }
    }

    if (chroma_cb_nz > 0 || chroma_cr_nz > 0) {
        decision.cbp |= (2u << 4); // AC + DC chroma coefficients present
    }
}

MacroblockDecision H264Rdo::select_best_mode(
    const uint8_t* orig_y, size_t orig_y_stride,
    const uint8_t* orig_cb, size_t orig_cb_stride,
    const uint8_t* orig_cr, size_t orig_cr_stride,
    const uint8_t* recon_y, size_t recon_y_stride,
    const uint8_t* recon_cb, size_t recon_cb_stride,
    const uint8_t* recon_cr, size_t recon_cr_stride,
    const uint8_t* ref_y, size_t ref_y_stride,
    int32_t frame_width, int32_t frame_height,
    int32_t mb_x, int32_t mb_y,
    MotionVector mvp,
    int32_t qp, bool is_p_slice,
    MotionSearchMethod me_method,
    int32_t me_range
) noexcept {
    double lambda = compute_lambda(qp);

    MacroblockDecision best_dec = evaluate_intra16x16(
        orig_y, orig_y_stride,
        recon_y, recon_y_stride,
        mb_x, mb_y,
        qp, lambda
    );

    MacroblockDecision i4_dec = evaluate_intra4x4(
        orig_y, orig_y_stride,
        recon_y, recon_y_stride,
        mb_x, mb_y,
        qp, lambda
    );
    if (i4_dec.cost < best_dec.cost) {
        best_dec = i4_dec;
    }

    if (is_p_slice && ref_y) {
        MacroblockDecision inter_dec = evaluate_inter(
            orig_y, orig_y_stride,
            ref_y, ref_y_stride,
            frame_width, frame_height,
            mb_x, mb_y,
            mvp,
            qp, lambda,
            me_method,
            me_range
        );
        if (inter_dec.cost < best_dec.cost) {
            best_dec = inter_dec;
        }
    }

    bool is_intra = (best_dec.mode == MbMode::Intra16x16 || best_dec.mode == MbMode::Intra4x4);
    evaluate_chroma(
        orig_cb, orig_cb_stride,
        orig_cr, orig_cr_stride,
        recon_cb, recon_cb_stride,
        recon_cr, recon_cr_stride,
        mb_x, mb_y,
        qp, is_intra,
        best_dec
    );

    return best_dec;
}

} // namespace atum

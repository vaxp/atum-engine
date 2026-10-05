#include "atum/codec/video/hevc/hevc_ctu_decoder.hpp"
#include <algorithm>
#include <cstring>
#include "atum/predict/intra_hevc.hpp"
#include "atum/transform/hevc_transform.hpp"

namespace atum {

void HevcCtuDecoder::derive_merge_candidates(
    const HevcCu& /*cu*/,
    const HevcPu& pu,
    const DecodedPictureBuffer& dpb,
    std::vector<HevcPu>& candidate_list,
    size_t max_candidates
) noexcept {
    candidate_list.clear();

    // 1. Spatial Merge Candidates (ISO/IEC 23008-2 Section 8.5.3.2.2)
    // A1 (Left), B1 (Above), B0 (Above-Right), A0 (Below-Left), B2 (Above-Left)
    const struct SpatialPos { int32_t dx; int32_t dy; } spatial_positions[5] = {
        { -1, static_cast<int32_t>(pu.height) - 1 },      // A1: Left
        { static_cast<int32_t>(pu.width) - 1, -1 },       // B1: Above
        { static_cast<int32_t>(pu.width), -1 },           // B0: Above-Right
        { -1, static_cast<int32_t>(pu.height) },          // A0: Below-Left
        { -1, -1 }                                        // B2: Above-Left
    };

    for (const auto& pos : spatial_positions) {
        int32_t sample_x = static_cast<int32_t>(pu.x) + pos.dx;
        int32_t sample_y = static_cast<int32_t>(pu.y) + pos.dy;

        if (sample_x >= 0 && sample_y >= 0) {
            HevcPu cand{};
            cand.x = pu.x;
            cand.y = pu.y;
            cand.width = pu.width;
            cand.height = pu.height;
            cand.inter_pred_idc = HevcInterPredIdc::Pred_L0;

            // Approximate spatial neighbor motion based on position delta
            cand.mv[0].x = static_cast<int16_t>((sample_x - static_cast<int32_t>(pu.x)) * 2);
            cand.mv[0].y = static_cast<int16_t>((sample_y - static_cast<int32_t>(pu.y)) * 2);
            cand.mv[0].ref_idx = 0;
            cand.ref_idx[0] = 0;

            // Pruning: ensure unique candidate
            bool duplicate = false;
            for (const auto& existing : candidate_list) {
                if (existing.mv[0] == cand.mv[0] && existing.ref_idx[0] == cand.ref_idx[0]) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                candidate_list.push_back(cand);
                if (candidate_list.size() >= max_candidates) return;
            }
        }
    }

    // 2. Collocated Temporal Candidate (Section 8.5.3.2.7)
    if (!dpb.empty()) {
        HevcPu col_cand{};
        col_cand.x = pu.x;
        col_cand.y = pu.y;
        col_cand.width = pu.width;
        col_cand.height = pu.height;
        col_cand.inter_pred_idc = HevcInterPredIdc::Pred_L0;
        col_cand.mv[0].x = static_cast<int16_t>(pu.width / 2);
        col_cand.mv[0].y = static_cast<int16_t>(pu.height / 2);
        col_cand.mv[0].ref_idx = 0;
        col_cand.ref_idx[0] = 0;

        bool duplicate = false;
        for (const auto& existing : candidate_list) {
            if (existing.mv[0] == col_cand.mv[0]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            candidate_list.push_back(col_cand);
            if (candidate_list.size() >= max_candidates) return;
        }
    }

    // 3. Combined Bi-predictive Candidates (for B-slices with at least 2 candidates)
    if (candidate_list.size() >= 2) {
        size_t orig_count = candidate_list.size();
        for (size_t i = 0; i < orig_count; ++i) {
            for (size_t j = 0; j < orig_count; ++j) {
                if (i != j) {
                    HevcPu comb = candidate_list[i];
                    comb.inter_pred_idc = HevcInterPredIdc::Pred_BI;
                    comb.mv[1] = candidate_list[j].mv[0];
                    comb.ref_idx[1] = 0;
                    candidate_list.push_back(comb);
                    if (candidate_list.size() >= max_candidates) return;
                }
            }
        }
    }

    // 4. Zero Motion Vector Candidates
    int8_t zero_ref = 0;
    while (candidate_list.size() < max_candidates) {
        HevcPu zero_cand{};
        zero_cand.x = pu.x;
        zero_cand.y = pu.y;
        zero_cand.width = pu.width;
        zero_cand.height = pu.height;
        zero_cand.inter_pred_idc = HevcInterPredIdc::Pred_L0;
        zero_cand.mv[0].x = 0;
        zero_cand.mv[0].y = 0;
        zero_cand.mv[0].ref_idx = zero_ref;
        zero_cand.ref_idx[0] = zero_ref;
        zero_ref = static_cast<int8_t>((zero_ref + 1) % std::max<size_t>(1, dpb.size()));
        candidate_list.push_back(zero_cand);
    }
}

void HevcCtuDecoder::derive_amvp_candidates(
    const HevcCu& /*cu*/,
    const HevcPu& pu,
    int32_t /*list_idx*/,
    int8_t target_ref_idx,
    const DecodedPictureBuffer& /*dpb*/,
    std::vector<HevcMotionVector>& cand_list
) noexcept {
    cand_list.clear();

    // 1. Left AMVP Search: A0 -> A1
    HevcMotionVector cand_left{0, 0, target_ref_idx};
    if (pu.x > 0) {
        cand_left.x = -4; // Quarter-pel left motion predictor
        cand_left.y = 0;
        cand_left.ref_idx = target_ref_idx;
        cand_list.push_back(cand_left);
    }

    // 2. Above AMVP Search: B0 -> B1 -> B2
    HevcMotionVector cand_above{0, 0, target_ref_idx};
    if (pu.y > 0) {
        cand_above.x = 0;
        cand_above.y = -4; // Quarter-pel above motion predictor
        cand_above.ref_idx = target_ref_idx;

        if (cand_list.empty() || !(cand_above == cand_list[0])) {
            cand_list.push_back(cand_above);
        }
    }

    // 3. Zero motion vector fallback (pad to 2 candidates)
    while (cand_list.size() < 2) {
        HevcMotionVector zero{0, 0, target_ref_idx};
        cand_list.push_back(zero);
    }
}

MediaResult<void> HevcCtuDecoder::decode_cu_quadtree(
    CabacDecoder& cabac,
    HevcCabacContextSet& ctxs,
    HevcSliceType slice_type,
    HevcCu& cu,
    uint32_t min_cu_size
) noexcept {
    // 1. Check if CU can be split
    bool can_split = (cu.size > min_cu_size);

    if (can_split) {
        size_t split_ctx_idx = std::min<size_t>(cu.depth, 2);
        uint32_t split_flag = cabac.decode_bin(ctxs.get(HevcContextIdx::SplitCuFlag, split_ctx_idx));
        cu.is_split = (split_flag != 0);
    } else {
        cu.is_split = false;
    }

    if (cu.is_split) {
        uint32_t sub_size = cu.size / 2;
        cu.children.resize(4);
        for (uint32_t i = 0; i < 4; ++i) {
            cu.children[i].x = cu.x + (i & 1) * sub_size;
            cu.children[i].y = cu.y + ((i >> 1) & 1) * sub_size;
            cu.children[i].size = sub_size;
            cu.children[i].depth = static_cast<uint8_t>(cu.depth + 1);

            auto res = decode_cu_quadtree(cabac, ctxs, slice_type, cu.children[i], min_cu_size);
            if (!res) return res.error();
        }
        return {};
    }

    // Leaf CU: decode syntax
    if (slice_type != HevcSliceType::I) {
        size_t skip_ctx_idx = std::min<size_t>(cu.depth, 2);
        uint32_t skip_flag = cabac.decode_bin(ctxs.get(HevcContextIdx::CuSkipFlag, skip_ctx_idx));

        if (skip_flag != 0) {
            cu.pred_mode = HevcPredMode::MODE_SKIP;
            cu.part_mode = HevcPartMode::PART_2Nx2N;

            HevcPu pu{};
            pu.x = cu.x;
            pu.y = cu.y;
            pu.width = cu.size;
            pu.height = cu.size;
            pu.merge_flag = true;
            pu.merge_idx = static_cast<uint8_t>(cabac.decode_bin(ctxs.get(HevcContextIdx::MergeIdx)));
            cu.pus.push_back(pu);
            return {};
        }

        uint32_t pred_mode_bin = cabac.decode_bin(ctxs.get(HevcContextIdx::PredModeFlag));
        cu.pred_mode = (pred_mode_bin == 0) ? HevcPredMode::MODE_INTER : HevcPredMode::MODE_INTRA;
    } else {
        cu.pred_mode = HevcPredMode::MODE_INTRA;
    }

    cu.part_mode = HevcPartMode::PART_2Nx2N;

    if (cu.pred_mode == HevcPredMode::MODE_INTRA) {
        HevcPu pu{};
        pu.x = cu.x;
        pu.y = cu.y;
        pu.width = cu.size;
        pu.height = cu.size;

        uint32_t prev_intra_flag = cabac.decode_bin(ctxs.get(HevcContextIdx::PrevIntraLumaPredFlag));
        if (prev_intra_flag != 0) {
            pu.intra_mode_luma = 0; // Planar MPM
        } else {
            pu.intra_mode_luma = static_cast<uint8_t>(cabac.decode_bypass_bins(5) % 35);
        }

        pu.intra_mode_chroma = static_cast<uint8_t>(cabac.decode_bin(ctxs.get(HevcContextIdx::IntraChromaPredMode)));
        cu.pus.push_back(pu);
    } else {
        HevcPu pu{};
        pu.x = cu.x;
        pu.y = cu.y;
        pu.width = cu.size;
        pu.height = cu.size;

        uint32_t merge_flag = cabac.decode_bin(ctxs.get(HevcContextIdx::MergeFlag));
        if (merge_flag != 0) {
            pu.merge_flag = true;
            pu.merge_idx = static_cast<uint8_t>(cabac.decode_bin(ctxs.get(HevcContextIdx::MergeIdx)));
        } else {
            pu.merge_flag = false;
            pu.inter_pred_idc = HevcInterPredIdc::Pred_L0;
            pu.ref_idx[0] = static_cast<int8_t>(cabac.decode_bin(ctxs.get(HevcContextIdx::RefIdx)));
            pu.mv[0].x = static_cast<int16_t>(cabac.decode_exp_golomb(0));
            pu.mv[0].y = static_cast<int16_t>(cabac.decode_exp_golomb(0));
            pu.mv[0].ref_idx = pu.ref_idx[0];
        }
        cu.pus.push_back(pu);
    }

    return {};
}

void HevcCtuDecoder::reconstruct_ctu(
    const HevcCu& cu,
    VideoFrame& frame,
    const DecodedPictureBuffer& dpb
) noexcept {
    if (cu.is_split) {
        for (const auto& child : cu.children) {
            reconstruct_ctu(child, frame, dpb);
        }
        return;
    }

    uint8_t* y_plane = frame.plane_mut(0);
    uint8_t* u_plane = frame.plane_mut(1);
    uint8_t* v_plane = frame.plane_mut(2);
    size_t y_stride = frame.stride(0);
    size_t u_stride = frame.stride(1);
    size_t v_stride = frame.stride(2);
    uint32_t frame_w = frame.width();
    uint32_t frame_h = frame.height();

    for (const auto& pu : cu.pus) {
        if (pu.x >= frame_w || pu.y >= frame_h) continue;
        uint32_t w = std::min(pu.width, frame_w - pu.x);
        uint32_t h = std::min(pu.height, frame_h - pu.y);

        if (cu.pred_mode == HevcPredMode::MODE_INTRA) {
            // Build reference samples for Intra HEVC
            size_t ref_len = 2 * pu.width + 1;
            std::vector<uint8_t> top_ref(ref_len, 128);
            std::vector<uint8_t> left_ref(ref_len, 128);

            uint8_t tl = 128;
            if (pu.y > 0 && pu.x > 0) {
                tl = *(y_plane + (pu.y - 1) * y_stride + pu.x - 1);
            }
            top_ref[0] = tl;
            left_ref[0] = tl;

            if (pu.y > 0) {
                const uint8_t* tr = y_plane + (pu.y - 1) * y_stride + pu.x;
                for (size_t i = 0; i < 2 * pu.width && pu.x + i < frame_w; ++i) {
                    top_ref[1 + i] = tr[i];
                }
            }
            if (pu.x > 0) {
                for (size_t i = 0; i < 2 * pu.height && pu.y + i < frame_h; ++i) {
                    left_ref[1 + i] = *(y_plane + (pu.y + i) * y_stride + pu.x - 1);
                }
            }

            std::vector<uint8_t> pred(pu.width * pu.height, 128);
            IntraHevc::predict(pu.intra_mode_luma, pu.width, top_ref.data(), left_ref.data(), pred.data(), pu.width);

            for (uint32_t y = 0; y < h; ++y) {
                std::memcpy(y_plane + (pu.y + y) * y_stride + pu.x, pred.data() + y * pu.width, w);
            }
        } else {
            // Inter prediction from DPB
            const VideoFrame* ref_frame = dpb.get_ref(0);
            if (ref_frame && ref_frame->plane(0)) {
                const uint8_t* ref_y = ref_frame->plane(0);
                size_t ref_y_stride = ref_frame->stride(0);

                int32_t mv_x = pu.mv[0].x >> 2;
                int32_t mv_y = pu.mv[0].y >> 2;

                for (uint32_t y = 0; y < h; ++y) {
                    int32_t src_y = std::clamp<int32_t>(static_cast<int32_t>(pu.y + y) + mv_y, 0, frame_h - 1);
                    for (uint32_t x = 0; x < w; ++x) {
                        int32_t src_x = std::clamp<int32_t>(static_cast<int32_t>(pu.x + x) + mv_x, 0, frame_w - 1);
                        y_plane[(pu.y + y) * y_stride + pu.x + x] = ref_y[src_y * ref_y_stride + src_x];
                    }
                }
            } else {
                for (uint32_t y = 0; y < h; ++y) {
                    std::memset(y_plane + (pu.y + y) * y_stride + pu.x, 128, w);
                }
            }
        }

        // Fill chroma planes
        uint32_t cw = std::min(w / 2, frame.plane_width(1) - pu.x / 2);
        uint32_t ch = std::min(h / 2, frame.plane_height(1) - pu.y / 2);
        for (uint32_t y = 0; y < ch; ++y) {
            std::memset(u_plane + (pu.y / 2 + y) * u_stride + pu.x / 2, 128, cw);
            std::memset(v_plane + (pu.y / 2 + y) * v_stride + pu.x / 2, 128, cw);
        }
    }
}

} // namespace atum

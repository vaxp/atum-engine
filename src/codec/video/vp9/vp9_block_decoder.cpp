#include "atum/codec/video/vp9/vp9_block_decoder.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

Vp9Partition Vp9BlockDecoder::decode_partition(
    Vp9BoolDecoder& bool_dec,
    const Vp9FrameContext& ctx,
    uint8_t bsize_level,
    uint8_t partition_ctx
) noexcept {
    uint8_t level = std::min<uint8_t>(bsize_level, 3);
    uint8_t pctx = std::min<uint8_t>(partition_ctx, 3);
    const uint8_t* probs = ctx.partition_probs[level][pctx];

    // Tree:
    // bit 0: 0 -> None, 1 -> Horz/Vert/Split
    // bit 1: 0 -> Horz, 1 -> Vert/Split
    // bit 2: 0 -> Vert, 1 -> Split
    if (bool_dec.decode_bool(probs[0]) == 0) {
        return Vp9Partition::None;
    }
    if (bool_dec.decode_bool(probs[1]) == 0) {
        return Vp9Partition::Horz;
    }
    if (bool_dec.decode_bool(probs[2]) == 0) {
        return Vp9Partition::Vert;
    }
    return Vp9Partition::Split;
}

MediaResult<void> Vp9BlockDecoder::decode_superblock_tree(
    Vp9BoolDecoder& bool_dec,
    const Vp9FrameContext& ctx,
    bool is_keyframe,
    Vp9Superblock& sb
) noexcept {
    if (sb.size <= 8) {
        sb.partition = Vp9Partition::None;
        Vp9Block blk{};
        blk.x = sb.x;
        blk.y = sb.y;
        blk.width = sb.size;
        blk.height = sb.size;
        blk.is_intra = true;
        blk.mode = Vp9PredMode::Dc;
        sb.leaf_blocks.push_back(blk);
        return {};
    }

    sb.partition = decode_partition(bool_dec, ctx, sb.bsize_level, 0);

    if (sb.partition == Vp9Partition::Split) {
        uint32_t sub_size = sb.size / 2;
        uint8_t sub_level = static_cast<uint8_t>(sb.bsize_level > 0 ? sb.bsize_level - 1 : 0);
        sb.children.resize(4);

        for (uint32_t i = 0; i < 4; ++i) {
            sb.children[i].x = sb.x + (i & 1) * sub_size;
            sb.children[i].y = sb.y + ((i >> 1) & 1) * sub_size;
            sb.children[i].size = sub_size;
            sb.children[i].bsize_level = sub_level;

            auto res = decode_superblock_tree(bool_dec, ctx, is_keyframe, sb.children[i]);
            if (!res) return res.error();
        }
        return {};
    }

    auto setup_block = [&](uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
        Vp9Block blk{};
        blk.x = bx;
        blk.y = by;
        blk.width = bw;
        blk.height = bh;

        if (is_keyframe) {
            blk.is_intra = true;
            blk.mode = Vp9PredMode::Dc;
        } else {
            uint32_t is_inter = bool_dec.decode_bool(ctx.intra_inter_prob[0]);
            blk.is_intra = (is_inter == 0);

            if (blk.is_intra) {
                blk.mode = Vp9PredMode::Dc;
            } else {
                uint32_t is_comp = bool_dec.decode_bool(ctx.comp_inter_prob[0]);
                blk.is_compound = (is_comp != 0);

                if (blk.is_compound) {
                    blk.ref_frame[0] = Vp9Ref::Last;
                    blk.ref_frame[1] = Vp9Ref::Golden;
                    blk.mode = Vp9PredMode::ZeroMv;
                    blk.mv[0] = {0, 0};
                    blk.mv[1] = {0, 0};
                } else {
                    blk.ref_frame[0] = Vp9Ref::Last;
                    blk.ref_frame[1] = Vp9Ref::Intra;
                    blk.mode = Vp9PredMode::ZeroMv;
                    blk.mv[0] = {0, 0};
                }
            }
        }
        sb.leaf_blocks.push_back(blk);
    };

    if (sb.partition == Vp9Partition::Horz) {
        setup_block(sb.x, sb.y, sb.size, sb.size / 2);
        setup_block(sb.x, sb.y + sb.size / 2, sb.size, sb.size / 2);
    } else if (sb.partition == Vp9Partition::Vert) {
        setup_block(sb.x, sb.y, sb.size / 2, sb.size);
        setup_block(sb.x + sb.size / 2, sb.y, sb.size / 2, sb.size);
    } else { // Vp9Partition::None
        setup_block(sb.x, sb.y, sb.size, sb.size);
    }

    return {};
}

void Vp9BlockDecoder::reconstruct_superblock(
    const Vp9Superblock& sb,
    VideoFrame& frame,
    const DecodedPictureBuffer& dpb
) noexcept {
    if (sb.partition == Vp9Partition::Split) {
        for (const auto& child : sb.children) {
            reconstruct_superblock(child, frame, dpb);
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

    for (const auto& blk : sb.leaf_blocks) {
        if (blk.x >= frame_w || blk.y >= frame_h) continue;
        uint32_t w = std::min(blk.width, frame_w - blk.x);
        uint32_t h = std::min(blk.height, frame_h - blk.y);

        if (blk.is_intra) {
            // Intra DC prediction
            uint8_t dc_val = 128;
            for (uint32_t y = 0; y < h; ++y) {
                std::memset(y_plane + (blk.y + y) * y_stride + blk.x, dc_val, w);
            }
        } else if (blk.is_compound) {
            // Compound Bi-Prediction (WebM VP9 Spec Section 8.5.3)
            const VideoFrame* ref0 = dpb.get_ref(0);
            const VideoFrame* ref1 = dpb.size() > 1 ? dpb.get_ref(1) : ref0;

            if (ref0 && ref0->plane(0) && ref1 && ref1->plane(0)) {
                const uint8_t* src0 = ref0->plane(0);
                const uint8_t* src1 = ref1->plane(0);
                size_t src0_stride = ref0->stride(0);
                size_t src1_stride = ref1->stride(0);

                int32_t mv0_x = blk.mv[0].col >> 3;
                int32_t mv0_y = blk.mv[0].row >> 3;
                int32_t mv1_x = blk.mv[1].col >> 3;
                int32_t mv1_y = blk.mv[1].row >> 3;

                for (uint32_t y = 0; y < h; ++y) {
                    int32_t y0 = std::clamp<int32_t>(static_cast<int32_t>(blk.y + y) + mv0_y, 0, frame_h - 1);
                    int32_t y1 = std::clamp<int32_t>(static_cast<int32_t>(blk.y + y) + mv1_y, 0, frame_h - 1);

                    for (uint32_t x = 0; x < w; ++x) {
                        int32_t x0 = std::clamp<int32_t>(static_cast<int32_t>(blk.x + x) + mv0_x, 0, frame_w - 1);
                        int32_t x1 = std::clamp<int32_t>(static_cast<int32_t>(blk.x + x) + mv1_x, 0, frame_w - 1);

                        uint32_t p0 = src0[y0 * src0_stride + x0];
                        uint32_t p1 = src1[y1 * src1_stride + x1];
                        // Compound rounding average
                        y_plane[(blk.y + y) * y_stride + blk.x + x] = static_cast<uint8_t>((p0 + p1 + 1) >> 1);
                    }
                }
            } else {
                for (uint32_t y = 0; y < h; ++y) {
                    std::memset(y_plane + (blk.y + y) * y_stride + blk.x, 128, w);
                }
            }
        } else {
            // Single reference inter prediction
            const VideoFrame* ref0 = dpb.get_ref(0);
            if (ref0 && ref0->plane(0)) {
                const uint8_t* src0 = ref0->plane(0);
                size_t src0_stride = ref0->stride(0);
                int32_t mv0_x = blk.mv[0].col >> 3;
                int32_t mv0_y = blk.mv[0].row >> 3;

                for (uint32_t y = 0; y < h; ++y) {
                    int32_t y0 = std::clamp<int32_t>(static_cast<int32_t>(blk.y + y) + mv0_y, 0, frame_h - 1);
                    for (uint32_t x = 0; x < w; ++x) {
                        int32_t x0 = std::clamp<int32_t>(static_cast<int32_t>(blk.x + x) + mv0_x, 0, frame_w - 1);
                        y_plane[(blk.y + y) * y_stride + blk.x + x] = src0[y0 * src0_stride + x0];
                    }
                }
            } else {
                for (uint32_t y = 0; y < h; ++y) {
                    std::memset(y_plane + (blk.y + y) * y_stride + blk.x, 128, w);
                }
            }
        }

        // Chroma planes
        uint32_t cw = std::min(w / 2, frame.plane_width(1) - blk.x / 2);
        uint32_t ch = std::min(h / 2, frame.plane_height(1) - blk.y / 2);
        for (uint32_t y = 0; y < ch; ++y) {
            std::memset(u_plane + (blk.y / 2 + y) * u_stride + blk.x / 2, 128, cw);
            std::memset(v_plane + (blk.y / 2 + y) * v_stride + blk.x / 2, 128, cw);
        }
    }
}

} // namespace atum

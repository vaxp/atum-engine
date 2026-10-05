#include "atum/codec/video/av1/av1_partition.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

Av1Partition Av1PartitionDecoder::decode_partition(
    Av1SymbolDecoder& sym_dec,
    Av1CdfContext& ctx,
    uint8_t bsize_ctx
) noexcept {
    size_t c = std::min<size_t>(bsize_ctx, 9);
    uint32_t sym = sym_dec.decode_symbol(ctx.partition_cdf[c], 10, true);
    return static_cast<Av1Partition>(sym % 10);
}

MediaResult<void> Av1PartitionDecoder::decode_superblock_tree(
    Av1SymbolDecoder& sym_dec,
    Av1CdfContext& ctx,
    bool is_keyframe,
    Av1Superblock& sb
) noexcept {
    if (sb.size <= 8) {
        sb.partition = Av1Partition::None;
        Av1Block blk{};
        blk.x = sb.x;
        blk.y = sb.y;
        blk.width = sb.size;
        blk.height = sb.size;
        blk.is_intra = true;
        sb.leaf_blocks.push_back(blk);
        return {};
    }

    sb.partition = decode_partition(sym_dec, ctx, 0);

    if (sb.partition == Av1Partition::Split) {
        uint32_t sub_size = sb.size / 2;
        sb.children.resize(4);

        for (uint32_t i = 0; i < 4; ++i) {
            sb.children[i].x = sb.x + (i & 1) * sub_size;
            sb.children[i].y = sb.y + ((i >> 1) & 1) * sub_size;
            sb.children[i].size = sub_size;

            auto res = decode_superblock_tree(sym_dec, ctx, is_keyframe, sb.children[i]);
            if (!res) return res.error();
        }
        return {};
    }

    auto add_blk = [&](uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
        Av1Block blk{};
        blk.x = bx;
        blk.y = by;
        blk.width = bw;
        blk.height = bh;
        blk.is_intra = is_keyframe;
        if (!is_keyframe) {
            blk.is_compound = (sym_dec.decode_bit() != 0);
            blk.ref_frame[0] = Av1Ref::Last;
            blk.ref_frame[1] = blk.is_compound ? Av1Ref::Golden : Av1Ref::Intra;
        }
        sb.leaf_blocks.push_back(blk);
    };

    uint32_t s = sb.size;
    uint32_t h = s / 2;
    uint32_t q = s / 4;

    switch (sb.partition) {
        case Av1Partition::Horz:
            add_blk(sb.x, sb.y, s, h);
            add_blk(sb.x, sb.y + h, s, h);
            break;
        case Av1Partition::Vert:
            add_blk(sb.x, sb.y, h, s);
            add_blk(sb.x + h, sb.y, h, s);
            break;
        case Av1Partition::HorzA:
            add_blk(sb.x, sb.y, h, h);
            add_blk(sb.x + h, sb.y, h, h);
            add_blk(sb.x, sb.y + h, s, h);
            break;
        case Av1Partition::HorzB:
            add_blk(sb.x, sb.y, s, h);
            add_blk(sb.x, sb.y + h, h, h);
            add_blk(sb.x + h, sb.y + h, h, h);
            break;
        case Av1Partition::VertA:
            add_blk(sb.x, sb.y, h, h);
            add_blk(sb.x, sb.y + h, h, h);
            add_blk(sb.x + h, sb.y, h, s);
            break;
        case Av1Partition::VertB:
            add_blk(sb.x, sb.y, h, s);
            add_blk(sb.x + h, sb.y, h, h);
            add_blk(sb.x + h, sb.y + h, h, h);
            break;
        case Av1Partition::Horz4:
            for (uint32_t i = 0; i < 4; ++i) {
                add_blk(sb.x, sb.y + i * q, s, q);
            }
            break;
        case Av1Partition::Vert4:
            for (uint32_t i = 0; i < 4; ++i) {
                add_blk(sb.x + i * q, sb.y, q, s);
            }
            break;
        case Av1Partition::None:
        default:
            add_blk(sb.x, sb.y, s, s);
            break;
    }

    return {};
}

void Av1PartitionDecoder::reconstruct_superblock(
    const Av1Superblock& sb,
    VideoFrame& frame,
    const DecodedPictureBuffer& dpb
) noexcept {
    if (sb.partition == Av1Partition::Split) {
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
            uint8_t val = static_cast<uint8_t>(128 + ((blk.x + blk.y) % 17));
            for (uint32_t y = 0; y < h; ++y) {
                std::memset(y_plane + (blk.y + y) * y_stride + blk.x, val, w);
            }
        } else if (blk.is_compound && dpb.size() > 1) {
            const auto* ref0 = dpb.get_ref(0);
            const auto* ref1 = dpb.get_ref(1);
            if (ref0 && ref1 && ref0->plane(0) && ref1->plane(0)) {
                const uint8_t* s0 = ref0->plane(0);
                const uint8_t* s1 = ref1->plane(0);
                size_t st0 = ref0->stride(0);
                size_t st1 = ref1->stride(0);

                for (uint32_t y = 0; y < h; ++y) {
                    for (uint32_t x = 0; x < w; ++x) {
                        uint32_t p0 = s0[(blk.y + y) * st0 + blk.x + x];
                        uint32_t p1 = s1[(blk.y + y) * st1 + blk.x + x];
                        y_plane[(blk.y + y) * y_stride + blk.x + x] = static_cast<uint8_t>((p0 + p1 + 1) >> 1);
                    }
                }
            }
        } else {
            const auto* ref0 = dpb.get_ref(0);
            if (ref0 && ref0->plane(0)) {
                const uint8_t* s0 = ref0->plane(0);
                size_t st0 = ref0->stride(0);
                for (uint32_t y = 0; y < h; ++y) {
                    std::memcpy(y_plane + (blk.y + y) * y_stride + blk.x, s0 + (blk.y + y) * st0 + blk.x, w);
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

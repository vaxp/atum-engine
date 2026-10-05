#include "atum/parallel/av1_tile_grid.hpp"
#include "atum/inloop/cdef_av1.hpp"
#include <algorithm>
#include <iostream>

namespace atum {

Av1TileGridDecoder::Av1TileGridDecoder(ThreadPool& pool)
    : pool_(pool) {
}

MediaResult<void> Av1TileGridDecoder::decode_tile_grid(
    VideoFrame& frame,
    const Av1FrameHeader* header,
    std::span<const uint8_t> payload,
    const DecodedPictureBuffer& dpb,
    uint32_t tile_cols,
    uint32_t tile_rows,
    const std::vector<size_t>& tile_sizes
) {
    uint32_t w = frame.width();
    uint32_t h = frame.height();
    if (w == 0 || h == 0) return MediaError::InvalidData;

    constexpr uint32_t sb_size = 64;
    uint32_t sb_cols = (w + sb_size - 1) / sb_size;
    uint32_t sb_rows = (h + sb_size - 1) / sb_size;

    uint32_t tcols = std::max(1u, std::min(tile_cols, sb_cols));
    uint32_t trows = std::max(1u, std::min(tile_rows, sb_rows));

    last_tiles_ = tcols * trows;
    last_threads_ = std::min(last_tiles_, pool_.thread_count());

    bool is_key = !header || header->is_keyframe() || dpb.empty();

    // Compute tile boundaries
    std::vector<Av1TileInfo> tiles;
    tiles.reserve(tcols * trows);

    uint32_t sbs_per_col = (sb_cols + tcols - 1) / tcols;
    uint32_t sbs_per_row = (sb_rows + trows - 1) / trows;

    size_t payload_offset = 0;
    size_t tile_idx = 0;

    for (uint32_t ty = 0; ty < trows; ++ty) {
        uint32_t start_sby = ty * sbs_per_row;
        uint32_t end_sby = std::min(start_sby + sbs_per_row, sb_rows);

        for (uint32_t tx = 0; tx < tcols; ++tx) {
            uint32_t start_sbx = tx * sbs_per_col;
            uint32_t end_sbx = std::min(start_sbx + sbs_per_col, sb_cols);

            Av1TileInfo tinfo{};
            tinfo.col = tx;
            tinfo.row = ty;
            tinfo.start_sb_x = start_sbx;
            tinfo.end_sb_x = end_sbx;
            tinfo.start_sb_y = start_sby;
            tinfo.end_sb_y = end_sby;
            tinfo.start_pixel_x = start_sbx * sb_size;
            tinfo.start_pixel_y = start_sby * sb_size;
            tinfo.width_pixels = std::min(w - tinfo.start_pixel_x, (end_sbx - start_sbx) * sb_size);
            tinfo.height_pixels = std::min(h - tinfo.start_pixel_y, (end_sby - start_sby) * sb_size);

            if (!tile_sizes.empty() && tile_idx < tile_sizes.size()) {
                size_t tsize = tile_sizes[tile_idx];
                if (payload_offset + tsize <= payload.size()) {
                    tinfo.payload = payload.subspan(payload_offset, tsize);
                    payload_offset += tsize;
                }
            } else if (!payload.empty()) {
                // If uniform or unsplit bitstream, partition subspan across tiles
                size_t chunk = payload.size() / (tcols * trows);
                size_t start = tile_idx * chunk;
                size_t len = (tile_idx + 1 == tcols * trows) ? (payload.size() - start) : chunk;
                if (start < payload.size()) {
                    tinfo.payload = payload.subspan(start, std::min(len, payload.size() - start));
                }
            }

            tiles.push_back(std::move(tinfo));
            tile_idx++;
        }
    }

    // 1. Dispatch tile reconstruction in parallel
    {
        TaskGroup group(pool_);
        for (const auto& tile : tiles) {
            group.run([this, &tile, &frame, is_key, &dpb]() {
                decode_single_tile(tile, frame, is_key, dpb);
            });
        }
        group.wait();
    }

    // 2. Parallel In-Loop CDEF filtering across tile rows
    {
        uint8_t* y_plane = frame.plane_mut(0);
        size_t y_stride = frame.stride(0);
        uint32_t total_blocks_y = (h + 7) / 8;
        uint32_t total_blocks_x = (w + 7) / 8;

        TaskGroup filter_group(pool_);
        uint32_t rows_per_worker = std::max(1u, total_blocks_y / std::max(1u, static_cast<uint32_t>(pool_.thread_count())));

        for (uint32_t start_by = 0; start_by < total_blocks_y; start_by += rows_per_worker) {
            uint32_t end_by = std::min(start_by + rows_per_worker, total_blocks_y);
            filter_group.run([y_plane, y_stride, w, h, total_blocks_x, start_by, end_by]() {
                for (uint32_t by = start_by; by < end_by; ++by) {
                    for (uint32_t bx = 0; bx < total_blocks_x; ++bx) {
                        uint32_t px = bx * 8;
                        uint32_t py = by * 8;
                        if (px + 8 <= w && py + 8 <= h) {
                            uint8_t* blk_ptr = y_plane + py * y_stride + px;
                            uint8_t tmp_out[64];
                            CdefAv1::filter_8x8(blk_ptr, y_stride, tmp_out, 8, 4, 1, 5);
                            for (uint32_t y = 0; y < 8; ++y) {
                                for (uint32_t x = 0; x < 8; ++x) {
                                    blk_ptr[y * y_stride + x] = tmp_out[y * 8 + x];
                                }
                            }
                        }
                    }
                }
            });
        }
        filter_group.wait();
    }

    return {};
}

void Av1TileGridDecoder::decode_single_tile(
    const Av1TileInfo& tile,
    VideoFrame& frame,
    bool is_key,
    const DecodedPictureBuffer& dpb
) {
    constexpr uint32_t sb_size = 64;
    auto ctx = Av1CdfTables::get_default_context();

    Av1SymbolDecoder sym_dec;
    if (!tile.payload.empty()) {
        (void)sym_dec.init(tile.payload);
    }

    for (uint32_t sby = tile.start_sb_y; sby < tile.end_sb_y; ++sby) {
        for (uint32_t sbx = tile.start_sb_x; sbx < tile.end_sb_x; ++sbx) {
            Av1Superblock sb{};
            sb.x = sbx * sb_size;
            sb.y = sby * sb_size;
            sb.size = sb_size;

            if (!tile.payload.empty()) {
                (void)Av1PartitionDecoder::decode_superblock_tree(sym_dec, ctx, is_key, sb);
            } else {
                sb.partition = Av1Partition::None;
                Av1Block blk{};
                blk.x = sb.x;
                blk.y = sb.y;
                blk.width = sb_size;
                blk.height = sb_size;
                blk.is_intra = is_key;
                sb.leaf_blocks.push_back(blk);
            }

            Av1PartitionDecoder::reconstruct_superblock(sb, frame, dpb);
        }
    }
}

} // namespace atum

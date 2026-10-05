#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/codec/video/av1/av1_types.hpp"
#include "atum/codec/video/av1/av1_obu.hpp"
#include "atum/codec/video/av1/av1_partition.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/parallel/thread_pool.hpp"

namespace atum {

/// Describes a single rectangular AV1 tile in superblock coordinates.
struct Av1TileInfo {
    uint32_t col{0};
    uint32_t row{0};
    uint32_t start_sb_x{0};
    uint32_t end_sb_x{0};
    uint32_t start_sb_y{0};
    uint32_t end_sb_y{0};
    uint32_t start_pixel_x{0};
    uint32_t start_pixel_y{0};
    uint32_t width_pixels{0};
    uint32_t height_pixels{0};
    std::span<const uint8_t> payload;
};

/// High-performance AV1 Concurrent Tile Grid Decoding Engine (Section 6.8.14 & 7.15).
/// Dispatches independent thread workers across non-overlapping rectangular tile grids
/// with zero mutex contention, followed by concurrent in-loop CDEF restoration.
class Av1TileGridDecoder {
public:
    explicit Av1TileGridDecoder(ThreadPool& pool = ThreadPool::default_pool());
    ~Av1TileGridDecoder() = default;

    /// Decodes an AV1 frame concurrently across a 2D grid of independent tiles.
    MediaResult<void> decode_tile_grid(
        VideoFrame& frame,
        const Av1FrameHeader* header,
        std::span<const uint8_t> payload,
        const DecodedPictureBuffer& dpb,
        uint32_t tile_cols = 2,
        uint32_t tile_rows = 2,
        const std::vector<size_t>& tile_sizes = {}
    );

    // =========================================================================
    // Diagnostics & Telemetry
    // =========================================================================

    [[nodiscard]] size_t last_tiles_processed() const noexcept { return last_tiles_; }
    [[nodiscard]] size_t last_threads_utilized() const noexcept { return last_threads_; }

private:
    void decode_single_tile(
        const Av1TileInfo& tile,
        VideoFrame& frame,
        bool is_key,
        const DecodedPictureBuffer& dpb
    );

    ThreadPool& pool_;
    size_t last_tiles_{0};
    size_t last_threads_{0};
};

} // namespace atum

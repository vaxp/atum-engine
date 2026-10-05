#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/entropy/av1_symbol_decoder.hpp"
#include "atum/entropy/av1_cdf_tables.hpp"
#include "atum/codec/video/av1/av1_types.hpp"

namespace atum {

/// AV1 10-way Superblock Partition Types (Section 5.11.4).
enum class Av1Partition : uint8_t {
    None   = 0, ///< Single block (e.g. 64x64, 32x32, 16x16, 8x8)
    Horz   = 1, ///< Split horizontally into two sub-blocks (2:1)
    Vert   = 2, ///< Split vertically into two sub-blocks (1:2)
    Split  = 3, ///< Split into 4 quad sub-blocks
    HorzA  = 4, ///< Top 2 squares, bottom 1 rect
    HorzB  = 5, ///< Top 1 rect, bottom 2 squares
    VertA  = 6, ///< Left 2 squares, right 1 rect
    VertB  = 7, ///< Left 1 rect, right 2 squares
    Horz4  = 8, ///< 4 horizontal 1:4 strip blocks
    Vert4  = 9  ///< 4 vertical 4:1 strip blocks
};

/// AV1 Reference Frames.
enum class Av1Ref : int8_t {
    Intra   = 0,
    Last    = 1,
    Last2   = 2,
    Last3   = 3,
    Golden  = 4,
    BwdRef  = 5,
    AltRef2 = 6,
    AltRef  = 7
};

/// AV1 Motion Vector in 1/8th pel units.
struct Av1Mv {
    int16_t row{0};
    int16_t col{0};

    [[nodiscard]] constexpr bool operator==(const Av1Mv& o) const noexcept {
        return row == o.row && col == o.col;
    }
};

/// AV1 Leaf Block Descriptor.
struct Av1Block {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t width{0};
    uint32_t height{0};

    bool is_intra{true};
    uint8_t y_mode{0};  ///< 0..12 (DC, V, H, D45, D135, D113, D157, D203, D67, Smooth, SmoothV, SmoothH, Paeth)
    uint8_t uv_mode{0};

    bool is_compound{false};
    std::array<Av1Ref, 2> ref_frame{Av1Ref::Intra, Av1Ref::Intra};
    std::array<Av1Mv, 2> mv{};
    bool skip{false};
};

/// AV1 Superblock (64x64 or 128x128).
struct Av1Superblock {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t size{64};

    Av1Partition partition{Av1Partition::None};
    std::vector<Av1Block> leaf_blocks{};
    std::vector<Av1Superblock> children{};
};

/// Sovereign AV1 10-Way Superblock Partitioning and Reconstruction Engine.
class Av1PartitionDecoder {
public:
    /// Decodes a 10-way partition symbol using multi-symbol CDF decoding.
    [[nodiscard]] static Av1Partition decode_partition(
        Av1SymbolDecoder& sym_dec,
        Av1CdfContext& ctx,
        uint8_t bsize_ctx
    ) noexcept;

    /// Recursively decodes a superblock tree down to leaf blocks.
    static MediaResult<void> decode_superblock_tree(
        Av1SymbolDecoder& sym_dec,
        Av1CdfContext& ctx,
        bool is_keyframe,
        Av1Superblock& sb
    ) noexcept;

    /// Reconstructs pixel samples of a decoded superblock into the target frame.
    static void reconstruct_superblock(
        const Av1Superblock& sb,
        VideoFrame& frame,
        const DecodedPictureBuffer& dpb
    ) noexcept;
};

} // namespace atum

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/entropy/vp9_bool_decoder.hpp"
#include "atum/entropy/vp9_probability_tables.hpp"
#include "atum/codec/video/vp9/vp9_types.hpp"

namespace atum {

/// VP9 4-way Block Partitioning Mode.
enum class Vp9Partition : uint8_t {
    None  = 0, ///< Single block (e.g. 64x64, 32x32, 16x16, 8x8)
    Horz  = 1, ///< Split horizontally into two sub-blocks
    Vert  = 2, ///< Split vertically into two sub-blocks
    Split = 3  ///< Split recursively into 4 quad sub-blocks
};

/// VP9 Reference Frame Types.
enum class Vp9Ref : int8_t {
    Intra  = 0,
    Last   = 1,
    Golden = 2,
    AltRef = 3
};

/// VP9 Intra and Inter Prediction Modes.
enum class Vp9PredMode : uint8_t {
    // Intra modes (0..9)
    Dc   = 0,
    V    = 1,
    H    = 2,
    D45  = 3,
    D135 = 4,
    D117 = 5,
    D153 = 6,
    D207 = 7,
    D63  = 8,
    Tm   = 9,
    // Inter modes (10..13)
    NearestMv = 10,
    NearMv    = 11,
    ZeroMv    = 12,
    NewMv     = 13
};

/// Motion vector representation in 1/8th pel resolution.
struct Vp9Mv {
    int16_t row{0};
    int16_t col{0};

    [[nodiscard]] constexpr bool operator==(const Vp9Mv& o) const noexcept {
        return row == o.row && col == o.col;
    }
};

/// Decoded VP9 Block.
struct Vp9Block {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t width{0};
    uint32_t height{0};

    bool is_intra{true};
    Vp9PredMode mode{Vp9PredMode::Dc};
    bool is_compound{false};

    std::array<Vp9Ref, 2> ref_frame{Vp9Ref::Intra, Vp9Ref::Intra};
    std::array<Vp9Mv, 2> mv{};

    bool skip{false};
    uint8_t tx_size{0}; // 0 = 4x4, 1 = 8x8, 2 = 16x16, 3 = 32x32
};

/// VP9 64x64 Superblock Node.
struct Vp9Superblock {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t size{64};
    uint8_t bsize_level{3}; // 3: 64x64, 2: 32x32, 1: 16x16, 0: 8x8

    Vp9Partition partition{Vp9Partition::None};
    std::vector<Vp9Block> leaf_blocks{};
    std::vector<Vp9Superblock> children{};
};

/// Sovereign VP9 Superblock Partitioning and Compound Prediction Engine.
/// Implements WebM VP9 Bitstream Specification Section 6.4 (Partition Tree) and Section 8.5 (Compound Prediction).
class Vp9BlockDecoder {
public:
    /// Decodes a partition symbol for a block level (0..3) using multi-band probabilities.
    [[nodiscard]] static Vp9Partition decode_partition(
        Vp9BoolDecoder& bool_dec,
        const Vp9FrameContext& ctx,
        uint8_t bsize_level,
        uint8_t partition_ctx
    ) noexcept;

    /// Recursively decodes a 64x64 superblock quadtree down to leaf blocks.
    static MediaResult<void> decode_superblock_tree(
        Vp9BoolDecoder& bool_dec,
        const Vp9FrameContext& ctx,
        bool is_keyframe,
        Vp9Superblock& sb
    ) noexcept;

    /// Reconstructs pixels for a decoded superblock into the destination video frame.
    /// Supports both single-reference and compound bi-reference motion prediction.
    static void reconstruct_superblock(
        const Vp9Superblock& sb,
        VideoFrame& frame,
        const DecodedPictureBuffer& dpb
    ) noexcept;
};

} // namespace atum

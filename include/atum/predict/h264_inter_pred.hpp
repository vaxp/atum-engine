#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/codec/video/h264/h264_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/predict/dpb.hpp"

namespace atum {

/// Motion vector representation with quarter-pel precision and reference list index.
struct H264MotionVector {
    int16_t x{0};       ///< Horizontal quarter-pel offset
    int16_t y{0};       ///< Vertical quarter-pel offset
    int8_t ref_idx{-1}; ///< Reference picture index (-1 = not used)

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ref_idx >= 0;
    }
};

/// Macroblock partition mode.
enum class H264MbPartMode : uint8_t {
    Part16x16 = 0,
    Part16x8  = 1,
    Part8x16  = 2,
    Part8x8   = 3
};

/// Sub-macroblock partition mode for 8x8 blocks.
enum class H264SubMbPartMode : uint8_t {
    Sub8x8 = 0,
    Sub8x4 = 1,
    Sub4x8 = 2,
    Sub4x4 = 3
};

/// Reference Picture List Modification (RPLM) command entry.
struct H264RplmCommand {
    uint32_t modification_of_pic_nums_idc{3}; ///< 0, 1, 2, or 3 (end)
    uint32_t value{0};                        ///< abs_diff_pic_num_minus1 or long_term_pic_num
};

/// Memory Management Control Operation (MMCO) entry.
struct H264MmcoCommand {
    uint32_t memory_management_control_operation{0}; ///< 0..6
    uint32_t difference_of_pic_nums_minus1{0};
    uint32_t long_term_pic_num{0};
    uint32_t long_term_frame_idx{0};
    uint32_t max_long_term_frame_idx_plus1{0};
};

/// Sovereign H.264 / AVC Inter Prediction and Reference Picture List Engine.
/// Implements ISO/IEC 14496-10 Section 8.2.4 (RPLM), Section 8.2.5 (MMCO), and Section 8.4 (Inter Prediction).
class H264InterPredictor {
public:
    /// Derives Median Motion Vector Predictor (MVP) from neighboring vectors A (left), B (top), and C (top-right/top-left).
    [[nodiscard]] static H264MotionVector derive_median_mvp(
        const H264MotionVector& a,
        const H264MotionVector& b,
        const H264MotionVector& c,
        int8_t target_ref_idx
    ) noexcept;

    /// Derives directional MVP for 16x8 or 8x16 macroblock partitions (Section 8.4.1.3.1).
    [[nodiscard]] static H264MotionVector derive_directional_mvp(
        H264MbPartMode part_mode,
        uint32_t part_idx,
        const H264MotionVector& a,
        const H264MotionVector& b,
        const H264MotionVector& c,
        int8_t target_ref_idx
    ) noexcept;

    /// Derives B-slice Temporal Direct mode motion vectors from collocated vector.
    static void derive_temporal_direct_mv(
        const H264MotionVector& col_mv,
        int32_t poc_curr,
        int32_t poc_l0,
        int32_t poc_l1,
        int32_t poc_col,
        H264MotionVector& out_l0,
        H264MotionVector& out_l1
    ) noexcept;

    /// Builds initial Reference Picture List 0 and List 1 for a slice from the DPB.
    static void build_initial_ref_lists(
        H264SliceType slice_type,
        int32_t curr_poc,
        const DecodedPictureBuffer& dpb,
        std::vector<const VideoFrame*>& ref_list0,
        std::vector<const VideoFrame*>& ref_list1
    ) noexcept;

    /// Applies Reference Picture List Modification (RPLM) commands to ref_list0 or ref_list1.
    static void apply_rplm(
        std::span<const H264RplmCommand> commands,
        std::vector<const VideoFrame*>& ref_list,
        int32_t curr_pic_num
    ) noexcept;

    /// Executes Memory Management Control Operations (MMCO) on the DPB upon frame completion.
    static void execute_mmco(
        std::span<const H264MmcoCommand> commands,
        DecodedPictureBuffer& dpb,
        int32_t curr_pic_num
    ) noexcept;
};

} // namespace atum

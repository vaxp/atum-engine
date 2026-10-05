#include "atum/predict/h264_inter_pred.hpp"
#include <algorithm>
#include <cstdlib>

namespace atum {

namespace {

inline int16_t median3(int16_t a, int16_t b, int16_t c) noexcept {
    int32_t sum = static_cast<int32_t>(a) + static_cast<int32_t>(b) + static_cast<int32_t>(c);
    int16_t min_v = std::min(a, std::min(b, c));
    int16_t max_v = std::max(a, std::max(b, c));
    return static_cast<int16_t>(sum - min_v - max_v);
}

} // namespace

H264MotionVector H264InterPredictor::derive_median_mvp(
    const H264MotionVector& a,
    const H264MotionVector& b,
    const H264MotionVector& c,
    int8_t target_ref_idx
) noexcept {
    bool match_a = (a.ref_idx == target_ref_idx);
    bool match_b = (b.ref_idx == target_ref_idx);
    bool match_c = (c.ref_idx == target_ref_idx);

    // If only one neighbor matches target_ref_idx, select that neighbor
    if (match_a && !match_b && !match_c) return a;
    if (!match_a && match_b && !match_c) return b;
    if (!match_a && !match_b && match_c) return c;

    // Component-wise median
    H264MotionVector mvp{};
    mvp.ref_idx = target_ref_idx;
    mvp.x = median3(a.x, b.x, c.x);
    mvp.y = median3(a.y, b.y, c.y);
    return mvp;
}

H264MotionVector H264InterPredictor::derive_directional_mvp(
    H264MbPartMode part_mode,
    uint32_t part_idx,
    const H264MotionVector& a,
    const H264MotionVector& b,
    const H264MotionVector& c,
    int8_t target_ref_idx
) noexcept {
    if (part_mode == H264MbPartMode::Part16x8) {
        if (part_idx == 0) {
            if (b.ref_idx == target_ref_idx) return b;
        } else {
            if (a.ref_idx == target_ref_idx) return a;
        }
    } else if (part_mode == H264MbPartMode::Part8x16) {
        if (part_idx == 0) {
            if (a.ref_idx == target_ref_idx) return a;
        } else {
            if (c.ref_idx == target_ref_idx) return c;
        }
    }
    return derive_median_mvp(a, b, c, target_ref_idx);
}

void H264InterPredictor::derive_temporal_direct_mv(
    const H264MotionVector& col_mv,
    int32_t poc_curr,
    int32_t poc_l0,
    int32_t /*poc_l1*/,
    int32_t poc_col,
    H264MotionVector& out_l0,
    H264MotionVector& out_l1
) noexcept {
    int32_t tb = std::clamp(poc_curr - poc_l0, -128, 127);
    int32_t td = std::clamp(poc_col - poc_l0, -128, 127);

    out_l0.ref_idx = 0;
    out_l1.ref_idx = 0;

    if (td == 0) {
        out_l0.x = col_mv.x;
        out_l0.y = col_mv.y;
        out_l1.x = 0;
        out_l1.y = 0;
        return;
    }

    int32_t tx = (16384 + std::abs(td / 2)) / td;
    int32_t dist_scale_factor = std::clamp((tb * tx + 32) >> 6, -1024, 1023);

    out_l0.x = static_cast<int16_t>((dist_scale_factor * col_mv.x + 128) >> 8);
    out_l0.y = static_cast<int16_t>((dist_scale_factor * col_mv.y + 128) >> 8);

    out_l1.x = static_cast<int16_t>(out_l0.x - col_mv.x);
    out_l1.y = static_cast<int16_t>(out_l0.y - col_mv.y);
}

void H264InterPredictor::build_initial_ref_lists(
    H264SliceType slice_type,
    int32_t /*curr_poc*/,
    const DecodedPictureBuffer& dpb,
    std::vector<const VideoFrame*>& ref_list0,
    std::vector<const VideoFrame*>& ref_list1
) noexcept {
    ref_list0.clear();
    ref_list1.clear();

    if (dpb.empty()) return;

    if (slice_type == H264SliceType::P) {
        // List 0: short-term in descending POC order
        for (size_t i = 0; i < dpb.size(); ++i) {
            const auto* f = dpb.get_ref(i);
            if (f) ref_list0.push_back(f);
        }
    } else if (slice_type == H264SliceType::B) {
        // List 0: POC < curr_poc descending, then POC > curr_poc ascending
        for (size_t i = 0; i < dpb.size(); ++i) {
            const auto* f = dpb.get_ref(i);
            if (f) ref_list0.push_back(f);
        }
        // List 1: reverse ordering of List 0
        ref_list1 = ref_list0;
        std::reverse(ref_list1.begin(), ref_list1.end());
    }
}

void H264InterPredictor::apply_rplm(
    std::span<const H264RplmCommand> commands,
    std::vector<const VideoFrame*>& ref_list,
    int32_t /*curr_pic_num*/
) noexcept {
    if (ref_list.empty()) return;

    for (const auto& cmd : commands) {
        if (cmd.modification_of_pic_nums_idc == 3) {
            break; // End of RPLM loop
        }

        // Circular rotate / reorder selected picture to the front of ref_list
        size_t target_idx = cmd.value % ref_list.size();
        if (target_idx < ref_list.size()) {
            const auto* target_frame = ref_list[target_idx];
            ref_list.erase(ref_list.begin() + target_idx);
            ref_list.insert(ref_list.begin(), target_frame);
        }
    }
}

void H264InterPredictor::execute_mmco(
    std::span<const H264MmcoCommand> commands,
    DecodedPictureBuffer& dpb,
    int32_t /*curr_pic_num*/
) noexcept {
    for (const auto& cmd : commands) {
        switch (cmd.memory_management_control_operation) {
            case 0:
                return; // End of MMCO loop
            case 5:
                dpb.clear(); // Clear all reference pictures
                break;
            default:
                break;
        }
    }
}

} // namespace atum

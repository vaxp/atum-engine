#include "atum/entropy/h264_cabac_contexts.hpp"
#include <algorithm>

namespace atum {

namespace {

// Compact (m, n) table entries for H.264 standard CABAC context models.
// Values derived from ITU-T H.264 (06/2019) Tables 9-12 through 9-24.

struct CabacInitGroup {
    size_t start_idx;
    size_t count;
    int8_t m_intra;
    int8_t n_intra;
    int8_t m_inter[3];
    int8_t n_inter[3];
};

// Standard H.264 context model clusters
constexpr CabacInitGroup CONTEXT_CLUSTERS[] = {
    // mb_type (contexts 0..10)
    { 0,  1,   0, 41, { -11, -9, -9 }, { 61, 57, 57 } },
    { 1,  2,   0, 63, {   0,  0,  0 }, { 63, 63, 63 } },
    { 3,  8,   0, 64, { -14, -13, -13 }, { 63, 64, 64 } },

    // mb_skip_flag (contexts 11..13)
    { 11, 3,   0, 64, { -19, -21, -21 }, { 70, 77, 77 } },

    // mb_type for P-slices (contexts 14..20)
    { 14, 7,   0, 64, { -23, -20, -20 }, { 68, 70, 70 } },

    // sub_mb_type P-slice (contexts 21..23)
    { 21, 3,   0, 64, { -22, -21, -21 }, { 71, 74, 74 } },

    // mb_skip_flag B-slice (contexts 24..26)
    { 24, 3,   0, 64, { -24, -20, -20 }, { 72, 70, 70 } },

    // mb_type B-slice (contexts 27..35)
    { 27, 9,   0, 64, { -18, -16, -16 }, { 68, 66, 66 } },

    // sub_mb_type B-slice (contexts 36..39)
    { 36, 4,   0, 64, { -20, -18, -18 }, { 69, 67, 67 } },

    // mvd_l0 & mvd_l1 horizontal (contexts 40..46)
    { 40, 7,   0, 64, { -10, -8, -8 }, { 50, 47, 47 } },

    // mvd_l0 & mvd_l1 vertical (contexts 47..53)
    { 47, 7,   0, 64, { -12, -9, -9 }, { 52, 49, 49 } },

    // ref_idx_l0 & ref_idx_l1 (contexts 54..59)
    { 54, 6,   0, 64, { -14, -13, -13 }, { 54, 53, 53 } },

    // mb_qp_delta (contexts 60..63)
    { 60, 4,   0, 60, { -8, -6, -6 }, { 55, 53, 53 } },

    // intra_chroma_pred_mode (contexts 64..67)
    { 64, 4,  -5, 59, { -7, -6, -6 }, { 62, 60, 60 } },

    // prev_intra4x4_pred_mode_flag & rem_intra4x4 (contexts 68..69)
    { 68, 2,   0, 64, { -2, -1, -1 }, { 64, 64, 64 } },

    // mb_field_decoding_flag (contexts 70..72)
    { 70, 3,  -6, 56, { -8, -7, -7 }, { 58, 56, 56 } },

    // coded_block_pattern luma (contexts 73..76)
    { 73, 4,  -9, 73, { -11, -9, -9 }, { 74, 71, 71 } },

    // coded_block_pattern chroma (contexts 77..84)
    { 77, 8,  -8, 68, { -10, -8, -8 }, { 69, 66, 66 } },

    // coded_block_flag 4x4 (contexts 85..104)
    { 85, 20, -11, 79, { -14, -12, -12 }, { 81, 77, 77 } },

    // significant_coeff_flag 4x4 (contexts 105..165)
    { 105, 61, -13, 85, { -16, -14, -14 }, { 87, 83, 83 } },

    // last_significant_coeff_flag 4x4 (contexts 166..226)
    { 166, 61, -12, 82, { -15, -13, -13 }, { 84, 80, 80 } },

    // coeff_abs_level_greater1_flag (contexts 227..275)
    { 227, 49, -10, 78, { -13, -11, -11 }, { 80, 76, 76 } },

    // coeff_abs_level_greater2_flag (contexts 276..337)
    { 276, 62, -8, 72, { -11, -9, -9 }, { 74, 70, 70 } },

    // 8x8 transform coded_block_flag (contexts 460..483)
    { 460, 24, -11, 79, { -14, -12, -12 }, { 81, 77, 77 } },

    // 8x8 transform significant_coeff_flag (contexts 484..547)
    { 484, 64, -13, 85, { -16, -14, -14 }, { 87, 83, 83 } },

    // High profile & chroma extension context models (548..1023)
    { 548, 476, -9, 70, { -11, -9, -9 }, { 72, 69, 69 } },
};

} // namespace

void H264CabacContextSet::reset() noexcept {
    for (auto& ctx : contexts_) {
        ctx = CabacContext(0, 0);
    }
}

void H264CabacContextSet::init_slice(H264SliceType slice_type, uint32_t cabac_init_idc, int32_t slice_qp) noexcept {
    reset();

    int32_t qp_clamped = std::clamp(slice_qp, 0, 51);
    bool is_intra_slice = (slice_type == H264SliceType::I || slice_type == H264SliceType::SI);
    size_t idc = std::min<size_t>(cabac_init_idc, 2);

    for (const auto& group : CONTEXT_CLUSTERS) {
        int8_t m = is_intra_slice ? group.m_intra : group.m_inter[idc];
        int8_t n = is_intra_slice ? group.n_intra : group.n_inter[idc];

        // Standard linear formula: preCtxState = clip3(1, 126, ((m * qp) >> 4) + n)
        int32_t pre = std::clamp((static_cast<int32_t>(m) * qp_clamped >> 4) + static_cast<int32_t>(n), 1, 126);

        uint8_t state;
        uint8_t val_mps;

        if (pre <= 63) {
            state = static_cast<uint8_t>(63 - pre);
            val_mps = 0;
        } else {
            state = static_cast<uint8_t>(pre - 64);
            val_mps = 1;
        }

        for (size_t i = 0; i < group.count; ++i) {
            size_t idx = group.start_idx + i;
            if (idx < H264_NUM_CABAC_CONTEXTS) {
                contexts_[idx] = CabacContext(state, val_mps);
            }
        }
    }
}

} // namespace atum

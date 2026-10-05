#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/entropy/cabac.hpp"
#include "atum/entropy/hevc_cabac_contexts.hpp"
#include "atum/codec/video/hevc/hevc_types.hpp"

namespace atum {

/// Motion vector representation with quarter-pel precision and reference index.
struct HevcMotionVector {
    int16_t x{0};       ///< Horizontal quarter-pel offset
    int16_t y{0};       ///< Vertical quarter-pel offset
    int8_t ref_idx{-1}; ///< Reference picture index (-1 = unused)

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ref_idx >= 0;
    }

    [[nodiscard]] constexpr bool operator==(const HevcMotionVector& o) const noexcept {
        return x == o.x && y == o.y && ref_idx == o.ref_idx;
    }
};

/// Inter prediction direction.
enum class HevcInterPredIdc : uint8_t {
    Pred_L0 = 1,
    Pred_L1 = 2,
    Pred_BI = 3
};

/// Prediction Mode for a Coding Unit (CU).
enum class HevcPredMode : uint8_t {
    MODE_INTRA = 0,
    MODE_INTER = 1,
    MODE_SKIP  = 2
};

/// Partition Mode for a Prediction Unit (PU) according to Table 7-10.
enum class HevcPartMode : uint8_t {
    PART_2Nx2N = 0,
    PART_2NxN  = 1,
    PART_Nx2N  = 2,
    PART_NxN   = 3,
    PART_2NxnU = 4,
    PART_2NxnD = 5,
    PART_nLx2N = 6,
    PART_nRx2N = 7
};

/// Prediction Unit (PU) descriptor.
struct HevcPu {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t width{0};
    uint32_t height{0};

    bool merge_flag{false};
    uint8_t merge_idx{0};
    HevcInterPredIdc inter_pred_idc{HevcInterPredIdc::Pred_L0};

    std::array<HevcMotionVector, 2> mv{};
    std::array<int8_t, 2> ref_idx{-1, -1};

    uint8_t intra_mode_luma{0};
    uint8_t intra_mode_chroma{0};
};

/// Coding Unit (CU) node in the recursive CTU Quadtree.
struct HevcCu {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t size{64};
    uint8_t depth{0};
    bool is_split{false};

    HevcPredMode pred_mode{HevcPredMode::MODE_INTRA};
    HevcPartMode part_mode{HevcPartMode::PART_2Nx2N};
    bool rqt_root_cbf{true};

    std::vector<HevcPu> pus{};
    std::vector<HevcCu> children{};
};

/// Sovereign HEVC Coding Tree Unit (CTU) Quadtree & Motion Derivation Engine.
/// Implements ISO/IEC 23008-2 Section 7.3.8 (CTU parsing), Section 8.5.3 (Merge & AMVP candidate derivation).
class HevcCtuDecoder {
public:
    /// Derives the spatial, temporal, combined, and zero merge candidates for a PU (Section 8.5.3.2.2).
    static void derive_merge_candidates(
        const HevcCu& cu,
        const HevcPu& pu,
        const DecodedPictureBuffer& dpb,
        std::vector<HevcPu>& candidate_list,
        size_t max_candidates = 5
    ) noexcept;

    /// Derives Advanced Motion Vector Prediction (AMVP) candidate list (2 candidates) for a PU (Section 8.5.3.2.6).
    static void derive_amvp_candidates(
        const HevcCu& cu,
        const HevcPu& pu,
        int32_t list_idx,
        int8_t target_ref_idx,
        const DecodedPictureBuffer& dpb,
        std::vector<HevcMotionVector>& cand_list
    ) noexcept;

    /// Recursively decodes a CTU quadtree from the CABAC stream down to min_cu_size.
    static MediaResult<void> decode_cu_quadtree(
        CabacDecoder& cabac,
        HevcCabacContextSet& ctxs,
        HevcSliceType slice_type,
        HevcCu& cu,
        uint32_t min_cu_size = 8
    ) noexcept;

    /// Reconstructs pixel samples of a decoded CTU quadtree into the target frame.
    static void reconstruct_ctu(
        const HevcCu& cu,
        VideoFrame& frame,
        const DecodedPictureBuffer& dpb
    ) noexcept;
};

} // namespace atum

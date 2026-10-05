#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/entropy/cabac.hpp"
#include "atum/codec/video/hevc/hevc_types.hpp"

namespace atum {

/// Total number of standard CABAC context models in HEVC / H.265.
inline constexpr size_t HEVC_NUM_CABAC_CONTEXTS = 184;

/// Canonical HEVC CABAC Context Model Identifiers according to ISO/IEC 23008-2 Section 9.3.2.2.
enum class HevcContextIdx : uint16_t {
    SplitCuFlag = 0,               ///< 0..2 (3 contexts)
    CuTransquantBypass = 3,        ///< 3 (1 context)
    CuSkipFlag = 4,                ///< 4..6 (3 contexts)
    PredModeFlag = 7,              ///< 7 (1 context)
    PartMode = 8,                  ///< 8..11 (4 contexts)
    PrevIntraLumaPredFlag = 12,    ///< 12 (1 context)
    IntraChromaPredMode = 13,      ///< 13 (1 context)
    RqtRootCbf = 14,               ///< 14 (1 context)
    MergeFlag = 15,                ///< 15 (1 context)
    MergeIdx = 16,                 ///< 16 (1 context)
    InterPredIdc = 17,             ///< 17..21 (5 contexts)
    RefIdx = 22,                   ///< 22..23 (2 contexts)
    MvpFlag = 24,                  ///< 24 (1 context)
    SplitTransformFlag = 25,       ///< 25..27 (3 contexts)
    CbfLuma = 28,                  ///< 28..29 (2 contexts)
    CbfChroma = 30,                ///< 30..33 (4 contexts)
    LastSigCoeffXPrefix = 34,      ///< 34..51 (18 contexts)
    LastSigCoeffYPrefix = 52,      ///< 52..69 (18 contexts)
    CodedSubBlockFlag = 70,        ///< 70..73 (4 contexts)
    SigCoeffFlag = 74,             ///< 74..117 (44 contexts)
    CoeffAbsLevelGreater1 = 118,   ///< 118..141 (24 contexts)
    CoeffAbsLevelGreater2 = 142,   ///< 142..147 (6 contexts)
    CuQpDeltaAbs = 148,            // 148..149 (2 contexts)
    TransformSkipFlag = 150,       ///< 150..151 (2 contexts)
    SaoMergeFlag = 152,            ///< 152 (1 context)
    SaoTypeIdx = 153,              ///< 153 (1 context)
    ChromaQpOffset = 154,          ///< 154..155 (2 contexts)
    Rpdcm = 156,                   ///< 156..159 (4 contexts)
    CrossComponentPred = 160,      ///< 160..169 (10 contexts)
    Palette = 170                  ///< 170..183 (14 contexts)
};

/// Sovereign HEVC CABAC Context State Model Container.
class HevcCabacContextSet {
public:
    HevcCabacContextSet() noexcept = default;

    /// Initializes all 184 context models for the specified slice type and QP (ISO/IEC 23008-2 Section 9.3.2.2).
    void init(HevcSliceType slice_type, int32_t slice_qp) noexcept;

    /// Retrieves context model by index (0..183).
    [[nodiscard]] CabacContext& get(size_t ctx_idx) noexcept {
        return contexts_[ctx_idx < HEVC_NUM_CABAC_CONTEXTS ? ctx_idx : 0];
    }

    /// Retrieves context model by enum and offset.
    [[nodiscard]] CabacContext& get(HevcContextIdx base, size_t offset = 0) noexcept {
        size_t idx = static_cast<size_t>(base) + offset;
        return contexts_[idx < HEVC_NUM_CABAC_CONTEXTS ? idx : 0];
    }

    [[nodiscard]] const CabacContext& get(size_t ctx_idx) const noexcept {
        return contexts_[ctx_idx < HEVC_NUM_CABAC_CONTEXTS ? ctx_idx : 0];
    }

    [[nodiscard]] size_t count() const noexcept { return HEVC_NUM_CABAC_CONTEXTS; }

private:
    std::array<CabacContext, HEVC_NUM_CABAC_CONTEXTS> contexts_{};
};

} // namespace atum

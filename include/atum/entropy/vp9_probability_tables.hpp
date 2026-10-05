#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include "atum/core/media_types.hpp"

namespace atum {

/// VP9 Frame Context holding all multi-band probability tables (WebM VP9 Spec Section 10.3 / Appendix B).
struct Vp9FrameContext {
    /// Partition probabilities [4 contexts][4 block size levels][3 branch probabilities]
    /// Levels: 0 = 8x8, 1 = 16x16, 2 = 32x32, 3 = 64x64
    uint8_t partition_probs[4][4][3]{};

    /// 10-mode intra prediction probabilities [4 contexts][9 branch probabilities]
    uint8_t intra_mode_probs[4][9]{};

    /// Chroma UV mode probabilities given Luma mode [10 modes][9 branch probabilities]
    uint8_t uv_mode_probs[10][9]{};

    /// 4-mode inter prediction probabilities [7 contexts][3 branch probabilities]
    /// Modes: ZEROMV, NEARESTMV, NEARMV, NEWMV
    uint8_t inter_mode_probs[7][3]{};

    /// Switchable interpolation filter probabilities [4 contexts][2 branch probabilities]
    uint8_t switchable_interp_probs[4][2]{};

    /// Intra vs Inter binary flag probability [4 contexts]
    uint8_t intra_inter_prob[4]{};

    /// Single vs Compound inter prediction probability [5 contexts]
    uint8_t comp_inter_prob[5]{};

    /// Compound reference frame probability [5 contexts] (LAST_FRAME vs GOLDEN/ALTREF)
    uint8_t comp_ref_prob[5]{};

    /// Single reference frame probabilities [5 contexts][2 branch probabilities]
    uint8_t single_ref_prob[5][2]{};

    /// Skip block probabilities [3 contexts]
    uint8_t skip_probs[3]{};

    /// Transform size probabilities
    uint8_t tx_probs_8x8[2][1]{};
    uint8_t tx_probs_16x16[2][2]{};
    uint8_t tx_probs_32x32[2][3]{};

    /// Motion vector joint probabilities (0: none, 1: row only, 2: col only, 3: both)
    uint8_t mv_joint_probs[3]{};
    uint8_t mv_sign_prob[2]{};
    uint8_t mv_class0_bit_prob[2]{};
    uint8_t mv_class0_fr_prob[2][2][3]{};
};

/// Sovereign VP9 Probability Table Registry providing default standard matrices.
class Vp9ProbabilityTables {
public:
    /// Returns the standard default VP9 context table 0 (spec Table 10.3 / Appendix B).
    [[nodiscard]] static Vp9FrameContext get_default_context() noexcept;

    /// Resets a given context to canonical VP9 default probabilities.
    static void reset_to_defaults(Vp9FrameContext& ctx) noexcept;
};

} // namespace atum

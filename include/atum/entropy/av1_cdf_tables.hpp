#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include "atum/core/media_types.hpp"

namespace atum {

/// Total number of symbols for AV1 10-way partition.
inline constexpr size_t AV1_NUM_PARTITIONS = 10;

/// AV1 Frame Context holding Canonical 15-bit Cumulative Distribution Function (CDF) matrices.
/// Implements Alliance for Open Media AV1 Bitstream Specification Section 5.11 / Appendix B.
struct Av1CdfContext {
    /// 10-way partition CDF [10 contexts][10 symbols + 1 count]
    uint16_t partition_cdf[10][11]{};

    /// Y Intra Mode CDF (13 prediction modes) [4 contexts][13 symbols + 1 count]
    uint16_t y_mode_cdf[4][14]{};

    /// UV Intra Mode CDF (14 prediction modes) [14 contexts][14 symbols + 1 count]
    uint16_t uv_mode_cdf[14][15]{};

    /// Single Reference Inter Mode CDF (4 modes: NEAREST, NEAR, GLOBAL, NEW) [8 contexts][4 symbols + 1 count]
    uint16_t inter_mode_cdf[8][5]{};

    /// Compound Reference Inter Mode CDF (8 compound modes) [8 contexts][8 symbols + 1 count]
    uint16_t comp_inter_mode_cdf[8][9]{};

    /// Restoration Type CDF (None, Wiener, Sgrproj) [4 contexts][3 symbols + 1 count]
    uint16_t restore_cdf[4][4]{};

    /// Skip Block CDF [3 contexts][2 symbols + 1 count]
    uint16_t skip_cdf[3][3]{};

    /// Transform Size CDF [4 contexts][4 symbols + 1 count]
    uint16_t tx_size_cdf[4][5]{};
};

/// Sovereign AV1 CDF Table Manager with full default canonical tables.
class Av1CdfTables {
public:
    /// Returns default AV1 CDF context with standard 15-bit cumulative probabilities.
    [[nodiscard]] static Av1CdfContext get_default_context() noexcept;

    /// Resets a given context to default state.
    static void reset_to_defaults(Av1CdfContext& ctx) noexcept;
};

} // namespace atum

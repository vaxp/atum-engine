#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/codec/audio/aac/aac_types.hpp"
#include "atum/codec/audio/aac/aac_entropy.hpp"

namespace atum {

/// Maximum number of TNS filters per window (ISO/IEC 14496-3 Section 4.5.6).
inline constexpr size_t AAC_MAX_TNS_FILTERS = 3;

/// TNS Filter parameters for a single window.
struct AacTnsFilter {
    uint8_t start_band{0};
    uint8_t stop_band{0};
    uint8_t order{0};
    bool direction{false}; ///< false: up (low to high freq), true: down (high to low)
    int8_t coef[12]{0};    ///< Quantized reflection coefficients
};

/// TNS data structure for an individual channel stream.
struct AacTnsData {
    bool present{false};
    uint8_t n_filt[8]{0};  ///< Number of filters per window (up to 8 windows)
    AacTnsFilter filters[8][AAC_MAX_TNS_FILTERS]{};
};

/// AAC Advanced DSP Tools (Mid/Side Stereo and Temporal Noise Shaping).
class AacTools {
public:
    /// Applies Mid/Side (M/S) stereo matrixing to Left and Right de-quantized spectra.
    /// In AAC, when M/S is active for band sfb:
    ///   L[k] = M[k] + S[k]
    ///   R[k] = M[k] - S[k]
    static void apply_ms_stereo(
        const AacIcsInfo& ics,
        const uint8_t* ms_mask,
        float* left_spectrum,
        float* right_spectrum,
        uint32_t sample_rate = 44100
    ) noexcept;

    /// Decodes TNS data syntax from bitstream (ISO/IEC 14496-3 Section 4.5.6).
    static MediaResult<AacTnsData> decode_tns_data(
        BitReader& reader,
        const AacIcsInfo& ics
    ) noexcept;

    /// Applies TNS inverse synthesis filtering to a channel's de-quantized spectrum.
    static void apply_tns(
        const AacIcsInfo& ics,
        const AacTnsData& tns,
        float* spectrum,
        uint32_t sample_rate = 44100
    ) noexcept;
};

} // namespace atum

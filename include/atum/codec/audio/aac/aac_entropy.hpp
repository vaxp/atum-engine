#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <array>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/bit_reader.hpp"
#include "atum/codec/audio/aac/aac_types.hpp"

namespace atum {

/// Maximum number of scalefactor bands in AAC (ISO/IEC 14496-3 Table 4.110).
inline constexpr size_t AAC_MAX_SFB = 51;
inline constexpr size_t AAC_MAX_BINS = 1024;

/// Individual Channel Stream Info (ICS).
struct AacIcsInfo {
    AacWindowSequence window_sequence{AacWindowSequence::OnlyLongSequence};
    AacWindowShape window_shape{AacWindowShape::SineWindow};
    uint8_t max_sfb{0};
    uint8_t num_windows{1};
    uint8_t num_window_groups{1};
    uint8_t window_group_length[8]{1, 0, 0, 0, 0, 0, 0, 0};
};

/// Sovereign AAC Entropy and Spectral De-quantization Engine.
/// Implements ISO/IEC 14496-3 Section 4.5.2 (Syntax) and Section 4.5.3 (De-quantization).
class AacEntropyDecoder {
public:
    /// Decodes ICS information from bitstream.
    static MediaResult<AacIcsInfo> decode_ics_info(BitReader& reader) noexcept;

    /// Decodes Section Data assigning Huffman codebook indices (0..11) to each SFB.
    static MediaResult<void> decode_section_data(
        BitReader& reader,
        const AacIcsInfo& ics,
        uint8_t* codebooks
    ) noexcept;

    /// Decodes Scale Factor Data using differential DPCM and Huffman coding.
    static MediaResult<void> decode_scalefactors(
        BitReader& reader,
        const AacIcsInfo& ics,
        uint8_t global_gain,
        const uint8_t* codebooks,
        int16_t* scalefactors
    ) noexcept;

    /// Decodes spectral coefficients from the 12 AAC Huffman codebooks.
    static MediaResult<void> decode_spectral_data(
        BitReader& reader,
        const AacIcsInfo& ics,
        const uint8_t* codebooks,
        int32_t* quantized_coeffs
    ) noexcept;

    /// Inverse quantizes integer spectral coefficients to floating-point values:
    /// x_inv = sign(x) * |x|^(4/3) * 2^(0.25 * (sf - 100))
    static void dequantize_spectrum(
        const AacIcsInfo& ics,
        const int32_t* quantized_coeffs,
        const int16_t* scalefactors,
        float* output_spectrum
    ) noexcept;

    /// Standard scalefactor band offset tables for 44.1 kHz, 48 kHz, etc.
    static std::span<const uint16_t> get_sfb_offsets(uint32_t sample_rate, bool is_short) noexcept;
};

} // namespace atum

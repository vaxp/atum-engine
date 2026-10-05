#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include "atum/core/media_types.hpp"
#include "atum/codec/audio/opus/opus_parser.hpp"
#include "atum/codec/audio/opus/opus_range_decoder.hpp"

namespace atum {

/// Number of critical Bark frequency bands in CELT (RFC 6716 Table 55).
inline constexpr size_t CELT_NUM_EBANDS = 21;

/// CELT Decoder Engine (RFC 6716 Section 4.3).
/// Implements PVQ algebraic sphere decoding, band energy synthesis, and MDCT filterbank.
class CeltDecoder {
public:
    CeltDecoder();
    ~CeltDecoder() = default;

    /// Resets overlap-add state and history buffers.
    void reset() noexcept;

    /// Decodes a single CELT frame from the range decoder bitstream into PCM float samples.
    MediaResult<void> decode_frame(
        OpusRangeDecoder& range_dec,
        const OpusToc& toc,
        size_t frame_samples,
        float* pcm_out
    ) noexcept;

    /// Synthesizes Pulse Vector Quantization (PVQ) unit-norm vector on S^{N-1}.
    static void decode_pvq(
        OpusRangeDecoder& range_dec,
        size_t N,
        uint32_t K,
        float* out_vec
    ) noexcept;

    /// Direct IMDCT transform of size N (RFC 6716 Section 4.3.5).
    static void imdct(const float* in_spec, float* out_time, size_t N) noexcept;

private:
    std::vector<float> prev_tail_[2]; // Overlap-add tail for left and right channels
    float old_energy_[2][CELT_NUM_EBANDS]{};
};

} // namespace atum

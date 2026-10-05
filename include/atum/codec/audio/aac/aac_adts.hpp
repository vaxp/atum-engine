#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <optional>
#include "atum/core/media_types.hpp"
#include "atum/codec/audio/aac/aac_types.hpp"

namespace atum {

/// Parsed Audio Data Transport Stream (ADTS) Header according to ISO/IEC 14496-3 Section 1.A.2.2.
struct AacAdtsHeader {
    bool id_mpeg2{false};                    ///< 0: MPEG-4, 1: MPEG-2
    bool protection_absent{true};            ///< 1: No CRC, 0: 16-bit CRC present
    AacAot profile{AacAot::AacLc};           ///< Audio Object Type
    uint8_t sampling_frequency_index{4};     ///< Default: 4 (44.1 kHz)
    uint32_t sample_rate{44100};             ///< Resolved sample rate in Hz
    uint8_t channel_configuration{2};        ///< Default: 2 (Stereo)
    ChannelLayout channel_layout{ChannelLayout::Stereo};
    uint32_t frame_length{0};                ///< Total frame length in bytes including header
    uint16_t adts_buffer_fullness{0x7FF};
    uint8_t num_raw_data_blocks{1};          ///< Number of AAC frames in packet (1..4)
    size_t header_size{7};                   ///< 7 bytes without CRC, 9 bytes with CRC
    uint16_t crc{0};
};

/// Sovereign AAC ADTS Stream Parser.
class AacAdtsParser {
public:
    /// Parses an ADTS header from the beginning of buffer data.
    [[nodiscard]] static MediaResult<AacAdtsHeader> parse(std::span<const uint8_t> data) noexcept;

    /// Searches for the next valid 12-bit 0xFFF syncword in the buffer.
    [[nodiscard]] static std::optional<size_t> find_sync(std::span<const uint8_t> data) noexcept;
};

} // namespace atum

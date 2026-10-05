#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <optional>
#include "atum/core/media_types.hpp"
#include "atum/audio/audio_types.hpp"

namespace atum {

/// FLAC STREAMINFO Metadata Block (IETF RFC FLAC Section 5.1).
struct FlacStreamInfo {
    uint16_t min_block_size{0};
    uint16_t max_block_size{0};
    uint32_t min_frame_size{0};
    uint32_t max_frame_size{0};
    uint32_t sample_rate{44100};
    uint8_t channels{2};
    uint8_t bits_per_sample{16};
    uint64_t total_samples{0};
    uint8_t md5[16]{0};
};

/// FLAC Channel Assignment Scheme.
enum class FlacChannelMode : uint8_t {
    Independent = 0, ///< 1 to 8 independent channels
    LeftSide    = 8, ///< Left (ch 0), Side = Left - Right (ch 1)
    RightSide   = 9, ///< Right (ch 0), Side = Left - Right (ch 1)
    MidSide     = 10 ///< Mid = (Left + Right)/2 (ch 0), Side = Left - Right (ch 1)
};

/// Parsed FLAC Frame Header (Section 5.3).
struct FlacFrameHeader {
    bool variable_block_size{false};
    uint16_t block_size{4096};
    uint32_t sample_rate{44100};
    uint8_t channels{2};
    FlacChannelMode channel_mode{FlacChannelMode::Independent};
    uint8_t bits_per_sample{16};
    uint64_t frame_or_sample_number{0};
    uint8_t crc8{0};
    size_t header_bytes{0};
};

/// Sovereign FLAC Bitstream Parser.
class FlacParser {
public:
    /// Parses STREAMINFO metadata block (34 bytes).
    [[nodiscard]] static MediaResult<FlacStreamInfo> parse_streaminfo(std::span<const uint8_t> data) noexcept;

    /// Parses FLAC frame header starting at 14-bit syncword 0x3FFE.
    [[nodiscard]] static MediaResult<FlacFrameHeader> parse_frame_header(std::span<const uint8_t> data) noexcept;

    /// Hunts for the next valid 14-bit 0x3FFE syncword.
    [[nodiscard]] static std::optional<size_t> find_sync(std::span<const uint8_t> data) noexcept;
};

} // namespace atum

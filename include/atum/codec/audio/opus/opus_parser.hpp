#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <optional>
#include "atum/core/media_types.hpp"
#include "atum/audio/audio_types.hpp"

namespace atum {

/// Opus Audio Bandwidth (RFC 6716 Section 3.1).
enum class OpusBandwidth : uint8_t {
    Narrowband = 0,    ///< 4 kHz audio bandwidth
    Mediumband = 1,    ///< 6 kHz audio bandwidth
    Wideband = 2,      ///< 8 kHz audio bandwidth
    Superwideband = 3, ///< 12 kHz audio bandwidth
    Fullband = 4       ///< 20 kHz audio bandwidth (standard 48 kHz sampling)
};

/// Opus Codec Mode (RFC 6716 Section 3.1).
enum class OpusMode : uint8_t {
    SilkOnly = 0,
    Hybrid = 1,
    CeltOnly = 2
};

/// Parsed Opus Table of Contents (TOC) byte (RFC 6716 Section 3.1).
struct OpusToc {
    uint8_t config{0};         ///< Config index 0..31
    OpusMode mode{OpusMode::CeltOnly};
    OpusBandwidth bandwidth{OpusBandwidth::Fullband};
    float frame_duration_ms{20.0f};
    size_t frame_samples{960};  ///< Frame size in samples at 48 kHz
    bool stereo{false};
    uint8_t code{0};           ///< Frame packaging code (0, 1, 2, 3)
};

/// Individual parsed frame inside an Opus packet.
struct OpusFrame {
    std::span<const uint8_t> data;
    size_t frame_samples{960};
};

/// Parsed Opus Packet containing 1 or more frames.
struct OpusPacket {
    OpusToc toc{};
    std::vector<OpusFrame> frames;
    size_t total_samples{960};
};

/// Sovereign Opus Stream Parser according to RFC 6716.
class OpusParser {
public:
    /// Parses the 1-byte Opus TOC header.
    [[nodiscard]] static MediaResult<OpusToc> parse_toc(uint8_t toc_byte) noexcept;

    /// Parses an entire Opus packet into discrete frames.
    [[nodiscard]] static MediaResult<OpusPacket> parse_packet(std::span<const uint8_t> data) noexcept;
};

} // namespace atum

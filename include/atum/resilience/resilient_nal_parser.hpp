#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <string_view>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/nal_unit.hpp"

namespace atum {

/// Telemetry statistics gathered during resilient NAL parsing and stream recovery.
struct ResilientParseStats {
    size_t total_bytes_processed{0};
    size_t corrupted_bytes_skipped{0};
    size_t valid_nals_extracted{0};
    size_t quarantined_nals_count{0};
    size_t resync_events{0};

    void reset() noexcept {
        total_bytes_processed = 0;
        corrupted_bytes_skipped = 0;
        valid_nals_extracted = 0;
        quarantined_nals_count = 0;
        resync_events = 0;
    }
};

/// Resilient NAL Parser & Start-Code Resynchronization Engine.
/// Hunts for standard Annex-B start codes through corrupted, bit-flipped, or truncated
/// payloads, validates NAL headers, quarantines malformed units, and isolates slice errors.
class ResilientNalParser {
public:
    enum class CodecStandard : uint8_t {
        H264,
        HEVC
    };

    explicit ResilientNalParser(CodecStandard standard = CodecStandard::H264) noexcept
        : standard_(standard) {}

    /// Parses an Annex-B bitstream chunk with active corruption detection and start-code hunting.
    [[nodiscard]] std::vector<NalUnit> parse_and_recover(std::span<const uint8_t> bitstream);

    /// Validates whether a candidate NAL unit header conforms to specifications.
    [[nodiscard]] bool validate_nal_header(std::span<const uint8_t> nal_data) const noexcept;

    /// Hunts forward from `offset` in `data` to locate the next valid start code.
    /// Returns offset to the start code, or size if no valid start code found.
    [[nodiscard]] size_t hunt_next_start_code(const uint8_t* data, size_t size,
                                              size_t start_offset, size_t& out_sc_len) const noexcept;

    [[nodiscard]] const ResilientParseStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_.reset(); }

    void set_codec_standard(CodecStandard standard) noexcept { standard_ = standard; }
    [[nodiscard]] CodecStandard codec_standard() const noexcept { return standard_; }

private:
    CodecStandard standard_{CodecStandard::H264};
    ResilientParseStats stats_;

    bool validate_h264_header(uint8_t header, size_t payload_len) const noexcept;
    bool validate_hevc_header(uint8_t h0, uint8_t h1, size_t payload_len) const noexcept;
};

} // namespace atum

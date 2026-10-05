#pragma once

#include <cstdint>
#include <unordered_map>
#include <optional>
#include <vector>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/nal_unit.hpp"
#include "atum/codec/video/h264/h264_types.hpp"
#include "atum/codec/video/h264/h264_sps.hpp"
#include "atum/codec/video/h264/h264_pps.hpp"
#include "atum/codec/video/h264/h264_slice.hpp"

namespace atum {

/// High-level H.264 / AVC Bitstream and Parameter Set Parser.
class H264Parser {
public:
    H264Parser() = default;

    /// Parses an extradata buffer (either Annex-B parameter sets or AVCC format).
    MediaResult<void> parse_extradata(std::span<const uint8_t> extradata);

    /// Parses a single NAL unit.
    MediaResult<void> parse_nal(const NalUnit& nal);

    /// Parses an Annex-B bitstream chunk containing one or more NAL units.
    MediaResult<void> parse_annex_b(std::span<const uint8_t> bitstream);

    /// Returns the currently active SPS, or nullptr if none parsed yet.
    [[nodiscard]] const H264Sps* active_sps() const noexcept;

    /// Returns the currently active PPS, or nullptr if none parsed yet.
    [[nodiscard]] const H264Pps* active_pps() const noexcept;

    /// Returns the latest parsed slice header, if any.
    [[nodiscard]] const std::optional<H264SliceHeader>& last_slice_header() const noexcept {
        return last_slice_;
    }

    /// Returns the raw slice data payload (after slice header and byte alignment) of the last parsed slice.
    [[nodiscard]] std::span<const uint8_t> last_slice_data() const noexcept {
        return last_slice_data_;
    }

    /// Map of all stored SPS keyed by seq_parameter_set_id.
    [[nodiscard]] const std::unordered_map<uint32_t, H264Sps>& sps_map() const noexcept {
        return sps_map_;
    }

    /// Map of all stored PPS keyed by pic_parameter_set_id.
    [[nodiscard]] const std::unordered_map<uint32_t, H264Pps>& pps_map() const noexcept {
        return pps_map_;
    }

    /// Current picture width in pixels (including cropping).
    [[nodiscard]] uint32_t width() const noexcept {
        auto* sps = active_sps();
        return sps ? sps->width() : 0;
    }

    /// Current picture height in pixels (including cropping).
    [[nodiscard]] uint32_t height() const noexcept {
        auto* sps = active_sps();
        return sps ? sps->height() : 0;
    }

    /// Resets parser state and parameter sets.
    void reset() noexcept {
        sps_map_.clear();
        pps_map_.clear();
        active_sps_id_.reset();
        active_pps_id_.reset();
        last_slice_.reset();
        last_slice_data_.clear();
    }

private:
    std::unordered_map<uint32_t, H264Sps> sps_map_;
    std::unordered_map<uint32_t, H264Pps> pps_map_;
    std::optional<uint32_t> active_sps_id_;
    std::optional<uint32_t> active_pps_id_;
    std::optional<H264SliceHeader> last_slice_;
    std::vector<uint8_t> last_slice_data_;
};

} // namespace atum

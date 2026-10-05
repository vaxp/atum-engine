#pragma once

#include <cstdint>
#include <unordered_map>
#include <optional>
#include <vector>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/nal_unit.hpp"
#include "atum/codec/video/hevc/hevc_types.hpp"
#include "atum/codec/video/hevc/hevc_parameter_sets.hpp"
#include "atum/codec/video/hevc/hevc_slice.hpp"

namespace atum {

/// High-level HEVC / H.265 Bitstream and Parameter Set Parser.
class HevcParser {
public:
    HevcParser() = default;

    /// Parses extradata (either Annex-B parameter sets or hvcC format).
    MediaResult<void> parse_extradata(std::span<const uint8_t> extradata);

    /// Parses a single NAL unit (with 2-byte HEVC header).
    MediaResult<void> parse_nal(std::span<const uint8_t> nal_data);

    /// Parses an Annex-B bitstream chunk containing one or more HEVC NAL units.
    MediaResult<void> parse_annex_b(std::span<const uint8_t> bitstream);

    [[nodiscard]] const HevcVps* active_vps() const noexcept;
    [[nodiscard]] const HevcSps* active_sps() const noexcept;
    [[nodiscard]] const HevcPps* active_pps() const noexcept;

    [[nodiscard]] const std::optional<HevcSliceHeader>& last_slice_header() const noexcept {
        return last_slice_;
    }

    [[nodiscard]] const std::unordered_map<uint32_t, HevcVps>& vps_map() const noexcept { return vps_map_; }
    [[nodiscard]] const std::unordered_map<uint32_t, HevcSps>& sps_map() const noexcept { return sps_map_; }
    [[nodiscard]] const std::unordered_map<uint32_t, HevcPps>& pps_map() const noexcept { return pps_map_; }

    [[nodiscard]] uint32_t width() const noexcept {
        auto* sps = active_sps();
        return sps ? sps->width() : 0;
    }

    [[nodiscard]] uint32_t height() const noexcept {
        auto* sps = active_sps();
        return sps ? sps->height() : 0;
    }

    void reset() noexcept {
        vps_map_.clear();
        sps_map_.clear();
        pps_map_.clear();
        active_vps_id_.reset();
        active_sps_id_.reset();
        active_pps_id_.reset();
        last_slice_.reset();
    }

private:
    std::unordered_map<uint32_t, HevcVps> vps_map_;
    std::unordered_map<uint32_t, HevcSps> sps_map_;
    std::unordered_map<uint32_t, HevcPps> pps_map_;
    std::optional<uint32_t> active_vps_id_;
    std::optional<uint32_t> active_sps_id_;
    std::optional<uint32_t> active_pps_id_;
    std::optional<HevcSliceHeader> last_slice_;
};

} // namespace atum

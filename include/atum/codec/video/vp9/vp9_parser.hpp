#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include <optional>
#include "atum/core/media_types.hpp"
#include "atum/codec/video/vp9/vp9_types.hpp"
#include "atum/codec/video/vp9/vp9_header.hpp"

namespace atum {

/// VP9 Subframe descriptor within a superframe.
struct Vp9Subframe {
    std::span<const uint8_t> data;
    std::optional<Vp9UncompressedHeader> header;
};

/// VP9 Superframe and Packet Parser.
class Vp9Parser {
public:
    Vp9Parser() = default;

    /// Splits a VP9 packet into subframes (unpacking superframe if present).
    MediaResult<std::vector<Vp9Subframe>> parse_packet(std::span<const uint8_t> packet_data);

    /// Latest successfully parsed frame header.
    [[nodiscard]] const std::optional<Vp9UncompressedHeader>& last_header() const noexcept {
        return last_header_;
    }

    [[nodiscard]] uint32_t width() const noexcept {
        return width_ > 0 ? width_ : (last_header_.has_value() ? last_header_->width : 0);
    }

    [[nodiscard]] uint32_t height() const noexcept {
        return height_ > 0 ? height_ : (last_header_.has_value() ? last_header_->height : 0);
    }

    void reset() noexcept {
        last_header_.reset();
        width_ = 0;
        height_ = 0;
    }

private:
    std::optional<Vp9UncompressedHeader> last_header_;
    uint32_t width_{0};
    uint32_t height_{0};
};

} // namespace atum

#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include <optional>
#include "atum/core/media_types.hpp"
#include "atum/codec/video/av1/av1_types.hpp"
#include "atum/codec/video/av1/av1_leb128.hpp"
#include "atum/codec/video/av1/av1_obu.hpp"

namespace atum {

/// Parsed OBU unit descriptor.
struct Av1ObuUnit {
    Av1ObuHeader header;
    std::span<const uint8_t> payload;
};

/// High-level AV1 Bitstream and OBU Parser.
class Av1Parser {
public:
    Av1Parser() = default;

    /// Parses an AV1 stream or packet containing one or more OBUs.
    MediaResult<std::vector<Av1ObuUnit>> parse_stream(std::span<const uint8_t> data);

    /// Parses extradata (such as av1C configuration record or Sequence Header OBU).
    MediaResult<void> parse_extradata(std::span<const uint8_t> extradata);

    [[nodiscard]] const std::optional<Av1SequenceHeader>& sequence_header() const noexcept {
        return sequence_header_;
    }

    [[nodiscard]] const std::optional<Av1FrameHeader>& last_frame_header() const noexcept {
        return last_frame_header_;
    }

    [[nodiscard]] uint32_t width() const noexcept {
        if (last_frame_header_.has_value() && last_frame_header_->frame_width > 0) {
            return last_frame_header_->frame_width;
        }
        return sequence_header_.has_value() ? sequence_header_->width() : 0;
    }

    [[nodiscard]] uint32_t height() const noexcept {
        if (last_frame_header_.has_value() && last_frame_header_->frame_height > 0) {
            return last_frame_header_->frame_height;
        }
        return sequence_header_.has_value() ? sequence_header_->height() : 0;
    }

    void reset() noexcept {
        sequence_header_.reset();
        last_frame_header_.reset();
    }

private:
    std::optional<Av1SequenceHeader> sequence_header_;
    std::optional<Av1FrameHeader> last_frame_header_;
};

} // namespace atum

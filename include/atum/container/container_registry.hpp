#pragma once

#include "atum/container/container.hpp"
#include <memory>
#include <string_view>

namespace atum {

/// Known multimedia container formats supported by the engine.
enum class ContainerFormat {
    Unknown,
    Matroska,
    WebM,
    MpegTs,
    IsoBmff, // MP4, MOV, fMP4
    Wav,
    Ivf
};

/// Unified registry for automated format sniffing, demuxer instantiation, and container multiplexing.
class ContainerRegistry {
public:
    /// Sniffs the stream header magic bytes to detect container format. Preserves reader cursor position.
    static ContainerFormat probe(IoReader& reader);

    /// Auto-detects container format and returns an initialized Demuxer.
    static MediaResult<std::unique_ptr<Demuxer>> create_demuxer(std::shared_ptr<IoReader> reader);

    /// Instantiates a Muxer for the specified container format.
    static MediaResult<std::unique_ptr<Muxer>> create_muxer(ContainerFormat format, std::shared_ptr<IoWriter> writer);

    /// Returns human-readable format description.
    static std::string_view format_name(ContainerFormat format) noexcept;
};

} // namespace atum

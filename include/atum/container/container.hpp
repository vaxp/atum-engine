#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include "atum/core/media_types.hpp"
#include "atum/core/packet.hpp"
#include "atum/codec/codec_parameters.hpp"
#include "atum/io/io_stream.hpp"

namespace atum {

/// Stream metadata in a multimedia container.
struct Stream {
    int32_t index{0};
    CodecParameters codec_params;
    Rational timebase{1, 1000};
    int64_t duration{0};
    int64_t nb_frames{0};
};

/// Abstract container demuxer (stream splitter).
class Demuxer {
public:
    virtual ~Demuxer() = default;

    /// Probes and parses container headers using the provided stream reader.
    virtual MediaResult<void> open(std::shared_ptr<IoReader> reader) = 0;

    /// Reads the next interleaved compressed packet from any active stream.
    virtual MediaResult<void> read_packet(Packet& packet) = 0;

    /// Seeks to `timestamp` (in timebase of specified stream, or default stream if -1).
    virtual MediaResult<void> seek(int64_t timestamp, int32_t stream_index = -1) = 0;

    /// Returns list of elementary streams in container.
    [[nodiscard]] virtual const std::vector<Stream>& streams() const noexcept = 0;

    /// Finds first stream of specified media type (e.g. video or audio).
    [[nodiscard]] const Stream* find_best_stream(MediaType type) const noexcept {
        for (const auto& s : streams()) {
            if (s.codec_params.media_type == type) {
                return &s;
            }
        }
        return nullptr;
    }

    /// Total container duration in microseconds (1/1000000 s).
    [[nodiscard]] virtual int64_t duration() const noexcept = 0;
};

/// Abstract container muxer (stream multiplexer).
class Muxer {
public:
    virtual ~Muxer() = default;

    /// Opens the muxer with a stream writer sink.
    virtual MediaResult<void> open(std::shared_ptr<IoWriter> writer) = 0;

    /// Adds a stream to the container before headers are written. Returns assigned stream index.
    virtual MediaResult<int32_t> add_stream(const CodecParameters& params, Rational timebase) = 0;

    /// Writes container header and metadata.
    virtual MediaResult<void> write_header() = 0;

    /// Writes an interleaved compressed packet to the output container.
    virtual MediaResult<void> write_packet(const Packet& packet) = 0;

    /// Finalizes container indices, durations, and trailers.
    virtual MediaResult<void> write_trailer() = 0;
};

} // namespace atum

#pragma once

#include "atum/container/container.hpp"

namespace atum {

/// Sovereign IVF video container demuxer.
class IvfDemuxer : public Demuxer {
public:
    IvfDemuxer() = default;
    ~IvfDemuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoReader> reader) override;
    MediaResult<void> read_packet(Packet& packet) override;
    MediaResult<void> seek(int64_t timestamp, int32_t stream_index = -1) override;

    [[nodiscard]] const std::vector<Stream>& streams() const noexcept override { return streams_; }
    [[nodiscard]] int64_t duration() const noexcept override { return duration_; }

private:
    std::shared_ptr<IoReader> reader_;
    std::vector<Stream> streams_;
    int64_t duration_{0};
    uint32_t num_frames_{0};
    uint32_t current_frame_{0};
};

/// Sovereign IVF video container muxer.
class IvfMuxer : public Muxer {
public:
    IvfMuxer() = default;
    ~IvfMuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoWriter> writer) override;
    MediaResult<int32_t> add_stream(const CodecParameters& params, Rational timebase) override;
    MediaResult<void> write_header() override;
    MediaResult<void> write_packet(const Packet& packet) override;
    MediaResult<void> write_trailer() override;

private:
    std::shared_ptr<IoWriter> writer_;
    CodecParameters params_;
    Rational timebase_{1, 30};
    uint32_t frame_count_{0};
    bool header_written_{false};
};

} // namespace atum

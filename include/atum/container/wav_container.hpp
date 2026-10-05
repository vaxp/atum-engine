#pragma once

#include "atum/container/container.hpp"

namespace atum {

/// Sovereign RIFF WAV audio demuxer.
class WavDemuxer : public Demuxer {
public:
    WavDemuxer() = default;
    ~WavDemuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoReader> reader) override;
    MediaResult<void> read_packet(Packet& packet) override;
    MediaResult<void> seek(int64_t timestamp, int32_t stream_index = -1) override;

    [[nodiscard]] const std::vector<Stream>& streams() const noexcept override { return streams_; }
    [[nodiscard]] int64_t duration() const noexcept override { return duration_; }

private:
    std::shared_ptr<IoReader> reader_;
    std::vector<Stream> streams_;
    int64_t duration_{0}; // In microseconds
    int64_t data_start_offset_{0};
    int64_t data_size_{0};
    int64_t current_data_offset_{0};
    int64_t current_pts_{0};
    uint32_t block_align_{1};
};

/// Sovereign RIFF WAV audio muxer.
class WavMuxer : public Muxer {
public:
    WavMuxer() = default;
    ~WavMuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoWriter> writer) override;
    MediaResult<int32_t> add_stream(const CodecParameters& params, Rational timebase) override;
    MediaResult<void> write_header() override;
    MediaResult<void> write_packet(const Packet& packet) override;
    MediaResult<void> write_trailer() override;

private:
    std::shared_ptr<IoWriter> writer_;
    CodecParameters params_;
    Rational timebase_{1, 44100};
    uint32_t total_data_bytes_{0};
    bool header_written_{false};
};

} // namespace atum

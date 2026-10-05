#pragma once

#include "atum/container/container.hpp"
#include <vector>
#include <unordered_map>

namespace atum {

/// Sample location and timing record built from ISOBMFF sample tables.
struct IsoSampleRecord {
    uint64_t offset{0};
    uint32_t size{0};
    int64_t pts{0};
    int64_t dts{0};
    int64_t duration{0};
    bool is_keyframe{false};
    int32_t stream_index{0};
};

/// Sovereign ISO Base Media File Format (MP4 / MOV / fMP4) demuxer.
class IsoBmffDemuxer : public Demuxer {
public:
    IsoBmffDemuxer() = default;
    ~IsoBmffDemuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoReader> reader) override;
    MediaResult<void> read_packet(Packet& packet) override;
    MediaResult<void> seek(int64_t timestamp, int32_t stream_index = -1) override;

    [[nodiscard]] const std::vector<Stream>& streams() const noexcept override { return streams_; }
    [[nodiscard]] int64_t duration() const noexcept override { return duration_; }
    [[nodiscard]] bool is_fragmented() const noexcept { return is_fragmented_; }

private:
    struct TrackExtends {
        uint32_t track_id{0};
        uint32_t default_sample_description_index{1};
        uint32_t default_sample_duration{0};
        uint32_t default_sample_size{0};
        uint32_t default_sample_flags{0};
    };

    struct TrackContext {
        Stream stream;
        uint32_t track_id{0};
        uint32_t timescale{1000};
        std::vector<IsoSampleRecord> samples;
        size_t next_sample_idx{0};
        int64_t last_dts{0};
        TrackExtends trex;
    };

    MediaResult<void> parse_moov(int64_t box_start, int64_t box_size);
    MediaResult<void> parse_trak(int64_t box_start, int64_t box_size);
    MediaResult<void> parse_mvex(int64_t box_start, int64_t box_size);
    MediaResult<void> parse_mdia(TrackContext& ctx, int64_t box_start, int64_t box_size);
    MediaResult<void> parse_minf(TrackContext& ctx, int64_t box_start, int64_t box_size);
    MediaResult<void> parse_stbl(TrackContext& ctx, int64_t box_start, int64_t box_size);
    MediaResult<void> parse_moof(int64_t box_start, int64_t box_size);
    MediaResult<void> parse_traf(int64_t box_start, int64_t box_size, int64_t moof_start);
    MediaResult<void> read_next_fragment();

    std::shared_ptr<IoReader> reader_;
    std::vector<Stream> streams_;
    std::vector<TrackContext> tracks_;
    std::unordered_map<uint32_t, size_t> track_id_to_idx_;
    std::vector<IsoSampleRecord> interleaved_samples_;
    size_t current_sample_idx_{0};
    int64_t duration_{0}; // In microseconds
    uint32_t movie_timescale_{1000};
    bool is_fragmented_{false};
};

} // namespace atum

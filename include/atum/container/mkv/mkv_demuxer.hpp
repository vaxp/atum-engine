#pragma once

#include "atum/container/container.hpp"
#include "atum/container/mkv/ebml.hpp"
#include <vector>
#include <deque>
#include <string>
#include <unordered_map>

namespace atum {

/// Random access cue entry for fast bisection seeking.
struct MkvCue {
    int64_t time_ms{0};
    uint64_t track{0};
    int64_t cluster_offset{0};
    uint64_t block_number{0};
};

/// Internal track descriptor in Matroska / WebM.
struct MkvTrack {
    Stream stream;
    uint64_t track_number{0};
    uint64_t track_uid{0};
    MkvTrackType track_type{MkvTrackType::Video};
    std::string codec_id;
    std::vector<uint8_t> codec_private;
    uint64_t default_duration_ns{0};
    uint32_t pixel_width{0};
    uint32_t pixel_height{0};
    double sample_rate{0.0};
    uint32_t channels{0};
    uint32_t bit_depth{0};
};

/// Sovereign Matroska and WebM container demuxer.
class MkvDemuxer : public Demuxer {
public:
    MkvDemuxer() = default;
    ~MkvDemuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoReader> reader) override;
    MediaResult<void> read_packet(Packet& packet) override;
    MediaResult<void> seek(int64_t timestamp, int32_t stream_index = -1) override;

    [[nodiscard]] const std::vector<Stream>& streams() const noexcept override { return streams_; }
    [[nodiscard]] int64_t duration() const noexcept override { return duration_; }
    [[nodiscard]] const std::string& doc_type() const noexcept { return doc_type_; }

private:
    MediaResult<void> parse_ebml_header(const EbmlElementHeader& hdr);
    MediaResult<void> parse_segment(const EbmlElementHeader& hdr);
    MediaResult<void> parse_seek_head(int64_t end_offset);
    MediaResult<void> parse_info(int64_t end_offset);
    MediaResult<void> parse_tracks(int64_t end_offset);
    MediaResult<void> parse_cues(int64_t end_offset);
    MediaResult<void> parse_simple_block(int64_t payload_size);
    MediaResult<void> parse_block_group(int64_t payload_size);
    MediaResult<void> read_next_cluster_or_block();

    static CodecId map_codec_id(std::string_view mkv_codec) noexcept;

    std::shared_ptr<IoReader> reader_;
    std::string doc_type_{"matroska"};
    uint64_t timecode_scale_ns_{1000000}; // Default: 1ms = 1,000,000 ns
    int64_t duration_{0};                // In microseconds
    int64_t segment_data_start_{0};
    int64_t segment_data_end_{0};
    int64_t first_cluster_offset_{0};
    int64_t current_cluster_timecode_{0};

    std::vector<Stream> streams_;
    std::vector<MkvTrack> tracks_;
    std::unordered_map<uint64_t, size_t> track_number_to_stream_idx_;
    std::vector<MkvCue> cues_;
    std::deque<Packet> packet_queue_;
};

} // namespace atum

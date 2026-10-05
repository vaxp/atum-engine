#pragma once

#include "atum/container/container.hpp"
#include "atum/container/mkv/ebml.hpp"
#include <vector>
#include <string>

namespace atum {

/// Sovereign Matroska and WebM container multiplexer.
class MkvMuxer : public Muxer {
public:
    MkvMuxer() = default;
    ~MkvMuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoWriter> writer) override;
    MediaResult<int32_t> add_stream(const CodecParameters& params, Rational timebase) override;
    MediaResult<void> write_header() override;
    MediaResult<void> write_packet(const Packet& packet) override;
    MediaResult<void> write_trailer() override;

    /// Sets the container DocType ("matroska" or "webm").
    void set_doc_type(std::string doc_type) noexcept { doc_type_ = std::move(doc_type); }

private:
    struct TrackInfo {
        int32_t stream_index{0};
        CodecParameters params;
        Rational timebase{1, 1000};
        uint64_t track_number{1};
        std::string codec_id;
    };

    struct CueEntry {
        int64_t time_ms{0};
        uint64_t track_number{1};
        int64_t cluster_position{0};
    };

    static std::string map_to_mkv_codec_id(CodecId id) noexcept;
    MediaResult<void> start_new_cluster(int64_t time_ms);

    std::shared_ptr<IoWriter> writer_;
    std::string doc_type_{"webm"};
    std::vector<TrackInfo> tracks_;
    std::vector<CueEntry> cues_;

    int64_t segment_data_start_{0};
    int64_t current_cluster_start_{0};
    int64_t current_cluster_time_ms_{0};
    bool cluster_active_{false};
    int64_t max_pts_ms_{0};
};

} // namespace atum

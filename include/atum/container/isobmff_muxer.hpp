#pragma once

#include "atum/container/container.hpp"
#include "atum/container/isobmff_demuxer.hpp"
#include <vector>

namespace atum {

/// Sovereign ISO Base Media File Format (MP4 / MOV) multiplexer.
class IsoBmffMuxer : public Muxer {
public:
    IsoBmffMuxer() = default;
    ~IsoBmffMuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoWriter> writer) override;
    MediaResult<int32_t> add_stream(const CodecParameters& params, Rational timebase) override;
    MediaResult<void> write_header() override;
    MediaResult<void> write_packet(const Packet& packet) override;
    MediaResult<void> write_trailer() override;

private:
    struct TrackInfo {
        int32_t stream_index{0};
        uint32_t track_id{1};
        CodecParameters params;
        Rational timebase{1, 1000};
        uint32_t timescale{1000};
        std::vector<IsoSampleRecord> samples;
        int64_t last_dts{0};
    };

    static uint32_t map_codec_to_fourcc(CodecId id) noexcept;

    std::shared_ptr<IoWriter> writer_;
    std::vector<TrackInfo> tracks_;
    int64_t mdat_header_offset_{0};
    int64_t mdat_data_start_{0};
    uint32_t movie_timescale_{1000};
};

} // namespace atum

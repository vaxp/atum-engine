#pragma once

#include "atum/container/container.hpp"
#include "atum/container/ts/mpegts_types.hpp"
#include <vector>
#include <unordered_map>

namespace atum {

/// Sovereign MPEG-2 Transport Stream (MPEG-TS / ISO/IEC 13818-1) Multiplexer.
class TsMuxer : public Muxer {
public:
    TsMuxer() = default;
    ~TsMuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoWriter> writer) override;
    MediaResult<int32_t> add_stream(const CodecParameters& params, Rational timebase) override;
    MediaResult<void> write_header() override;
    MediaResult<void> write_packet(const Packet& packet) override;
    MediaResult<void> write_trailer() override;

private:
    struct StreamEntry {
        int32_t stream_index{0};
        CodecParameters params;
        Rational timebase{1, 90000};
        uint16_t pid{0x0100};
        uint8_t stream_id{0xE0};
        TsStreamType stream_type{TsStreamType::H264};
        uint8_t continuity_counter{0};
    };

    static TsStreamType map_codec_to_ts_type(CodecId id) noexcept;
    MediaResult<void> write_pat_packet();
    MediaResult<void> write_pmt_packet();

    std::shared_ptr<IoWriter> writer_;
    std::vector<StreamEntry> streams_;
    uint16_t program_number_{1};
    uint16_t pmt_pid_{0x1000};
    uint16_t pcr_pid_{0x0100};
    uint8_t pat_cc_{0};
    uint8_t pmt_cc_{0};
    uint64_t packet_counter_{0};
    int64_t last_pcr_pts_{0};
};

} // namespace atum

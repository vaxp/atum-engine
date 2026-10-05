#pragma once

#include "atum/container/container.hpp"
#include "atum/container/ts/mpegts_types.hpp"
#include <unordered_map>
#include <deque>
#include <vector>

namespace atum {

/// Sovereign MPEG-2 Transport Stream (MPEG-TS / ISO/IEC 13818-1) Demuxer.
class TsDemuxer : public Demuxer {
public:
    TsDemuxer() = default;
    ~TsDemuxer() override = default;

    MediaResult<void> open(std::shared_ptr<IoReader> reader) override;
    MediaResult<void> read_packet(Packet& packet) override;
    MediaResult<void> seek(int64_t timestamp, int32_t stream_index = -1) override;

    [[nodiscard]] const std::vector<Stream>& streams() const noexcept override { return streams_; }
    [[nodiscard]] int64_t duration() const noexcept override { return duration_; }
    [[nodiscard]] uint64_t last_pcr_27mhz() const noexcept { return last_pcr_27mhz_; }

private:
    struct PesAccumulator {
        uint16_t pid{0};
        int32_t stream_index{-1};
        std::vector<uint8_t> buffer;
        int64_t pts{NOPTS};
        int64_t dts{NOPTS};
        bool is_keyframe{false};
        bool active{false};
        size_t expected_length{0};
    };

    MediaResult<void> sync_and_read_ts_packet(std::span<uint8_t, TS_PACKET_SIZE> packet_buf);
    static TsHeader parse_ts_header(std::span<const uint8_t, TS_PACKET_SIZE> pkt) noexcept;
    void parse_pat(std::span<const uint8_t> payload);
    void parse_pmt(std::span<const uint8_t> payload);
    void handle_pes_packet(const TsHeader& hdr, std::span<const uint8_t> payload);
    void flush_pes(PesAccumulator& acc);

    std::shared_ptr<IoReader> reader_;
    std::vector<Stream> streams_;
    std::unordered_map<uint16_t, int32_t> pid_to_stream_idx_;
    std::unordered_map<uint16_t, PesAccumulator> pes_accumulators_;
    std::unordered_map<uint16_t, uint16_t> pmt_pids_; // program_number -> pmt_pid
    uint16_t active_pcr_pid_{0};

    std::deque<Packet> packet_queue_;
    int64_t duration_{0};
    uint64_t last_pcr_27mhz_{0};
    int64_t first_packet_offset_{0};
};

} // namespace atum

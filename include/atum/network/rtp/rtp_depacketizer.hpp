#pragma once

#include "atum/network/rtp/rtp_types.hpp"
#include "atum/core/packet.hpp"
#include "atum/core/media_types.hpp"
#include "atum/codec/codec_id.hpp"
#include <vector>
#include <deque>
#include <optional>
#include <map>

namespace atum {

/// Out-of-order UDP packet reordering queue with sequence number rollover handling.
class RtpJitterBuffer {
public:
    explicit RtpJitterBuffer(size_t max_queue_size = 64) noexcept
        : max_queue_size_(max_queue_size) {}

    /// Pushes an incoming RTP packet into the jitter buffer. Returns false if dropped as duplicate/late.
    bool push(RtpPacket packet);

    /// Pops the next strictly in-sequence RTP packet, or std::nullopt if missing.
    std::optional<RtpPacket> pop();

    /// Returns true if there are packets ready to be popped.
    [[nodiscard]] bool has_packet() const noexcept;

    /// Flushes all packets regardless of missing gaps.
    void flush();

    [[nodiscard]] size_t size() const noexcept { return buffer_.size(); }
    [[nodiscard]] uint64_t packets_received() const noexcept { return packets_received_; }
    [[nodiscard]] uint64_t packets_dropped() const noexcept { return packets_dropped_; }

private:
    struct SeqCompare {
        bool operator()(uint16_t a, uint16_t b) const noexcept {
            return static_cast<int16_t>(a - b) < 0;
        }
    };

    size_t max_queue_size_{64};
    std::map<uint16_t, RtpPacket, SeqCompare> buffer_;
    std::optional<uint16_t> expected_seq_;
    uint64_t packets_received_{0};
    uint64_t packets_dropped_{0};
};

/// Sovereign Depacketizer for RFC 6184 (H.264), RFC 7798 (HEVC), and RFC 7587 (Opus).
class RtpDepacketizer {
public:
    explicit RtpDepacketizer(CodecId codec_id, int32_t stream_index = 0)
        : codec_id_(codec_id), stream_index_(stream_index) {}

    /// Ingests a raw RTP packet and reassembles elementary media packets.
    MediaResult<std::vector<Packet>> process_rtp_packet(const RtpPacket& rtp);

    /// Ingests raw UDP packet bytes, parses RTP header, and processes payload.
    MediaResult<std::vector<Packet>> process_raw_bytes(std::span<const uint8_t> raw_bytes);

    void reset() noexcept;

private:
    MediaResult<std::vector<Packet>> process_h264(const RtpPacket& rtp);
    MediaResult<std::vector<Packet>> process_hevc(const RtpPacket& rtp);
    MediaResult<std::vector<Packet>> process_opus(const RtpPacket& rtp);

    CodecId codec_id_{CodecId::H264};
    int32_t stream_index_{0};

    // H.264 Fragmentation Unit (FU-A) assembly buffer
    std::vector<uint8_t> h264_fu_buffer_;
    bool h264_fu_active_{false};
    uint32_t h264_fu_timestamp_{0};

    // HEVC Fragmentation Unit (FU) assembly buffer
    std::vector<uint8_t> hevc_fu_buffer_;
    bool hevc_fu_active_{false};
    uint32_t hevc_fu_timestamp_{0};
};

} // namespace atum

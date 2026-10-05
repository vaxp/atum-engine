#include "atum/network/rtp/rtp_depacketizer.hpp"
#include <algorithm>

namespace atum {

// --- RtpJitterBuffer ---

bool RtpJitterBuffer::push(RtpPacket packet) {
    packets_received_++;
    uint16_t seq = packet.header.sequence_number;

    if (!expected_seq_.has_value()) {
        expected_seq_ = seq;
    } else {
        // If packet sequence is strictly older than expected sequence, drop it
        if (SeqCompare{}(seq, *expected_seq_)) {
            packets_dropped_++;
            return false;
        }
    }

    buffer_[seq] = std::move(packet);

    if (buffer_.size() > max_queue_size_) {
        // Drop oldest to avoid unbound memory growth
        expected_seq_ = buffer_.begin()->first;
    }

    return true;
}

bool RtpJitterBuffer::has_packet() const noexcept {
    if (buffer_.empty() || !expected_seq_.has_value()) return false;
    return buffer_.contains(*expected_seq_) || (buffer_.size() >= max_queue_size_ / 2);
}

std::optional<RtpPacket> RtpJitterBuffer::pop() {
    if (buffer_.empty() || !expected_seq_.has_value()) {
        return std::nullopt;
    }

    // Strict in-order match
    auto it = buffer_.find(*expected_seq_);
    if (it != buffer_.end()) {
        RtpPacket pkt = std::move(it->second);
        buffer_.erase(it);
        expected_seq_ = static_cast<uint16_t>(*expected_seq_ + 1);
        return pkt;
    }

    // If buffer is half-full and expected sequence is missing, skip the gap
    if (buffer_.size() >= max_queue_size_ / 2) {
        auto first_it = buffer_.begin();
        RtpPacket pkt = std::move(first_it->second);
        expected_seq_ = static_cast<uint16_t>(first_it->first + 1);
        buffer_.erase(first_it);
        return pkt;
    }

    return std::nullopt;
}

void RtpJitterBuffer::flush() {
    buffer_.clear();
    expected_seq_.reset();
}

// --- RtpDepacketizer ---

void RtpDepacketizer::reset() noexcept {
    h264_fu_buffer_.clear();
    h264_fu_active_ = false;
    h264_fu_timestamp_ = 0;

    hevc_fu_buffer_.clear();
    hevc_fu_active_ = false;
    hevc_fu_timestamp_ = 0;
}

MediaResult<std::vector<Packet>> RtpDepacketizer::process_raw_bytes(std::span<const uint8_t> raw_bytes) {
    auto rtp_res = RtpPacket::parse(raw_bytes);
    if (!rtp_res.is_ok()) return rtp_res.error();
    return process_rtp_packet(*rtp_res);
}

MediaResult<std::vector<Packet>> RtpDepacketizer::process_rtp_packet(const RtpPacket& rtp) {
    if (rtp.payload.empty()) {
        return std::vector<Packet>{};
    }

    switch (codec_id_) {
        case CodecId::H264: return process_h264(rtp);
        case CodecId::HEVC: return process_hevc(rtp);
        case CodecId::Opus: return process_opus(rtp);
        default:
            return MediaError::NotImplemented;
    }
}

MediaResult<std::vector<Packet>> RtpDepacketizer::process_h264(const RtpPacket& rtp) {
    uint8_t nal_type = rtp.payload[0] & 0x1F;

    if (nal_type >= 1 && nal_type <= 23) {
        // Single NAL unit packet (RFC 6184 Section 5.6)
        std::vector<uint8_t> nalu;
        nalu.reserve(4 + rtp.payload.size());
        nalu.insert(nalu.end(), {0x00, 0x00, 0x00, 0x01});
        nalu.insert(nalu.end(), rtp.payload.begin(), rtp.payload.end());

        Packet pkt = Packet::from_bytes(nalu);
        pkt.set_pts(rtp.header.timestamp);
        pkt.set_dts(rtp.header.timestamp);
        pkt.set_stream_index(stream_index_);
        pkt.set_keyframe(nal_type == 5 || nal_type == 7 || nal_type == 8);

        std::vector<Packet> pkts;
        pkts.push_back(std::move(pkt));
        return pkts;
    }

    if (nal_type == 24) {
        // STAP-A aggregation packet (RFC 6184 Section 5.7.1)
        std::vector<Packet> pkts;
        size_t off = 1;

        while (off + 2 <= rtp.payload.size()) {
            uint16_t nsz = static_cast<uint16_t>((rtp.payload[off] << 8) | rtp.payload[off + 1]);
            off += 2;
            if (off + nsz > rtp.payload.size()) break;

            std::vector<uint8_t> nalu;
            nalu.reserve(4 + nsz);
            nalu.insert(nalu.end(), {0x00, 0x00, 0x00, 0x01});
            nalu.insert(nalu.end(), rtp.payload.begin() + off, rtp.payload.begin() + off + nsz);

            uint8_t sub_type = rtp.payload[off] & 0x1F;
            Packet pkt = Packet::from_bytes(nalu);
            pkt.set_pts(rtp.header.timestamp);
            pkt.set_dts(rtp.header.timestamp);
            pkt.set_stream_index(stream_index_);
            pkt.set_keyframe(sub_type == 5 || sub_type == 7 || sub_type == 8);

            pkts.push_back(std::move(pkt));
            off += nsz;
        }

        return pkts;
    }

    if (nal_type == 28) {
        // FU-A fragmentation unit (RFC 6184 Section 5.8)
        if (rtp.payload.size() < 2) return MediaError::InvalidData;

        uint8_t fu_indicator = rtp.payload[0];
        uint8_t fu_header = rtp.payload[1];
        bool start = (fu_header & 0x80) != 0;
        bool end = (fu_header & 0x40) != 0;
        uint8_t orig_type = fu_header & 0x1F;
        uint8_t reconstructed_hdr = (fu_indicator & 0xE0) | orig_type;

        if (start) {
            h264_fu_buffer_.clear();
            h264_fu_buffer_.insert(h264_fu_buffer_.end(), {0x00, 0x00, 0x00, 0x01, reconstructed_hdr});
            h264_fu_timestamp_ = rtp.header.timestamp;
            h264_fu_active_ = true;
        }

        if (h264_fu_active_) {
            h264_fu_buffer_.insert(h264_fu_buffer_.end(), rtp.payload.begin() + 2, rtp.payload.end());

            if (end) {
                Packet pkt = Packet::from_bytes(h264_fu_buffer_);
                pkt.set_pts(h264_fu_timestamp_);
                pkt.set_dts(h264_fu_timestamp_);
                pkt.set_stream_index(stream_index_);
                pkt.set_keyframe(orig_type == 5);

                h264_fu_active_ = false;
                h264_fu_buffer_.clear();

                std::vector<Packet> pkts;
                pkts.push_back(std::move(pkt));
                return pkts;
            }
        }

        return std::vector<Packet>{};
    }

    return std::vector<Packet>{};
}

MediaResult<std::vector<Packet>> RtpDepacketizer::process_hevc(const RtpPacket& rtp) {
    if (rtp.payload.size() < 2) return MediaError::InvalidData;
    uint8_t nal_type = (rtp.payload[0] >> 1) & 0x3F;

    if (nal_type <= 47) {
        // Single NAL unit packet (RFC 7798 Section 4.4.1)
        std::vector<uint8_t> nalu;
        nalu.reserve(4 + rtp.payload.size());
        nalu.insert(nalu.end(), {0x00, 0x00, 0x00, 0x01});
        nalu.insert(nalu.end(), rtp.payload.begin(), rtp.payload.end());

        Packet pkt = Packet::from_bytes(nalu);
        pkt.set_pts(rtp.header.timestamp);
        pkt.set_dts(rtp.header.timestamp);
        pkt.set_stream_index(stream_index_);
        pkt.set_keyframe(nal_type >= 16 && nal_type <= 21); // IDR / CRA / BLA

        std::vector<Packet> pkts;
        pkts.push_back(std::move(pkt));
        return pkts;
    }

    if (nal_type == 48) {
        // AP aggregation packet (RFC 7798 Section 4.4.2)
        std::vector<Packet> pkts;
        size_t off = 2; // Skip 2-byte AP header

        while (off + 2 <= rtp.payload.size()) {
            uint16_t nsz = static_cast<uint16_t>((rtp.payload[off] << 8) | rtp.payload[off + 1]);
            off += 2;
            if (off + nsz > rtp.payload.size() || nsz < 2) break;

            std::vector<uint8_t> nalu;
            nalu.reserve(4 + nsz);
            nalu.insert(nalu.end(), {0x00, 0x00, 0x00, 0x01});
            nalu.insert(nalu.end(), rtp.payload.begin() + off, rtp.payload.begin() + off + nsz);

            uint8_t sub_type = (rtp.payload[off] >> 1) & 0x3F;
            Packet pkt = Packet::from_bytes(nalu);
            pkt.set_pts(rtp.header.timestamp);
            pkt.set_dts(rtp.header.timestamp);
            pkt.set_stream_index(stream_index_);
            pkt.set_keyframe(sub_type >= 16 && sub_type <= 21);

            pkts.push_back(std::move(pkt));
            off += nsz;
        }

        return pkts;
    }

    if (nal_type == 49) {
        // FU fragmentation unit (RFC 7798 Section 4.4.3)
        if (rtp.payload.size() < 3) return MediaError::InvalidData;

        uint8_t fu_header = rtp.payload[2];
        bool start = (fu_header & 0x80) != 0;
        bool end = (fu_header & 0x40) != 0;
        uint8_t orig_type = fu_header & 0x3F;

        uint8_t hdr0 = (rtp.payload[0] & 0x81) | static_cast<uint8_t>(orig_type << 1);
        uint8_t hdr1 = rtp.payload[1];

        if (start) {
            hevc_fu_buffer_.clear();
            hevc_fu_buffer_.insert(hevc_fu_buffer_.end(), {0x00, 0x00, 0x00, 0x01, hdr0, hdr1});
            hevc_fu_timestamp_ = rtp.header.timestamp;
            hevc_fu_active_ = true;
        }

        if (hevc_fu_active_) {
            hevc_fu_buffer_.insert(hevc_fu_buffer_.end(), rtp.payload.begin() + 3, rtp.payload.end());

            if (end) {
                Packet pkt = Packet::from_bytes(hevc_fu_buffer_);
                pkt.set_pts(hevc_fu_timestamp_);
                pkt.set_dts(hevc_fu_timestamp_);
                pkt.set_stream_index(stream_index_);
                pkt.set_keyframe(orig_type >= 16 && orig_type <= 21);

                hevc_fu_active_ = false;
                hevc_fu_buffer_.clear();

                std::vector<Packet> pkts;
                pkts.push_back(std::move(pkt));
                return pkts;
            }
        }

        return std::vector<Packet>{};
    }

    return std::vector<Packet>{};
}

MediaResult<std::vector<Packet>> RtpDepacketizer::process_opus(const RtpPacket& rtp) {
    Packet pkt = Packet::from_bytes(rtp.payload);
    pkt.set_pts(rtp.header.timestamp);
    pkt.set_dts(rtp.header.timestamp);
    pkt.set_stream_index(stream_index_);
    pkt.set_keyframe(true);

    std::vector<Packet> pkts;
    pkts.push_back(std::move(pkt));
    return pkts;
}

} // namespace atum

#include "atum/container/ts/ts_demuxer.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

TsHeader TsDemuxer::parse_ts_header(std::span<const uint8_t, TS_PACKET_SIZE> pkt) noexcept {
    TsHeader hdr;
    if (pkt[0] != TS_SYNC_BYTE) {
        hdr.transport_error = true;
        return hdr;
    }

    hdr.transport_error = (pkt[1] & 0x80) != 0;
    hdr.payload_unit_start = (pkt[1] & 0x40) != 0;
    hdr.transport_priority = (pkt[1] & 0x20) != 0;
    hdr.pid = static_cast<uint16_t>(((pkt[1] & 0x1F) << 8) | pkt[2]);
    hdr.scrambling_control = (pkt[3] >> 6) & 0x03;
    hdr.adaptation_field_control = (pkt[3] >> 4) & 0x03;
    hdr.continuity_counter = pkt[3] & 0x0F;

    hdr.has_adaptation = (hdr.adaptation_field_control == 0x02 || hdr.adaptation_field_control == 0x03);
    hdr.payload_offset = 4;
    hdr.payload_size = 184;

    if (hdr.has_adaptation) {
        hdr.adaptation_length = pkt[4];
        if (hdr.adaptation_length > 0 && hdr.adaptation_length <= 183) {
            uint8_t flags = pkt[5];
            hdr.discontinuity_indicator = (flags & 0x80) != 0;
            hdr.random_access_indicator = (flags & 0x40) != 0;
            if ((flags & 0x10) != 0 && hdr.adaptation_length >= 7) { // PCR present
                uint64_t pcr_base = (static_cast<uint64_t>(pkt[6]) << 25) |
                                    (static_cast<uint64_t>(pkt[7]) << 17) |
                                    (static_cast<uint64_t>(pkt[8]) << 9)  |
                                    (static_cast<uint64_t>(pkt[9]) << 1)  |
                                    (static_cast<uint64_t>(pkt[10]) >> 7);
                uint64_t pcr_ext = ((static_cast<uint64_t>(pkt[10]) & 0x01) << 8) | pkt[11];
                hdr.has_pcr = true;
                hdr.pcr_27mhz = pcr_base * 300 + pcr_ext;
            }
        }
        hdr.payload_offset = 4 + 1 + hdr.adaptation_length;
        if (hdr.payload_offset > TS_PACKET_SIZE) {
            hdr.payload_size = 0;
        } else {
            hdr.payload_size = TS_PACKET_SIZE - hdr.payload_offset;
        }
    }

    if (hdr.adaptation_field_control == 0x02) { // Adaptation only, no payload
        hdr.payload_size = 0;
    }

    return hdr;
}

MediaResult<void> TsDemuxer::sync_and_read_ts_packet(std::span<uint8_t, TS_PACKET_SIZE> packet_buf) {
    while (reader_->remaining() >= static_cast<int64_t>(TS_PACKET_SIZE)) {
        auto b_res = reader_->read_u8();
        if (!b_res.is_ok()) return b_res.error();
        if (*b_res == TS_SYNC_BYTE) {
            packet_buf[0] = TS_SYNC_BYTE;
            auto r_res = reader_->read_exact(packet_buf.data() + 1, TS_PACKET_SIZE - 1);
            if (!r_res.is_ok()) return r_res;
            return MediaResult<void>::ok();
        }
    }
    return MediaError::EndOfStream;
}

MediaResult<void> TsDemuxer::open(std::shared_ptr<IoReader> reader) {
    if (!reader) return MediaError::IoError;
    reader_ = std::move(reader);
    streams_.clear();
    pid_to_stream_idx_.clear();
    pes_accumulators_.clear();
    pmt_pids_.clear();
    packet_queue_.clear();
    active_pcr_pid_ = 0;
    duration_ = 0;
    last_pcr_27mhz_ = 0;
    first_packet_offset_ = 0;

    // Scan initial packets to discover PAT and PMT
    std::array<uint8_t, TS_PACKET_SIZE> pkt_buf;
    int packets_scanned = 0;
    bool found_pmt = false;

    while (packets_scanned < 2000 && reader_->remaining() >= static_cast<int64_t>(TS_PACKET_SIZE)) {
        int64_t cur_pos = reader_->tell();
        auto res = sync_and_read_ts_packet(pkt_buf);
        if (!res.is_ok()) break;
        if (first_packet_offset_ == 0) first_packet_offset_ = cur_pos;

        TsHeader hdr = parse_ts_header(pkt_buf);
        if (hdr.transport_error) continue;

        if (hdr.has_pcr) {
            last_pcr_27mhz_ = hdr.pcr_27mhz;
        }

        if (hdr.pid == PID_PAT && hdr.payload_size > 0) {
            parse_pat(std::span<const uint8_t>(pkt_buf.data() + hdr.payload_offset, hdr.payload_size));
        } else if (pmt_pids_.contains(hdr.pid) && hdr.payload_size > 0) {
            parse_pmt(std::span<const uint8_t>(pkt_buf.data() + hdr.payload_offset, hdr.payload_size));
            found_pmt = true;
        }

        packets_scanned++;
        if (found_pmt && !streams_.empty()) {
            break;
        }
    }

    if (streams_.empty()) {
        return MediaError::InvalidData;
    }

    // Rewind back to beginning of stream
    reader_->seek(first_packet_offset_, SeekOrigin::Begin);
    return MediaResult<void>::ok();
}

void TsDemuxer::parse_pat(std::span<const uint8_t> payload) {
    if (payload.size() < 8) return;
    size_t offset = 0;
    // Pointer field if payload_unit_start is accounted for
    uint8_t pointer = payload[0];
    offset += 1 + pointer;
    if (offset + 8 > payload.size()) return;

    uint8_t table_id = payload[offset];
    if (table_id != 0x00) return;

    uint16_t section_len = static_cast<uint16_t>(((payload[offset + 1] & 0x0F) << 8) | payload[offset + 2]);
    if (offset + 3 + section_len > payload.size() || section_len < 9) return;

    // Verify CRC-32
    uint32_t crc = TsCrc32::calculate(payload.subspan(offset, 3 + section_len));
    if (crc != 0) return;

    size_t prog_loop_start = offset + 8;
    size_t prog_loop_end = offset + 3 + section_len - 4; // Exclude 4-byte CRC

    for (size_t p = prog_loop_start; p + 4 <= prog_loop_end; p += 4) {
        uint16_t prog_num = static_cast<uint16_t>((payload[p] << 8) | payload[p + 1]);
        uint16_t pmt_pid = static_cast<uint16_t>(((payload[p + 2] & 0x1F) << 8) | payload[p + 3]);
        if (prog_num != 0) { // Program 0 is Network Information Table (NIT)
            pmt_pids_[pmt_pid] = prog_num;
        }
    }
}

void TsDemuxer::parse_pmt(std::span<const uint8_t> payload) {
    if (payload.size() < 12) return;
    size_t offset = 0;
    uint8_t pointer = payload[0];
    offset += 1 + pointer;
    if (offset + 12 > payload.size()) return;

    uint8_t table_id = payload[offset];
    if (table_id != 0x02) return;

    uint16_t section_len = static_cast<uint16_t>(((payload[offset + 1] & 0x0F) << 8) | payload[offset + 2]);
    if (offset + 3 + section_len > payload.size() || section_len < 13) return;

    // Verify CRC-32
    uint32_t crc = TsCrc32::calculate(payload.subspan(offset, 3 + section_len));
    if (crc != 0) return;

    active_pcr_pid_ = static_cast<uint16_t>(((payload[offset + 8] & 0x1F) << 8) | payload[offset + 9]);
    uint16_t prog_info_len = static_cast<uint16_t>(((payload[offset + 10] & 0x0F) << 8) | payload[offset + 11]);

    size_t es_start = offset + 12 + prog_info_len;
    size_t es_end = offset + 3 + section_len - 4; // Exclude CRC

    size_t p = es_start;
    while (p + 5 <= es_end) {
        auto stype = static_cast<TsStreamType>(payload[p]);
        uint16_t elem_pid = static_cast<uint16_t>(((payload[p + 1] & 0x1F) << 8) | payload[p + 2]);
        uint16_t es_info_len = static_cast<uint16_t>(((payload[p + 3] & 0x0F) << 8) | payload[p + 4]);

        if (!pid_to_stream_idx_.contains(elem_pid)) {
            int32_t s_idx = static_cast<int32_t>(streams_.size());
            Stream s;
            s.index = s_idx;
            s.timebase = Rational{1, 90000}; // Standard 90 kHz MPEG timestamp timebase

            switch (stype) {
                case TsStreamType::H264:
                    s.codec_params.codec_id = CodecId::H264;
                    s.codec_params.media_type = MediaType::Video;
                    break;
                case TsStreamType::HEVC:
                    s.codec_params.codec_id = CodecId::HEVC;
                    s.codec_params.media_type = MediaType::Video;
                    break;
                case TsStreamType::AacAdts:
                    s.codec_params.codec_id = CodecId::AAC;
                    s.codec_params.media_type = MediaType::Audio;
                    s.codec_params.sample_rate = 44100;
                    s.codec_params.channels = 2;
                    break;
                case TsStreamType::Opus:
                    s.codec_params.codec_id = CodecId::Opus;
                    s.codec_params.media_type = MediaType::Audio;
                    s.codec_params.sample_rate = 48000;
                    s.codec_params.channels = 2;
                    break;
                case TsStreamType::Ac3:
                case TsStreamType::Eac3:
                    s.codec_params.codec_id = CodecId::Ac3;
                    s.codec_params.media_type = MediaType::Audio;
                    break;
                default:
                    s.codec_params.codec_id = CodecId::Unknown;
                    break;
            }

            pid_to_stream_idx_[elem_pid] = s_idx;
            streams_.push_back(s);

            PesAccumulator acc;
            acc.pid = elem_pid;
            acc.stream_index = s_idx;
            pes_accumulators_[elem_pid] = std::move(acc);
        }

        p += 5 + es_info_len;
    }
}

void TsDemuxer::handle_pes_packet(const TsHeader& hdr, std::span<const uint8_t> payload) {
    auto it = pes_accumulators_.find(hdr.pid);
    if (it == pes_accumulators_.end()) return;

    PesAccumulator& acc = it->second;

    if (hdr.payload_unit_start) {
        if (acc.active && !acc.buffer.empty()) {
            flush_pes(acc);
        }

        if (payload.size() < 9) return;
        // Verify 24-bit PES prefix: 0x00 0x00 0x01
        if (payload[0] != 0x00 || payload[1] != 0x00 || payload[2] != 0x01) return;

        uint16_t pes_packet_len = static_cast<uint16_t>((payload[4] << 8) | payload[5]);
        uint8_t flags2 = payload[7];
        uint8_t pts_dts_flags = (flags2 >> 6) & 0x03;
        uint8_t header_data_len = payload[8];

        int64_t pts = NOPTS;
        int64_t dts = NOPTS;

        size_t off = 9;
        if ((pts_dts_flags & 0x02) != 0 && header_data_len >= 5 && off + 5 <= payload.size()) {
            pts = ((static_cast<int64_t>(payload[off] & 0x0E)) << 29) |
                  ((static_cast<int64_t>(payload[off + 1])) << 22)   |
                  ((static_cast<int64_t>(payload[off + 2] & 0xFE)) << 14) |
                  ((static_cast<int64_t>(payload[off + 3])) << 7)    |
                  ((static_cast<int64_t>(payload[off + 4])) >> 1);
            off += 5;
        }

        if ((pts_dts_flags & 0x01) != 0 && header_data_len >= 10 && off + 5 <= payload.size()) {
            dts = ((static_cast<int64_t>(payload[off] & 0x0E)) << 29) |
                  ((static_cast<int64_t>(payload[off + 1])) << 22)   |
                  ((static_cast<int64_t>(payload[off + 2] & 0xFE)) << 14) |
                  ((static_cast<int64_t>(payload[off + 3])) << 7)    |
                  ((static_cast<int64_t>(payload[off + 4])) >> 1);
        } else {
            dts = pts;
        }

        size_t payload_start = 9 + header_data_len;
        if (pes_packet_len > 0 && pes_packet_len >= 3 + header_data_len) {
            acc.expected_length = pes_packet_len - (3 + header_data_len);
        } else {
            acc.expected_length = 0;
        }
        acc.pts = pts;
        acc.dts = dts;
        acc.is_keyframe = hdr.random_access_indicator;
        acc.active = true;
        acc.buffer.clear();

        if (payload_start < payload.size()) {
            size_t bytes_to_add = payload.size() - payload_start;
            if (acc.expected_length > 0 && bytes_to_add > acc.expected_length) {
                bytes_to_add = acc.expected_length;
            }
            acc.buffer.insert(acc.buffer.end(), payload.begin() + payload_start, payload.begin() + payload_start + bytes_to_add);
        }
    } else {
        if (acc.active) {
            size_t bytes_to_add = payload.size();
            if (acc.expected_length > 0 && acc.buffer.size() + bytes_to_add > acc.expected_length) {
                bytes_to_add = acc.expected_length - acc.buffer.size();
            }
            acc.buffer.insert(acc.buffer.end(), payload.begin(), payload.begin() + bytes_to_add);
            if (hdr.random_access_indicator) {
                acc.is_keyframe = true;
            }
        }
    }
}

void TsDemuxer::flush_pes(PesAccumulator& acc) {
    if (acc.buffer.empty()) return;

    Packet pkt = Packet::from_bytes(acc.buffer);
    pkt.set_pts(acc.pts);
    pkt.set_dts(acc.dts);
    pkt.set_stream_index(acc.stream_index);
    pkt.set_keyframe(acc.is_keyframe);

    packet_queue_.push_back(std::move(pkt));
    acc.buffer.clear();
    acc.active = false;
    acc.is_keyframe = false;
}

MediaResult<void> TsDemuxer::read_packet(Packet& packet) {
    std::array<uint8_t, TS_PACKET_SIZE> pkt_buf;

    while (packet_queue_.empty()) {
        if (reader_->remaining() < static_cast<int64_t>(TS_PACKET_SIZE)) {
            // Flush any pending accumulators at EOF
            for (auto& [_, acc] : pes_accumulators_) {
                if (acc.active && !acc.buffer.empty()) {
                    flush_pes(acc);
                }
            }
            if (packet_queue_.empty()) {
                return MediaError::EndOfStream;
            }
            break;
        }

        auto res = sync_and_read_ts_packet(pkt_buf);
        if (!res.is_ok()) return res;

        TsHeader hdr = parse_ts_header(pkt_buf);
        if (hdr.transport_error) continue;

        if (hdr.has_pcr) {
            last_pcr_27mhz_ = hdr.pcr_27mhz;
        }

        if (hdr.pid == PID_PAT && hdr.payload_size > 0) {
            parse_pat(std::span<const uint8_t>(pkt_buf.data() + hdr.payload_offset, hdr.payload_size));
        } else if (pmt_pids_.contains(hdr.pid) && hdr.payload_size > 0) {
            parse_pmt(std::span<const uint8_t>(pkt_buf.data() + hdr.payload_offset, hdr.payload_size));
        } else if (pid_to_stream_idx_.contains(hdr.pid) && hdr.payload_size > 0) {
            handle_pes_packet(hdr, std::span<const uint8_t>(pkt_buf.data() + hdr.payload_offset, hdr.payload_size));
        }
    }

    packet = std::move(packet_queue_.front());
    packet_queue_.pop_front();
    return MediaResult<void>::ok();
}

MediaResult<void> TsDemuxer::seek(int64_t timestamp, int32_t /*stream_index*/) {
    packet_queue_.clear();
    for (auto& [_, acc] : pes_accumulators_) {
        acc.buffer.clear();
        acc.active = false;
    }

    if (timestamp == 0) {
        return reader_->seek(first_packet_offset_, SeekOrigin::Begin).error();
    }

    // Binary / linear search in TS stream based on PCR or coarse byte ratio
    int64_t total_sz = reader_->size();
    if (total_sz > 0 && duration_ > 0) {
        double ratio = static_cast<double>(timestamp) / static_cast<double>(duration_);
        int64_t target_byte = static_cast<int64_t>(ratio * static_cast<double>(total_sz));
        // Align to 188-byte packet boundary
        target_byte = (target_byte / TS_PACKET_SIZE) * TS_PACKET_SIZE;
        return reader_->seek(target_byte, SeekOrigin::Begin).error();
    }

    return reader_->seek(first_packet_offset_, SeekOrigin::Begin).error();
}

} // namespace atum

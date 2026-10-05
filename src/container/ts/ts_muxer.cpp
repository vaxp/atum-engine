#include "atum/container/ts/ts_muxer.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

TsStreamType TsMuxer::map_codec_to_ts_type(CodecId id) noexcept {
    switch (id) {
        case CodecId::H264: return TsStreamType::H264;
        case CodecId::HEVC: return TsStreamType::HEVC;
        case CodecId::AAC:  return TsStreamType::AacAdts;
        case CodecId::Opus: return TsStreamType::Opus;
        case CodecId::Ac3:  return TsStreamType::Ac3;
        default:            return TsStreamType::Unknown;
    }
}

MediaResult<void> TsMuxer::open(std::shared_ptr<IoWriter> writer) {
    if (!writer) return MediaError::IoError;
    writer_ = std::move(writer);
    streams_.clear();
    pat_cc_ = 0;
    pmt_cc_ = 0;
    packet_counter_ = 0;
    last_pcr_pts_ = 0;
    return MediaResult<void>::ok();
}

MediaResult<int32_t> TsMuxer::add_stream(const CodecParameters& params, Rational timebase) {
    int32_t idx = static_cast<int32_t>(streams_.size());
    StreamEntry se;
    se.stream_index = idx;
    se.params = params;
    se.timebase = timebase;
    se.pid = static_cast<uint16_t>(0x0100 + idx);
    se.stream_type = map_codec_to_ts_type(params.codec_id);
    se.continuity_counter = 0;

    if (params.media_type == MediaType::Video) {
        se.stream_id = 0xE0;
        if (pcr_pid_ == 0x0100) {
            pcr_pid_ = se.pid;
        }
    } else {
        se.stream_id = 0xC0;
    }

    streams_.push_back(std::move(se));
    return idx;
}

MediaResult<void> TsMuxer::write_pat_packet() {
    std::array<uint8_t, TS_PACKET_SIZE> pkt;
    pkt.fill(0xFF);

    pkt[0] = TS_SYNC_BYTE;
    pkt[1] = 0x40; // PUSI = 1, PID = 0x0000 (bits 4..0)
    pkt[2] = 0x00;
    pkt[3] = 0x10 | (pat_cc_ & 0x0F); // Payload only
    pat_cc_ = (pat_cc_ + 1) & 0x0F;

    pkt[4] = 0x00; // pointer_field

    // PAT Section
    size_t s = 5;
    pkt[s++] = 0x00; // table_id = PAT
    uint16_t section_len = 5 + 4 + 4; // Header(5) + 1 Program(4) + CRC(4) = 13
    pkt[s++] = 0xB0 | ((section_len >> 8) & 0x0F);
    pkt[s++] = section_len & 0xFF;
    pkt[s++] = 0x00; // transport_stream_id = 1
    pkt[s++] = 0x01;
    pkt[s++] = 0xC1; // version 0, current_next = 1
    pkt[s++] = 0x00; // section_number = 0
    pkt[s++] = 0x00; // last_section_number = 0

    // Program 1 -> PMT PID
    pkt[s++] = static_cast<uint8_t>(program_number_ >> 8);
    pkt[s++] = static_cast<uint8_t>(program_number_ & 0xFF);
    pkt[s++] = 0xE0 | static_cast<uint8_t>((pmt_pid_ >> 8) & 0x1F);
    pkt[s++] = static_cast<uint8_t>(pmt_pid_ & 0xFF);

    // CRC-32 over section bytes [5 .. s-1]
    uint32_t crc = TsCrc32::calculate(std::span<const uint8_t>(pkt.data() + 5, s - 5));
    pkt[s++] = static_cast<uint8_t>(crc >> 24);
    pkt[s++] = static_cast<uint8_t>(crc >> 16);
    pkt[s++] = static_cast<uint8_t>(crc >> 8);
    pkt[s++] = static_cast<uint8_t>(crc & 0xFF);

    return writer_->write_exact(pkt.data(), TS_PACKET_SIZE);
}

MediaResult<void> TsMuxer::write_pmt_packet() {
    std::array<uint8_t, TS_PACKET_SIZE> pkt;
    pkt.fill(0xFF);

    pkt[0] = TS_SYNC_BYTE;
    pkt[1] = 0x40 | static_cast<uint8_t>((pmt_pid_ >> 8) & 0x1F);
    pkt[2] = static_cast<uint8_t>(pmt_pid_ & 0xFF);
    pkt[3] = 0x10 | (pmt_cc_ & 0x0F);
    pmt_cc_ = (pmt_cc_ + 1) & 0x0F;

    pkt[4] = 0x00; // pointer_field

    size_t s = 5;
    pkt[s++] = 0x02; // table_id = PMT
    uint16_t es_section_bytes = static_cast<uint16_t>(streams_.size() * 5);
    uint16_t section_len = 9 + es_section_bytes + 4;
    pkt[s++] = 0xB0 | ((section_len >> 8) & 0x0F);
    pkt[s++] = section_len & 0xFF;
    pkt[s++] = static_cast<uint8_t>(program_number_ >> 8);
    pkt[s++] = static_cast<uint8_t>(program_number_ & 0xFF);
    pkt[s++] = 0xC1;
    pkt[s++] = 0x00;
    pkt[s++] = 0x00;

    // PCR PID
    pkt[s++] = 0xE0 | static_cast<uint8_t>((pcr_pid_ >> 8) & 0x1F);
    pkt[s++] = static_cast<uint8_t>(pcr_pid_ & 0xFF);
    pkt[s++] = 0xF0; // program_info_length = 0
    pkt[s++] = 0x00;

    // Elementary stream loop
    for (const auto& st : streams_) {
        pkt[s++] = static_cast<uint8_t>(st.stream_type);
        pkt[s++] = 0xE0 | static_cast<uint8_t>((st.pid >> 8) & 0x1F);
        pkt[s++] = static_cast<uint8_t>(st.pid & 0xFF);
        pkt[s++] = 0xF0; // ES_info_length = 0
        pkt[s++] = 0x00;
    }

    uint32_t crc = TsCrc32::calculate(std::span<const uint8_t>(pkt.data() + 5, s - 5));
    pkt[s++] = static_cast<uint8_t>(crc >> 24);
    pkt[s++] = static_cast<uint8_t>(crc >> 16);
    pkt[s++] = static_cast<uint8_t>(crc >> 8);
    pkt[s++] = static_cast<uint8_t>(crc & 0xFF);

    return writer_->write_exact(pkt.data(), TS_PACKET_SIZE);
}

MediaResult<void> TsMuxer::write_header() {
    if (!writer_) return MediaError::IoError;
    auto res = write_pat_packet();
    if (!res.is_ok()) return res;
    res = write_pmt_packet();
    if (!res.is_ok()) return res;
    return writer_->flush();
}

MediaResult<void> TsMuxer::write_packet(const Packet& packet) {
    if (!writer_) return MediaError::IoError;
    if (packet.stream_index() < 0 || static_cast<size_t>(packet.stream_index()) >= streams_.size()) {
        return MediaError::InvalidData;
    }

    auto& st = streams_[packet.stream_index()];

    // Re-emit PAT/PMT periodically or on keyframe
    if ((packet_counter_ % 100 == 0) || packet.is_keyframe()) {
        write_pat_packet();
        write_pmt_packet();
    }

    // Convert PTS to 90 kHz MPEG timestamps
    int64_t pts_90k = packet.pts() >= 0 ? rescale_timestamp(packet.pts(), st.timebase, Rational{1, 90000}) : last_pcr_pts_;
    last_pcr_pts_ = pts_90k;

    // Assemble PES packet
    std::vector<uint8_t> pes;
    pes.reserve(packet.size() + 19);

    // Prefix 0x000001
    pes.push_back(0x00);
    pes.push_back(0x00);
    pes.push_back(0x01);
    pes.push_back(st.stream_id);

    // PES packet length: 0 for unbounded video, or payload + 8
    uint16_t pes_len = 0;
    if (st.stream_id != 0xE0 && (packet.size() + 8 <= 0xFFFF)) {
        pes_len = static_cast<uint16_t>(packet.size() + 8);
    }
    pes.push_back(static_cast<uint8_t>(pes_len >> 8));
    pes.push_back(static_cast<uint8_t>(pes_len & 0xFF));

    // Flags
    pes.push_back(0x80); // '10', unencrypted
    pes.push_back(0x80); // PTS present only
    pes.push_back(0x05); // Header data length = 5

    // 5 bytes PTS
    pes.push_back(static_cast<uint8_t>(0x21 | (((pts_90k >> 29) & 0x0E) | 1)));
    pes.push_back(static_cast<uint8_t>((pts_90k >> 22) & 0xFF));
    pes.push_back(static_cast<uint8_t>((((pts_90k >> 14) & 0xFE) | 1)));
    pes.push_back(static_cast<uint8_t>((pts_90k >> 7) & 0xFF));
    pes.push_back(static_cast<uint8_t>(((pts_90k << 1) & 0xFE) | 1));

    // Elementary payload
    pes.insert(pes.end(), packet.data(), packet.data() + packet.size());

    // Fragment PES into 188-byte TS packets
    size_t pes_offset = 0;
    bool is_first = true;

    while (pes_offset < pes.size()) {
        std::array<uint8_t, TS_PACKET_SIZE> pkt;
        pkt.fill(0xFF);

        pkt[0] = TS_SYNC_BYTE;
        uint8_t pusi = is_first ? 0x40 : 0x00;
        pkt[1] = pusi | static_cast<uint8_t>((st.pid >> 8) & 0x1F);
        pkt[2] = static_cast<uint8_t>(st.pid & 0xFF);
        pkt[3] = (st.continuity_counter & 0x0F);
        st.continuity_counter = (st.continuity_counter + 1) & 0x0F;

        size_t available_payload = 184;
        size_t bytes_left = pes.size() - pes_offset;

        if (is_first && packet.is_keyframe()) {
            // Adaptation field with PCR and random access indicator
            pkt[3] |= 0x30; // AFC = adaptation + payload
            size_t af_header_len = 7; // flags (1) + PCR (6)
            size_t max_payload = 184 - (1 + af_header_len); // 184 - 8 = 176
            size_t stuff_len = (bytes_left < max_payload) ? (max_payload - bytes_left) : 0;
            pkt[4] = static_cast<uint8_t>(af_header_len + stuff_len); // Total adaptation field length
            pkt[5] = 0x50;  // Random access indicator (bit 6) + PCR (bit 4)

            uint64_t pcr_base = static_cast<uint64_t>(pts_90k);
            pkt[6] = static_cast<uint8_t>(pcr_base >> 25);
            pkt[7] = static_cast<uint8_t>((pcr_base >> 17) & 0xFF);
            pkt[8] = static_cast<uint8_t>((pcr_base >> 9) & 0xFF);
            pkt[9] = static_cast<uint8_t>((pcr_base >> 1) & 0xFF);
            pkt[10] = static_cast<uint8_t>(((pcr_base & 1) << 7) | 0x7E);
            pkt[11] = 0x00;

            size_t chunk = std::min(bytes_left, max_payload);
            size_t payload_start = 5 + pkt[4];
            std::memcpy(pkt.data() + payload_start, pes.data() + pes_offset, chunk);
            pes_offset += chunk;
        } else if (bytes_left < available_payload) {
            // Last packet needing padding / stuffing
            size_t stuff_len = available_payload - bytes_left;
            pkt[3] |= 0x30; // AFC = adaptation + payload
            pkt[4] = static_cast<uint8_t>(stuff_len - 1);
            if (stuff_len > 1) {
                pkt[5] = 0x00; // flags
                // Remaining stuffing bytes are already 0xFF
            }
            size_t payload_start = 4 + 1 + pkt[4];
            std::memcpy(pkt.data() + payload_start, pes.data() + pes_offset, bytes_left);
            pes_offset += bytes_left;
        } else {
            // Full payload packet
            pkt[3] |= 0x10; // AFC = payload only
            std::memcpy(pkt.data() + 4, pes.data() + pes_offset, available_payload);
            pes_offset += available_payload;
        }

        auto res = writer_->write_exact(pkt.data(), TS_PACKET_SIZE);
        if (!res.is_ok()) return res;

        is_first = false;
        packet_counter_++;
    }

    return MediaResult<void>::ok();
}

MediaResult<void> TsMuxer::write_trailer() {
    if (!writer_) return MediaError::IoError;
    return writer_->flush();
}

} // namespace atum

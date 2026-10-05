#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include "atum/core/media_types.hpp"

namespace atum {

/// Parsed RFC 3550 RTP Packet Header.
struct RtpHeader {
    uint8_t version{2};
    bool padding{false};
    bool extension{false};
    uint8_t csrc_count{0};
    bool marker{false};
    uint8_t payload_type{96};
    uint16_t sequence_number{0};
    uint32_t timestamp{0};
    uint32_t ssrc{0};
    size_t header_size{12};
    size_t payload_size{0};
};

/// RFC 3550 RTP Packet view.
struct RtpPacket {
    RtpHeader header;
    std::vector<uint8_t> payload;

    static MediaResult<RtpPacket> parse(std::span<const uint8_t> raw_bytes) {
        if (raw_bytes.size() < 12) {
            return MediaError::InvalidData;
        }

        uint8_t b0 = raw_bytes[0];
        uint8_t b1 = raw_bytes[1];

        uint8_t ver = b0 >> 6;
        if (ver != 2) {
            return MediaError::InvalidData; // Standard RTP is version 2
        }

        RtpHeader hdr;
        hdr.version = ver;
        hdr.padding = (b0 & 0x20) != 0;
        hdr.extension = (b0 & 0x10) != 0;
        hdr.csrc_count = b0 & 0x0F;
        hdr.marker = (b1 & 0x80) != 0;
        hdr.payload_type = b1 & 0x7F;

        hdr.sequence_number = static_cast<uint16_t>((raw_bytes[2] << 8) | raw_bytes[3]);
        hdr.timestamp = (static_cast<uint32_t>(raw_bytes[4]) << 24) |
                        (static_cast<uint32_t>(raw_bytes[5]) << 16) |
                        (static_cast<uint32_t>(raw_bytes[6]) << 8)  |
                        static_cast<uint32_t>(raw_bytes[7]);
        hdr.ssrc = (static_cast<uint32_t>(raw_bytes[8]) << 24)  |
                   (static_cast<uint32_t>(raw_bytes[9]) << 16)  |
                   (static_cast<uint32_t>(raw_bytes[10]) << 8)  |
                   static_cast<uint32_t>(raw_bytes[11]);

        size_t off = 12 + static_cast<size_t>(hdr.csrc_count) * 4;
        if (hdr.extension) {
            if (raw_bytes.size() < off + 4) return MediaError::InvalidData;
            uint16_t ext_len = static_cast<uint16_t>((raw_bytes[off + 2] << 8) | raw_bytes[off + 3]);
            off += 4 + static_cast<size_t>(ext_len) * 4;
        }

        if (raw_bytes.size() < off) return MediaError::InvalidData;
        hdr.header_size = off;

        size_t pad_len = 0;
        if (hdr.padding && !raw_bytes.empty()) {
            pad_len = raw_bytes.back();
        }

        if (raw_bytes.size() < off + pad_len) return MediaError::InvalidData;
        hdr.payload_size = raw_bytes.size() - off - pad_len;

        RtpPacket pkt;
        pkt.header = hdr;
        pkt.payload.assign(raw_bytes.begin() + off, raw_bytes.begin() + off + hdr.payload_size);
        return pkt;
    }
};

} // namespace atum

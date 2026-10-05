#include "atum/codec/audio/flac/flac_parser.hpp"
#include <cstring>

namespace atum {

MediaResult<FlacStreamInfo> FlacParser::parse_streaminfo(std::span<const uint8_t> data) noexcept {
    if (data.size() < 34) return MediaError::NeedMoreData;

    FlacStreamInfo info{};
    info.min_block_size = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    info.max_block_size = (static_cast<uint16_t>(data[2]) << 8) | data[3];

    info.min_frame_size = (static_cast<uint32_t>(data[4]) << 16) |
                          (static_cast<uint32_t>(data[5]) << 8)  | data[6];
    info.max_frame_size = (static_cast<uint32_t>(data[7]) << 16) |
                          (static_cast<uint32_t>(data[8]) << 8)  | data[9];

    // Bytes 10..17 (8 bytes = 64 bits):
    // 20 bits sample rate | 3 bits channels - 1 | 5 bits bps - 1 | 36 bits total samples
    uint64_t b10_17 = 0;
    for (size_t i = 0; i < 8; ++i) {
        b10_17 = (b10_17 << 8) | data[10 + i];
    }

    info.sample_rate = static_cast<uint32_t>((b10_17 >> 44) & 0xFFFFF);
    info.channels = static_cast<uint8_t>(((b10_17 >> 41) & 0x07) + 1);
    info.bits_per_sample = static_cast<uint8_t>(((b10_17 >> 36) & 0x1F) + 1);
    info.total_samples = b10_17 & 0x0FFFFFFFFFULL;

    std::memcpy(info.md5, data.data() + 18, 16);
    return info;
}

std::optional<size_t> FlacParser::find_sync(std::span<const uint8_t> data) noexcept {
    if (data.size() < 2) return std::nullopt;
    for (size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i] == 0xFF && (data[i + 1] & 0xFC) == 0xF8) {
            return i;
        }
    }
    return std::nullopt;
}

MediaResult<FlacFrameHeader> FlacParser::parse_frame_header(std::span<const uint8_t> data) noexcept {
    if (data.size() < 4) return MediaError::NeedMoreData;

    // Check 14-bit sync 0x3FFE
    if (data[0] != 0xFF || (data[1] & 0xFC) != 0xF8) {
        return MediaError::InvalidData;
    }

    FlacFrameHeader hdr{};
    hdr.variable_block_size = (data[1] & 0x01) != 0;

    uint8_t bs_idx = (data[2] >> 4) & 0x0F;
    uint8_t sr_idx = data[2] & 0x0F;
    uint8_t ch_idx = (data[3] >> 4) & 0x0F;
    uint8_t bps_idx = (data[3] >> 1) & 0x07;

    // Channels & Channel mode
    if (ch_idx < 8) {
        hdr.channels = ch_idx + 1;
        hdr.channel_mode = FlacChannelMode::Independent;
    } else if (ch_idx == 8) {
        hdr.channels = 2;
        hdr.channel_mode = FlacChannelMode::LeftSide;
    } else if (ch_idx == 9) {
        hdr.channels = 2;
        hdr.channel_mode = FlacChannelMode::RightSide;
    } else if (ch_idx == 10) {
        hdr.channels = 2;
        hdr.channel_mode = FlacChannelMode::MidSide;
    } else {
        return MediaError::InvalidData;
    }

    // Bits per sample
    switch (bps_idx) {
        case 0: hdr.bits_per_sample = 16; break; // get from streaminfo
        case 1: hdr.bits_per_sample = 8; break;
        case 2: hdr.bits_per_sample = 12; break;
        case 4: hdr.bits_per_sample = 16; break;
        case 5: hdr.bits_per_sample = 20; break;
        case 6: hdr.bits_per_sample = 24; break;
        default: return MediaError::InvalidData;
    }

    // Block size lookup
    static constexpr uint16_t BS_TABLE[16] = {
        0, 192, 576, 1152, 2304, 4608, 0, 0,
        256, 512, 1024, 2048, 4096, 8192, 16384, 32768
    };
    hdr.block_size = BS_TABLE[bs_idx];

    // Sample rate lookup
    static constexpr uint32_t SR_TABLE[16] = {
        0, 88200, 176400, 192000, 8000, 16000, 22050, 24000,
        32000, 44100, 48000, 96000, 0, 0, 0, 0
    };
    hdr.sample_rate = SR_TABLE[sr_idx];

    // Decode UTF-8 encoded frame or sample number
    size_t pos = 4;
    if (pos >= data.size()) return MediaError::NeedMoreData;
    uint8_t first_byte = data[pos++];

    uint64_t num = 0;
    int extra_bytes = 0;
    if ((first_byte & 0x80) == 0) {
        num = first_byte;
    } else if ((first_byte & 0xE0) == 0xC0) {
        num = first_byte & 0x1F;
        extra_bytes = 1;
    } else if ((first_byte & 0xF0) == 0xE0) {
        num = first_byte & 0x0F;
        extra_bytes = 2;
    } else if ((first_byte & 0xF8) == 0xF0) {
        num = first_byte & 0x07;
        extra_bytes = 3;
    } else if ((first_byte & 0xFC) == 0xF8) {
        num = first_byte & 0x03;
        extra_bytes = 4;
    } else if ((first_byte & 0xFE) == 0xFC) {
        num = first_byte & 0x01;
        extra_bytes = 5;
    } else if (first_byte == 0xFE) {
        num = 0;
        extra_bytes = 6;
    } else {
        return MediaError::InvalidData;
    }

    if (pos + extra_bytes > data.size()) return MediaError::NeedMoreData;
    for (int i = 0; i < extra_bytes; ++i) {
        uint8_t b = data[pos++];
        if ((b & 0xC0) != 0x80) return MediaError::InvalidData;
        num = (num << 6) | (b & 0x3F);
    }
    hdr.frame_or_sample_number = num;

    // Additional block size bytes if bs_idx == 6 or 7
    if (bs_idx == 6) {
        if (pos >= data.size()) return MediaError::NeedMoreData;
        hdr.block_size = static_cast<uint16_t>(data[pos++]) + 1;
    } else if (bs_idx == 7) {
        if (pos + 1 >= data.size()) return MediaError::NeedMoreData;
        hdr.block_size = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
        hdr.block_size += 1;
        pos += 2;
    }

    // Additional sample rate bytes if sr_idx == 12, 13, 14
    if (sr_idx == 12) {
        if (pos >= data.size()) return MediaError::NeedMoreData;
        hdr.sample_rate = static_cast<uint32_t>(data[pos++]) * 1000;
    } else if (sr_idx == 13) {
        if (pos + 1 >= data.size()) return MediaError::NeedMoreData;
        hdr.sample_rate = (static_cast<uint32_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
    } else if (sr_idx == 14) {
        if (pos + 1 >= data.size()) return MediaError::NeedMoreData;
        hdr.sample_rate = ((static_cast<uint32_t>(data[pos]) << 8) | data[pos + 1]) * 10;
        pos += 2;
    }

    // 1 byte CRC-8
    if (pos >= data.size()) return MediaError::NeedMoreData;
    hdr.crc8 = data[pos++];
    hdr.header_bytes = pos;

    return hdr;
}

} // namespace atum

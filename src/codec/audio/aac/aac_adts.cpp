#include "atum/codec/audio/aac/aac_adts.hpp"

namespace atum {

MediaResult<AacAdtsHeader> AacAdtsParser::parse(std::span<const uint8_t> data) noexcept {
    if (data.size() < 7) {
        return MediaError::NeedMoreData;
    }

    // Verify 12-bit syncword 0xFFF
    if (data[0] != 0xFF || (data[1] & 0xF0) != 0xF0) {
        return MediaError::InvalidData;
    }

    // Check layer bits (must be '00')
    if (((data[1] >> 1) & 0x03) != 0) {
        return MediaError::InvalidData;
    }

    AacAdtsHeader hdr{};
    hdr.id_mpeg2 = ((data[1] >> 3) & 1) != 0;
    hdr.protection_absent = (data[1] & 1) != 0;

    uint8_t profile_bits = (data[2] >> 6) & 0x03;
    hdr.profile = static_cast<AacAot>(profile_bits + 1); // 0 -> Main (1), 1 -> LC (2), 2 -> SSR (3)

    hdr.sampling_frequency_index = (data[2] >> 2) & 0x0F;
    hdr.sample_rate = aac_sample_rate_from_index(hdr.sampling_frequency_index);
    if (hdr.sample_rate == 0) {
        return MediaError::InvalidData;
    }

    hdr.channel_configuration = ((data[2] & 1) << 2) | ((data[3] >> 6) & 0x03);
    hdr.channel_layout = aac_channel_layout_from_config(hdr.channel_configuration);

    // Frame length (13 bits): data[3](1..0) | data[4] | data[5](7..5)
    hdr.frame_length = (static_cast<uint32_t>(data[3] & 0x03) << 11) |
                       (static_cast<uint32_t>(data[4]) << 3) |
                       (static_cast<uint32_t>(data[5] >> 5) & 0x07);

    hdr.adts_buffer_fullness = (static_cast<uint16_t>(data[5] & 0x1F) << 6) |
                               (static_cast<uint16_t>(data[6] >> 2) & 0x3F);

    hdr.num_raw_data_blocks = (data[6] & 0x03) + 1;
    hdr.header_size = hdr.protection_absent ? 7 : 9;

    if (!hdr.protection_absent) {
        if (data.size() < 9) return MediaError::NeedMoreData;
        hdr.crc = (static_cast<uint16_t>(data[7]) << 8) | data[8];
    }

    if (hdr.frame_length < hdr.header_size) {
        return MediaError::InvalidData;
    }

    return hdr;
}

std::optional<size_t> AacAdtsParser::find_sync(std::span<const uint8_t> data) noexcept {
    if (data.size() < 2) return std::nullopt;

    for (size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i] == 0xFF && (data[i + 1] & 0xF0) == 0xF0) {
            // Verify layer == 00
            if (((data[i + 1] >> 1) & 0x03) == 0) {
                return i;
            }
        }
    }
    return std::nullopt;
}

} // namespace atum

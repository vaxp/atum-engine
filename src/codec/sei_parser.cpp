#include "atum/codec/sei_parser.hpp"
#include <cstring>

namespace atum {

static inline uint16_t read_u16_be(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static inline uint32_t read_u32_be(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

std::vector<uint8_t> SeiParser::unescape_ebsp(const uint8_t* data, size_t size) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(size);

    for (size_t i = 0; i < size; ++i) {
        if (i + 2 < size && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x03) {
            rbsp.push_back(0x00);
            rbsp.push_back(0x00);
            i += 2; // skip the emulation prevention byte 0x03
        } else {
            rbsp.push_back(data[i]);
        }
    }
    return rbsp;
}

bool SeiParser::parse_mdcv(const uint8_t* payload, size_t size, MasteringDisplayMetadata& out_mdcv) noexcept {
    if (!payload || size < 24) return false;

    // display_primaries_x and y for c = 0 (Green), 1 (Blue), 2 (Red) per ITU-T H.265 / SMPTE ST 2086
    const uint16_t gx = read_u16_be(payload + 0);
    const uint16_t gy = read_u16_be(payload + 2);
    const uint16_t bx = read_u16_be(payload + 4);
    const uint16_t by = read_u16_be(payload + 6);
    const uint16_t rx = read_u16_be(payload + 8);
    const uint16_t ry = read_u16_be(payload + 10);

    const uint16_t wx = read_u16_be(payload + 12);
    const uint16_t wy = read_u16_be(payload + 14);

    const uint32_t max_lum = read_u32_be(payload + 16);
    const uint32_t min_lum = read_u32_be(payload + 20);

    constexpr float CHROMA_SCALE = 50000.0f;
    constexpr float LUMA_SCALE = 10000.0f;

    // display_primaries: 0 = Red, 1 = Green, 2 = Blue
    out_mdcv.display_primaries[0][0] = static_cast<float>(rx) / CHROMA_SCALE;
    out_mdcv.display_primaries[0][1] = static_cast<float>(ry) / CHROMA_SCALE;
    out_mdcv.display_primaries[1][0] = static_cast<float>(gx) / CHROMA_SCALE;
    out_mdcv.display_primaries[1][1] = static_cast<float>(gy) / CHROMA_SCALE;
    out_mdcv.display_primaries[2][0] = static_cast<float>(bx) / CHROMA_SCALE;
    out_mdcv.display_primaries[2][1] = static_cast<float>(by) / CHROMA_SCALE;

    out_mdcv.white_point[0] = static_cast<float>(wx) / CHROMA_SCALE;
    out_mdcv.white_point[1] = static_cast<float>(wy) / CHROMA_SCALE;

    out_mdcv.max_luminance = static_cast<float>(max_lum) / LUMA_SCALE;
    out_mdcv.min_luminance = static_cast<float>(min_lum) / LUMA_SCALE;
    out_mdcv.has_mastering_display = true;

    return true;
}

bool SeiParser::parse_cll(const uint8_t* payload, size_t size, ContentLightLevel& out_cll) noexcept {
    if (!payload || size < 4) return false;

    out_cll.max_content_light_level = read_u16_be(payload + 0);
    out_cll.max_pic_average_light_level = read_u16_be(payload + 2);
    out_cll.has_cll = true;

    return true;
}

bool SeiParser::parse_h264_h265_sei(const uint8_t* data, size_t size, HdrMetadata& out_meta) {
    if (!data || size == 0) return false;

    // Unescape NAL payload EBSP to RBSP
    auto rbsp = unescape_ebsp(data, size);
    if (rbsp.empty()) return false;

    size_t offset = 0;

    // Check if leading bytes are a NAL unit header:
    // H.264 SEI: nal_unit_type = 6 (1 byte header)
    // HEVC SEI: nal_unit_type = 39 (PREFIX_SEI) or 40 (SUFFIX_SEI) (2 byte header)
    if (rbsp.size() >= 1 && (rbsp[0] & 0x1F) == 6) {
        offset = 1;
    } else if (rbsp.size() >= 2 && (((rbsp[0] >> 1) & 0x3F) == 39 || ((rbsp[0] >> 1) & 0x3F) == 40)) {
        offset = 2;
    }

    bool parsed_any = false;

    while (offset < rbsp.size()) {
        if (rbsp[offset] == 0x80) {
            // RBSP trailing stop bit
            break;
        }

        // Parse payloadType
        uint32_t payload_type = 0;
        while (offset < rbsp.size() && rbsp[offset] == 0xFF) {
            payload_type += 255;
            offset++;
        }
        if (offset >= rbsp.size()) break;
        payload_type += rbsp[offset++];

        // Parse payloadSize
        uint32_t payload_size = 0;
        while (offset < rbsp.size() && rbsp[offset] == 0xFF) {
            payload_size += 255;
            offset++;
        }
        if (offset >= rbsp.size()) break;
        payload_size += rbsp[offset++];

        if (offset + payload_size > rbsp.size()) break;

        const uint8_t* payload_ptr = rbsp.data() + offset;

        if (payload_type == SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME) {
            if (parse_mdcv(payload_ptr, payload_size, out_meta.mastering_display)) {
                parsed_any = true;
            }
        } else if (payload_type == SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO) {
            if (parse_cll(payload_ptr, payload_size, out_meta.light_level)) {
                parsed_any = true;
            }
        }

        offset += payload_size;
    }

    return parsed_any;
}

bool SeiParser::parse_av1_metadata_obu(const uint8_t* data, size_t size, HdrMetadata& out_meta) {
    if (!data || size < 2) return false;

    size_t offset = 0;

    // OBU Header:
    // obu_forbidden_bit (1) | obu_type (4) | obu_extension_flag (1) | obu_has_size_field (1) | obu_reserved_1bit (1)
    const uint8_t header = data[offset++];
    const uint8_t obu_type = (header >> 3) & 0x0F;
    if (obu_type != 5) { // OBU_METADATA
        return false;
    }

    const bool has_extension = (header & 0x04) != 0;
    if (has_extension) {
        if (offset >= size) return false;
        offset++; // skip extension header
    }

    const bool has_size_field = (header & 0x02) != 0;
    if (has_size_field) {
        // Read leb128 obu_size
        uint64_t obu_size = 0;
        int shift = 0;
        while (offset < size) {
            uint8_t byte = data[offset++];
            obu_size |= static_cast<uint64_t>(byte & 0x7F) << shift;
            shift += 7;
            if ((byte & 0x80) == 0) break;
        }
    }

    if (offset >= size) return false;

    // Read metadata_type (leb128)
    uint64_t metadata_type = 0;
    int shift = 0;
    while (offset < size) {
        uint8_t byte = data[offset++];
        metadata_type |= static_cast<uint64_t>(byte & 0x7F) << shift;
        shift += 7;
        if ((byte & 0x80) == 0) break;
    }

    const size_t remaining = size - offset;
    const uint8_t* payload_ptr = data + offset;

    if (metadata_type == AV1_METADATA_TYPE_HDR_CLL) {
        return parse_cll(payload_ptr, remaining, out_meta.light_level);
    } else if (metadata_type == AV1_METADATA_TYPE_HDR_MDCV) {
        return parse_mdcv(payload_ptr, remaining, out_meta.mastering_display);
    }

    return false;
}

} // namespace atum

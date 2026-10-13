#include "atum/codec/video/vp9/vp9_parser.hpp"

namespace atum {

MediaResult<std::vector<Vp9Subframe>> Vp9Parser::parse_packet(std::span<const uint8_t> packet_data) {
    if (packet_data.empty()) return MediaError::NeedMoreData;

    std::vector<Vp9Subframe> subframes;

    // Check for VP9 Superframe Index at the end of the packet
    uint8_t marker = packet_data[packet_data.size() - 1];
    bool is_superframe = false;
    uint8_t num_frames = 1;
    uint8_t bytes_per_frame = 0;
    size_t index_sz = 0;

    if ((marker & 0xE0) == 0xC0) { // 0b110xxxxx
        bytes_per_frame = ((marker >> 3) & 0x03) + 1;
        num_frames = (marker & 0x07) + 1;
        index_sz = 2 + num_frames * bytes_per_frame;

        if (packet_data.size() >= index_sz && packet_data[packet_data.size() - index_sz] == marker) {
            is_superframe = true;
        }
    }

    if (is_superframe) {
        size_t index_offset = packet_data.size() - index_sz + 1;
        size_t current_frame_offset = 0;

        for (uint8_t i = 0; i < num_frames; ++i) {
            uint32_t frame_sz = 0;
            for (uint8_t b = 0; b < bytes_per_frame; ++b) {
                frame_sz |= static_cast<uint32_t>(packet_data[index_offset++]) << (b * 8);
            }

            if (current_frame_offset + frame_sz > packet_data.size() - index_sz) {
                return MediaError::InvalidData;
            }

            std::span<const uint8_t> frame_span{packet_data.data() + current_frame_offset, frame_sz};
            Vp9Subframe sf{};
            sf.data = frame_span;
            auto hdr_res = Vp9UncompressedHeader::parse_frame(frame_span, width_, height_);
            if (hdr_res) {
                if (hdr_res->width > 0) width_ = hdr_res->width;
                if (hdr_res->height > 0) height_ = hdr_res->height;
                sf.header = *hdr_res;
                last_header_ = *hdr_res;
            }
            subframes.push_back(sf);
            current_frame_offset += frame_sz;
        }
    } else {
        Vp9Subframe sf{};
        sf.data = packet_data;
        auto hdr_res = Vp9UncompressedHeader::parse_frame(packet_data, width_, height_);
        if (hdr_res) {
            if (hdr_res->width > 0) width_ = hdr_res->width;
            if (hdr_res->height > 0) height_ = hdr_res->height;
            sf.header = *hdr_res;
            last_header_ = *hdr_res;
        }
        subframes.push_back(sf);
    }

    return subframes;
}

} // namespace atum

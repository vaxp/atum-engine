#include "atum/codec/video/av1/av1_parser.hpp"

namespace atum {

MediaResult<void> Av1Parser::parse_extradata(std::span<const uint8_t> extradata) {
    if (extradata.empty()) return MediaError::NeedMoreData;

    // Check if av1C box (first byte: marker=1, version=1 -> 0x81)
    if (extradata.size() >= 4 && (extradata[0] & 0x80) != 0 && (extradata[0] & 0x7F) == 1) {
        if (extradata.size() > 4) {
            auto res = parse_stream(extradata.subspan(4));
            if (!res) return res.error();
        }
        return {};
    }

    // Direct OBU bitstream
    auto res = parse_stream(extradata);
    if (!res) return res.error();
    return {};
}

MediaResult<std::vector<Av1ObuUnit>> Av1Parser::parse_stream(std::span<const uint8_t> data) {
    if (data.empty()) return MediaError::NeedMoreData;

    std::vector<Av1ObuUnit> units;
    size_t offset = 0;

    while (offset < data.size()) {
        auto hdr_res = Av1ObuHeader::parse(data.subspan(offset));
        if (!hdr_res) return hdr_res.error();

        auto [hdr, hdr_bytes] = *hdr_res;
        offset += hdr_bytes;

        size_t obu_size = 0;
        if (hdr.obu_has_size_field) {
            auto leb_res = Av1Leb128::decode(data.subspan(offset));
            if (!leb_res) return leb_res.error();
            obu_size = static_cast<size_t>(leb_res->first);
            offset += leb_res->second;
        } else {
            obu_size = data.size() - offset;
        }

        if (offset + obu_size > data.size()) {
            return MediaError::InvalidData;
        }

        std::span<const uint8_t> payload{data.data() + offset, obu_size};

        if (hdr.obu_type == Av1ObuType::SequenceHeader) {
            auto seq_res = Av1SequenceHeader::parse_payload(payload);
            if (seq_res) {
                sequence_header_ = *seq_res;
            }
        } else if (hdr.obu_type == Av1ObuType::FrameHeader || hdr.obu_type == Av1ObuType::Frame) {
            if (sequence_header_.has_value()) {
                auto frm_res = Av1FrameHeader::parse_payload(payload, *sequence_header_);
                if (frm_res) {
                    last_frame_header_ = *frm_res;
                }
            }
        }

        units.push_back(Av1ObuUnit{hdr, payload});
        offset += obu_size;
    }

    return units;
}

} // namespace atum

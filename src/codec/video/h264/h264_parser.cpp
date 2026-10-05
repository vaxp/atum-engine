#include "atum/codec/video/h264/h264_parser.hpp"

namespace atum {

const H264Sps* H264Parser::active_sps() const noexcept {
    if (active_sps_id_.has_value()) {
        auto it = sps_map_.find(*active_sps_id_);
        if (it != sps_map_.end()) return &it->second;
    }
    if (!sps_map_.empty()) {
        return &sps_map_.begin()->second;
    }
    return nullptr;
}

const H264Pps* H264Parser::active_pps() const noexcept {
    if (active_pps_id_.has_value()) {
        auto it = pps_map_.find(*active_pps_id_);
        if (it != pps_map_.end()) return &it->second;
    }
    if (!pps_map_.empty()) {
        return &pps_map_.begin()->second;
    }
    return nullptr;
}

MediaResult<void> H264Parser::parse_extradata(std::span<const uint8_t> extradata) {
    if (extradata.size() < 4) return MediaError::InvalidData;

    // Check if Annex-B format (starts with 0x000001 or 0x00000001)
    if (extradata[0] == 0 && extradata[1] == 0 &&
        (extradata[2] == 1 || (extradata[2] == 0 && extradata.size() > 3 && extradata[3] == 1))) {
        return parse_annex_b(extradata);
    }

    // AVCC format (ISO/IEC 14496-15)
    if (extradata[0] == 1 && extradata.size() >= 7) {
        size_t offset = 5;
        uint8_t num_sps = extradata[offset++] & 0x1F;
        for (uint8_t i = 0; i < num_sps; ++i) {
            if (offset + 2 > extradata.size()) return MediaError::InvalidData;
            uint16_t sps_len = (static_cast<uint16_t>(extradata[offset]) << 8) | extradata[offset + 1];
            offset += 2;
            if (offset + sps_len > extradata.size()) return MediaError::InvalidData;

            std::span<const uint8_t> sps_span{extradata.data() + offset, sps_len};
            NalUnit nal{};
            if (!sps_span.empty()) {
                nal.forbidden_zero_bit = (sps_span[0] >> 7) & 1;
                nal.ref_idc = (sps_span[0] >> 5) & 3;
                nal.type = sps_span[0] & 0x1F;
                nal.payload = sps_span.subspan(1);
                auto res = parse_nal(nal);
                if (!res) return res.error();
            }
            offset += sps_len;
        }

        if (offset < extradata.size()) {
            uint8_t num_pps = extradata[offset++];
            for (uint8_t i = 0; i < num_pps; ++i) {
                if (offset + 2 > extradata.size()) return MediaError::InvalidData;
                uint16_t pps_len = (static_cast<uint16_t>(extradata[offset]) << 8) | extradata[offset + 1];
                offset += 2;
                if (offset + pps_len > extradata.size()) return MediaError::InvalidData;

                std::span<const uint8_t> pps_span{extradata.data() + offset, pps_len};
                NalUnit nal{};
                if (!pps_span.empty()) {
                    nal.forbidden_zero_bit = (pps_span[0] >> 7) & 1;
                    nal.ref_idc = (pps_span[0] >> 5) & 3;
                    nal.type = pps_span[0] & 0x1F;
                    nal.payload = pps_span.subspan(1);
                    auto res = parse_nal(nal);
                    if (!res) return res.error();
                }
                offset += pps_len;
            }
        }
        return {};
    }

    return MediaError::InvalidData;
}

MediaResult<void> H264Parser::parse_nal(const NalUnit& nal) {
    auto nal_type = static_cast<H264NalType>(nal.type);
    if (nal.payload.empty()) return {};

    auto rbsp = NalParser::ebsp_to_rbsp(nal.payload);
    std::span<const uint8_t> rbsp_payload = rbsp;
    if (!rbsp_payload.empty() && (rbsp_payload[0] & 0x1F) == nal.type) {
        rbsp_payload = rbsp_payload.subspan(1);
    }

    if (nal_type == H264NalType::Sps) {
        auto sps_res = H264Sps::parse_rbsp(rbsp_payload);
        if (!sps_res) return sps_res.error();
        uint32_t id = sps_res->seq_parameter_set_id;
        sps_map_[id] = std::move(*sps_res);
        if (!active_sps_id_.has_value()) {
            active_sps_id_ = id;
        }
        return {};
    }

    if (nal_type == H264NalType::Pps) {
        auto pps_res = H264Pps::parse_rbsp(rbsp_payload);
        if (!pps_res) return pps_res.error();
        uint32_t id = pps_res->pic_parameter_set_id;
        pps_map_[id] = std::move(*pps_res);
        if (!active_pps_id_.has_value()) {
            active_pps_id_ = id;
        }
        return {};
    }

    if (nal_type == H264NalType::SliceIdr || nal_type == H264NalType::SliceNonIdr) {
        auto* pps = active_pps();
        auto* sps = active_sps();
        if (!pps || !sps) {
            // Cannot parse slice header without parameter sets
            return {};
        }

        BitReader reader(rbsp_payload);
        auto slice_res = H264SliceHeader::parse(reader, nal_type, *sps, *pps);
        if (slice_res) {
            if (slice_res->pic_parameter_set_id != pps->pic_parameter_set_id) {
                auto it = pps_map_.find(slice_res->pic_parameter_set_id);
                if (it != pps_map_.end()) {
                    active_pps_id_ = it->first;
                    auto sps_it = sps_map_.find(it->second.seq_parameter_set_id);
                    if (sps_it != sps_map_.end()) {
                        active_sps_id_ = sps_it->first;
                    }
                }
            }
            last_slice_ = *slice_res;
            reader.align_to_byte();
            auto remaining = reader.remaining_bytes();
            last_slice_data_.assign(remaining.begin(), remaining.end());
        }
        return {};
    }

    return {};
}

MediaResult<void> H264Parser::parse_annex_b(std::span<const uint8_t> bitstream) {
    auto nals = NalParser::split_annex_b(bitstream);
    for (const auto& nal : nals) {
        auto res = parse_nal(nal);
        if (!res) return res.error();
    }
    return {};
}

} // namespace atum

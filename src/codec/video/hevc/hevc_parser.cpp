#include "atum/codec/video/hevc/hevc_parser.hpp"

namespace atum {

const HevcVps* HevcParser::active_vps() const noexcept {
    if (active_vps_id_.has_value()) {
        auto it = vps_map_.find(*active_vps_id_);
        if (it != vps_map_.end()) return &it->second;
    }
    if (!vps_map_.empty()) return &vps_map_.begin()->second;
    return nullptr;
}

const HevcSps* HevcParser::active_sps() const noexcept {
    if (active_sps_id_.has_value()) {
        auto it = sps_map_.find(*active_sps_id_);
        if (it != sps_map_.end()) return &it->second;
    }
    if (!sps_map_.empty()) return &sps_map_.begin()->second;
    return nullptr;
}

const HevcPps* HevcParser::active_pps() const noexcept {
    if (active_pps_id_.has_value()) {
        auto it = pps_map_.find(*active_pps_id_);
        if (it != pps_map_.end()) return &it->second;
    }
    if (!pps_map_.empty()) return &pps_map_.begin()->second;
    return nullptr;
}

MediaResult<void> HevcParser::parse_extradata(std::span<const uint8_t> extradata) {
    if (extradata.size() < 4) return MediaError::InvalidData;

    // Check Annex-B start codes
    if (extradata[0] == 0 && extradata[1] == 0 &&
        (extradata[2] == 1 || (extradata[2] == 0 && extradata.size() > 3 && extradata[3] == 1))) {
        return parse_annex_b(extradata);
    }

    // hvcC format (ISO/IEC 14496-15)
    if (extradata[0] == 1 && extradata.size() >= 23) {
        uint8_t num_arrays = extradata[22];
        size_t offset = 23;
        for (uint8_t a = 0; a < num_arrays; ++a) {
            if (offset + 3 > extradata.size()) return MediaError::InvalidData;
            offset++; // skip array_completeness and NAL_unit_type
            uint16_t num_nalus = (static_cast<uint16_t>(extradata[offset]) << 8) | extradata[offset + 1];
            offset += 2;

            for (uint16_t n = 0; n < num_nalus; ++n) {
                if (offset + 2 > extradata.size()) return MediaError::InvalidData;
                uint16_t nal_len = (static_cast<uint16_t>(extradata[offset]) << 8) | extradata[offset + 1];
                offset += 2;
                if (offset + nal_len > extradata.size()) return MediaError::InvalidData;

                auto res = parse_nal(extradata.subspan(offset, nal_len));
                if (!res) return res.error();
                offset += nal_len;
            }
        }
        return {};
    }

    return MediaError::InvalidData;
}

MediaResult<void> HevcParser::parse_nal(std::span<const uint8_t> nal_data) {
    if (nal_data.size() < 2) return MediaError::InvalidData;

    auto hdr = HevcNalHeader::parse(nal_data);
    if (!hdr) return hdr.error();

    auto payload = nal_data.subspan(2);
    auto rbsp = NalParser::ebsp_to_rbsp(payload);

    if (hdr->nal_unit_type == HevcNalType::VPS_NUT) {
        auto res = HevcVps::parse_rbsp(rbsp);
        if (!res) return res.error();
        uint32_t id = res->vps_video_parameter_set_id;
        vps_map_[id] = std::move(*res);
        if (!active_vps_id_.has_value()) active_vps_id_ = id;
        return {};
    }

    if (hdr->nal_unit_type == HevcNalType::SPS_NUT) {
        auto res = HevcSps::parse_rbsp(rbsp);
        if (!res) return res.error();
        uint32_t id = res->sps_seq_parameter_set_id;
        sps_map_[id] = std::move(*res);
        if (!active_sps_id_.has_value()) active_sps_id_ = id;
        return {};
    }

    if (hdr->nal_unit_type == HevcNalType::PPS_NUT) {
        auto res = HevcPps::parse_rbsp(rbsp);
        if (!res) return res.error();
        uint32_t id = res->pps_pic_parameter_set_id;
        pps_map_[id] = std::move(*res);
        if (!active_pps_id_.has_value()) active_pps_id_ = id;
        return {};
    }

    if (hevc_is_vcl_nal(hdr->nal_unit_type)) {
        auto* sps = active_sps();
        auto* pps = active_pps();
        if (sps && pps) {
            BitReader reader(rbsp);
            auto res = HevcSliceHeader::parse(reader, hdr->nal_unit_type, *sps, *pps);
            if (res) {
                if (res->slice_pic_parameter_set_id != pps->pps_pic_parameter_set_id) {
                    auto it = pps_map_.find(res->slice_pic_parameter_set_id);
                    if (it != pps_map_.end()) {
                        active_pps_id_ = it->first;
                        auto sps_it = sps_map_.find(it->second.pps_seq_parameter_set_id);
                        if (sps_it != sps_map_.end()) active_sps_id_ = sps_it->first;
                    }
                }
                last_slice_ = *res;
            }
        }
        return {};
    }

    return {};
}

MediaResult<void> HevcParser::parse_annex_b(std::span<const uint8_t> bitstream) {
    auto nals = NalParser::split_annex_b(bitstream);
    for (const auto& nal : nals) {
        auto res = parse_nal(nal.payload);
        if (!res) return res.error();
    }
    return {};
}

} // namespace atum

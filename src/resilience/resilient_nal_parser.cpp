#include "atum/resilience/resilient_nal_parser.hpp"

namespace atum {

bool ResilientNalParser::validate_h264_header(uint8_t header, size_t payload_len) const noexcept {
    uint8_t forbidden_zero_bit = (header >> 7) & 0x01;
    if (forbidden_zero_bit != 0) {
        return false;
    }

    uint8_t type = header & 0x1F;
    // Valid standard H.264 NAL unit types are 1..23
    if (type == 0 || type > 23) {
        return false;
    }

    // Minimum payload bounds check
    if (payload_len < 1) {
        return false;
    }

    return true;
}

bool ResilientNalParser::validate_hevc_header(uint8_t h0, uint8_t h1, size_t payload_len) const noexcept {
    uint8_t forbidden_zero_bit = (h0 >> 7) & 0x01;
    if (forbidden_zero_bit != 0) {
        return false;
    }

    uint8_t nal_unit_type = (h0 >> 1) & 0x3F;
    if (nal_unit_type > 63) {
        return false;
    }

    uint8_t nuh_layer_id = ((h0 & 0x01) << 5) | ((h1 >> 3) & 0x1F);
    if (nuh_layer_id > 62) {
        return false;
    }

    uint8_t nuh_temporal_id_plus1 = h1 & 0x07;
    if (nuh_temporal_id_plus1 == 0) {
        return false; // Temporal ID plus 1 must never be 0 according to ITU-T H.265 Section 7.4.2.2
    }

    if (payload_len < 2) {
        return false;
    }

    return true;
}

bool ResilientNalParser::validate_nal_header(std::span<const uint8_t> nal_data) const noexcept {
    if (nal_data.empty()) return false;

    if (standard_ == CodecStandard::H264) {
        return validate_h264_header(nal_data[0], nal_data.size());
    } else {
        if (nal_data.size() < 2) return false;
        return validate_hevc_header(nal_data[0], nal_data[1], nal_data.size());
    }
}

size_t ResilientNalParser::hunt_next_start_code(
    const uint8_t* data, size_t size,
    size_t start_offset, size_t& out_sc_len
) const noexcept {
    out_sc_len = 0;
    if (start_offset >= size || size < 3) return size;

    for (size_t i = start_offset; i + 2 < size; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                out_sc_len = 3;
                return i;
            } else if (i + 3 < size && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                out_sc_len = 4;
                return i;
            }
        }
    }
    return size;
}

std::vector<NalUnit> ResilientNalParser::parse_and_recover(std::span<const uint8_t> bitstream) {
    std::vector<NalUnit> valid_units;
    if (bitstream.empty()) return valid_units;

    const uint8_t* ptr = bitstream.data();
    size_t remaining = bitstream.size();
    stats_.total_bytes_processed += remaining;

    size_t sc_len = 0;
    size_t first_sc = hunt_next_start_code(ptr, remaining, 0, sc_len);
    if (first_sc >= remaining) {
        // No start codes found in buffer (pure garbage or truncated data)
        stats_.corrupted_bytes_skipped += remaining;
        stats_.resync_events++;
        return valid_units;
    }

    if (first_sc > 0) {
        stats_.corrupted_bytes_skipped += first_sc;
        stats_.resync_events++;
        ptr += first_sc;
        remaining -= first_sc;
    }

    while (remaining > sc_len) {
        ptr += sc_len;
        remaining -= sc_len;

        size_t next_sc_len = 0;
        size_t payload_len = hunt_next_start_code(ptr, remaining, 0, next_sc_len);

        if (payload_len > 0) {
            std::span<const uint8_t> payload{ptr, payload_len};
            if (validate_nal_header(payload)) {
                NalUnit unit;
                uint8_t header = ptr[0];
                unit.forbidden_zero_bit = (header >> 7) & 0x01;
                if (standard_ == CodecStandard::H264) {
                    unit.ref_idc = (header >> 5) & 0x03;
                    unit.type = header & 0x1F;
                } else {
                    unit.ref_idc = 0;
                    unit.type = (header >> 1) & 0x3F;
                }
                unit.payload = payload;
                unit.start_code_size = sc_len;

                valid_units.push_back(unit);
                stats_.valid_nals_extracted++;
            } else {
                // Header corrupted: quarantine and skip
                stats_.quarantined_nals_count++;
                stats_.corrupted_bytes_skipped += payload_len;
                stats_.resync_events++;
            }
        }

        ptr += payload_len;
        remaining -= payload_len;
        sc_len = next_sc_len;
    }

    return valid_units;
}

} // namespace atum

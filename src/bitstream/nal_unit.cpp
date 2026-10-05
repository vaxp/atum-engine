#include "atum/bitstream/nal_unit.hpp"

namespace atum {

size_t NalParser::find_next_start_code(const uint8_t* data, size_t size, size_t& out_start_code_len) noexcept {
    out_start_code_len = 0;
    if (size < 3) return size;

    for (size_t i = 0; i + 2 < size; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                // 3-byte start code (0x000001)
                out_start_code_len = 3;
                return i;
            } else if (i + 3 < size && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                // 4-byte start code (0x00000001)
                out_start_code_len = 4;
                return i;
            }
        }
    }
    return size;
}

std::vector<NalUnit> NalParser::split_annex_b(std::span<const uint8_t> bitstream) {
    std::vector<NalUnit> units;
    const uint8_t* ptr = bitstream.data();
    size_t remaining = bitstream.size();

    size_t sc_len = 0;
    size_t first_sc = find_next_start_code(ptr, remaining, sc_len);
    if (first_sc >= remaining) return units;

    ptr += first_sc;
    remaining -= first_sc;

    while (remaining > sc_len) {
        ptr += sc_len;
        remaining -= sc_len;

        size_t next_sc_len = 0;
        size_t payload_len = find_next_start_code(ptr, remaining, next_sc_len);

        if (payload_len > 0) {
            uint8_t header = ptr[0];
            NalUnit unit;
            unit.forbidden_zero_bit = (header >> 7) & 0x01;
            unit.ref_idc = (header >> 5) & 0x03;
            unit.type = header & 0x1F;
            unit.payload = {ptr, payload_len};
            unit.start_code_size = sc_len;
            units.push_back(unit);
        }

        ptr += payload_len;
        remaining -= payload_len;
        sc_len = next_sc_len;
    }

    return units;
}

std::vector<uint8_t> NalParser::ebsp_to_rbsp(std::span<const uint8_t> ebsp) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(ebsp.size());

    size_t zero_count = 0;
    for (size_t i = 0; i < ebsp.size(); ++i) {
        uint8_t b = ebsp[i];
        if (zero_count >= 2 && b == 0x03) {
            // Emulation prevention byte detected!
            // Peek next byte if possible (must be <= 0x03 according to spec)
            if (i + 1 < ebsp.size() && ebsp[i + 1] <= 0x03) {
                zero_count = 0;
                continue; // Skip 0x03
            }
        }

        rbsp.push_back(b);
        if (b == 0x00) {
            zero_count++;
        } else {
            zero_count = 0;
        }
    }

    return rbsp;
}

std::vector<uint8_t> NalParser::rbsp_to_ebsp(std::span<const uint8_t> rbsp) {
    std::vector<uint8_t> ebsp;
    ebsp.reserve(rbsp.size() + rbsp.size() / 16);

    size_t zero_count = 0;
    for (uint8_t b : rbsp) {
        if (zero_count >= 2 && b <= 0x03) {
            ebsp.push_back(0x03);
            zero_count = 0;
        }

        ebsp.push_back(b);
        if (b == 0x00) {
            zero_count++;
        } else {
            zero_count = 0;
        }
    }

    return ebsp;
}

} // namespace atum

#include "atum/container/mkv/ebml.hpp"
#include <bit>
#include <cstring>

namespace atum {

MediaResult<uint32_t> Ebml::read_element_id(IoReader& reader) {
    auto b0_res = reader.read_u8();
    if (!b0_res.is_ok()) return b0_res.error();

    uint8_t b0 = *b0_res;
    if (b0 == 0) {
        return MediaError::InvalidData; // Standard EBML Element IDs cannot exceed 4 bytes
    }

    int len = 1 + std::countl_zero(b0);
    if (len < 1 || len > 4) {
        return MediaError::InvalidData;
    }

    uint32_t id = b0;
    for (int i = 1; i < len; ++i) {
        auto b_res = reader.read_u8();
        if (!b_res.is_ok()) return b_res.error();
        id = (id << 8) | *b_res;
    }

    return id;
}

MediaResult<uint64_t> Ebml::read_vint_size(IoReader& reader) {
    auto b0_res = reader.read_u8();
    if (!b0_res.is_ok()) return b0_res.error();

    uint8_t b0 = *b0_res;
    if (b0 == 0) {
        return MediaError::InvalidData; // VINT size cannot exceed 8 bytes
    }

    int len = 1 + std::countl_zero(b0);
    if (len < 1 || len > 8) {
        return MediaError::InvalidData;
    }

    uint8_t mask = static_cast<uint8_t>(0xFF >> len);
    uint64_t val = b0 & mask;
    bool all_ones = (val == mask);

    for (int i = 1; i < len; ++i) {
        auto b_res = reader.read_u8();
        if (!b_res.is_ok()) return b_res.error();
        uint8_t b = *b_res;
        val = (val << 8) | b;
        if (b != 0xFF) {
            all_ones = false;
        }
    }

    if (all_ones) {
        return EBML_UNKNOWN_SIZE;
    }

    return val;
}

MediaResult<uint64_t> Ebml::read_uint(IoReader& reader, size_t size) {
    if (size == 0) return 0ULL;
    if (size > 8) return MediaError::InvalidData;

    uint64_t val = 0;
    for (size_t i = 0; i < size; ++i) {
        auto b = reader.read_u8();
        if (!b.is_ok()) return b.error();
        val = (val << 8) | *b;
    }
    return val;
}

MediaResult<int64_t> Ebml::read_int(IoReader& reader, size_t size) {
    if (size == 0) return 0LL;
    if (size > 8) return MediaError::InvalidData;

    auto b0 = reader.read_u8();
    if (!b0.is_ok()) return b0.error();

    // Sign extend first byte
    int64_t val = static_cast<int8_t>(*b0);
    for (size_t i = 1; i < size; ++i) {
        auto b = reader.read_u8();
        if (!b.is_ok()) return b.error();
        val = (val << 8) | *b;
    }
    return val;
}

MediaResult<double> Ebml::read_float(IoReader& reader, size_t size) {
    if (size == 0) return 0.0;
    if (size == 4) {
        auto u32 = reader.read_u32_be();
        if (!u32.is_ok()) return u32.error();
        return static_cast<double>(std::bit_cast<float>(*u32));
    }
    if (size == 8) {
        auto u64 = reader.read_u64_be();
        if (!u64.is_ok()) return u64.error();
        return std::bit_cast<double>(*u64);
    }
    return MediaError::InvalidData;
}

MediaResult<std::string> Ebml::read_string(IoReader& reader, size_t size) {
    if (size == 0) return std::string{};
    std::string str;
    str.resize(size);
    auto res = reader.read_exact(reinterpret_cast<uint8_t*>(str.data()), size);
    if (!res.is_ok()) return res.error();

    // Strip trailing null characters if present
    while (!str.empty() && str.back() == '\0') {
        str.pop_back();
    }
    return str;
}

MediaResult<EbmlElementHeader> Ebml::read_element_header(IoReader& reader) {
    int64_t start = reader.tell();
    auto id_res = read_element_id(reader);
    if (!id_res.is_ok()) return id_res.error();

    auto sz_res = read_vint_size(reader);
    if (!sz_res.is_ok()) return sz_res.error();

    int64_t data_offset = reader.tell();
    return EbmlElementHeader{
        .id = *id_res,
        .size = *sz_res,
        .data_offset = data_offset,
        .header_size = data_offset - start
    };
}

size_t Ebml::write_element_id(uint8_t* dst, uint32_t id) noexcept {
    if (id <= 0xFF) {
        dst[0] = static_cast<uint8_t>(id);
        return 1;
    }
    if (id <= 0xFFFF) {
        dst[0] = static_cast<uint8_t>(id >> 8);
        dst[1] = static_cast<uint8_t>(id);
        return 2;
    }
    if (id <= 0xFFFFFF) {
        dst[0] = static_cast<uint8_t>(id >> 16);
        dst[1] = static_cast<uint8_t>(id >> 8);
        dst[2] = static_cast<uint8_t>(id);
        return 3;
    }
    dst[0] = static_cast<uint8_t>(id >> 24);
    dst[1] = static_cast<uint8_t>(id >> 16);
    dst[2] = static_cast<uint8_t>(id >> 8);
    dst[3] = static_cast<uint8_t>(id);
    return 4;
}

size_t Ebml::write_vint_size(uint8_t* dst, uint64_t val, size_t fixed_width) noexcept {
    if (fixed_width >= 1 && fixed_width <= 8) {
        uint8_t marker = static_cast<uint8_t>(1 << (8 - fixed_width));
        dst[0] = marker | static_cast<uint8_t>((val >> (8 * (fixed_width - 1))) & (marker - 1));
        for (size_t i = 1; i < fixed_width; ++i) {
            dst[i] = static_cast<uint8_t>((val >> (8 * (fixed_width - 1 - i))) & 0xFF);
        }
        return fixed_width;
    }

    // Determine minimal width
    size_t len = 1;
    uint64_t limit = (1ULL << 7) - 2; // Subtract 1 for all-ones reserved value
    while (len < 8 && val > limit) {
        len++;
        limit = (1ULL << (7 * len)) - 2;
    }

    uint8_t marker = static_cast<uint8_t>(1 << (8 - len));
    dst[0] = marker | static_cast<uint8_t>((val >> (8 * (len - 1))) & (marker - 1));
    for (size_t i = 1; i < len; ++i) {
        dst[i] = static_cast<uint8_t>((val >> (8 * (len - 1 - i))) & 0xFF);
    }
    return len;
}

MediaResult<void> Ebml::write_uint_element(IoWriter& writer, uint32_t id, uint64_t val) {
    uint8_t id_buf[4];
    size_t id_len = write_element_id(id_buf, id);
    auto res = writer.write_exact(id_buf, id_len);
    if (!res.is_ok()) return res;

    // Determine minimal bytes for val
    size_t val_len = 1;
    if (val > 0xFFFFFFFFFFFFFFULL) val_len = 8;
    else if (val > 0xFFFFFFFFFFFFULL) val_len = 7;
    else if (val > 0xFFFFFFFFFFULL) val_len = 6;
    else if (val > 0xFFFFFFFFULL) val_len = 5;
    else if (val > 0xFFFFFFULL) val_len = 4;
    else if (val > 0xFFFFULL) val_len = 3;
    else if (val > 0xFFULL) val_len = 2;

    uint8_t sz_buf[8];
    size_t sz_len = write_vint_size(sz_buf, val_len);
    res = writer.write_exact(sz_buf, sz_len);
    if (!res.is_ok()) return res;

    uint8_t data[8];
    for (size_t i = 0; i < val_len; ++i) {
        data[i] = static_cast<uint8_t>((val >> (8 * (val_len - 1 - i))) & 0xFF);
    }
    return writer.write_exact(data, val_len);
}

MediaResult<void> Ebml::write_float_element(IoWriter& writer, uint32_t id, double val) {
    uint8_t id_buf[4];
    size_t id_len = write_element_id(id_buf, id);
    auto res = writer.write_exact(id_buf, id_len);
    if (!res.is_ok()) return res;

    uint8_t sz_buf[8];
    size_t sz_len = write_vint_size(sz_buf, 8);
    res = writer.write_exact(sz_buf, sz_len);
    if (!res.is_ok()) return res;

    uint64_t u64 = std::bit_cast<uint64_t>(val);
    return writer.write_u64_be(u64);
}

MediaResult<void> Ebml::write_string_element(IoWriter& writer, uint32_t id, std::string_view str) {
    uint8_t id_buf[4];
    size_t id_len = write_element_id(id_buf, id);
    auto res = writer.write_exact(id_buf, id_len);
    if (!res.is_ok()) return res;

    uint8_t sz_buf[8];
    size_t sz_len = write_vint_size(sz_buf, str.size());
    res = writer.write_exact(sz_buf, sz_len);
    if (!res.is_ok()) return res;

    return writer.write_exact(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

MediaResult<void> Ebml::write_binary_element(IoWriter& writer, uint32_t id, std::span<const uint8_t> data) {
    uint8_t id_buf[4];
    size_t id_len = write_element_id(id_buf, id);
    auto res = writer.write_exact(id_buf, id_len);
    if (!res.is_ok()) return res;

    uint8_t sz_buf[8];
    size_t sz_len = write_vint_size(sz_buf, data.size());
    res = writer.write_exact(sz_buf, sz_len);
    if (!res.is_ok()) return res;

    return writer.write_exact(data.data(), data.size());
}

MediaResult<void> Ebml::write_master_header(IoWriter& writer, uint32_t id, uint64_t payload_size) {
    uint8_t id_buf[4];
    size_t id_len = write_element_id(id_buf, id);
    auto res = writer.write_exact(id_buf, id_len);
    if (!res.is_ok()) return res;

    uint8_t sz_buf[8];
    size_t sz_len = write_vint_size(sz_buf, payload_size);
    return writer.write_exact(sz_buf, sz_len);
}

} // namespace atum

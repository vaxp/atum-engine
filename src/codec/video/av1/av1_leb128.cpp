#include "atum/codec/video/av1/av1_leb128.hpp"

namespace atum {

MediaResult<std::pair<uint64_t, size_t>> Av1Leb128::decode(const uint8_t* data, size_t size) noexcept {
    if (!data || size == 0) return MediaError::NeedMoreData;

    uint64_t value = 0;
    size_t bytes_read = 0;

    for (size_t i = 0; i < 8; ++i) {
        if (i >= size) return MediaError::NeedMoreData;
        uint8_t byte = data[i];
        value |= static_cast<uint64_t>(byte & 0x7F) << (i * 7);
        bytes_read++;
        if ((byte & 0x80) == 0) {
            return std::make_pair(value, bytes_read);
        }
    }

    return MediaError::InvalidData; // More than 8 bytes in LEB128 is disallowed in AV1
}

size_t Av1Leb128::encode(uint64_t value, uint8_t* out_buffer) noexcept {
    if (!out_buffer) return 0;
    size_t bytes_written = 0;
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        out_buffer[bytes_written++] = byte;
    } while (value != 0 && bytes_written < 8);

    return bytes_written;
}

} // namespace atum

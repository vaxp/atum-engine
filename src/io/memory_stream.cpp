#include "atum/io/memory_stream.hpp"
#include <cstring>
#include <algorithm>

namespace atum {

MediaResult<size_t> MemoryReader::read(uint8_t* dst, size_t count) {
    if (!dst && count > 0) return MediaError::InvalidData;
    if (pos_ >= size_) return size_t{0};

    size_t available = size_ - pos_;
    size_t to_read = std::min(count, available);
    if (to_read > 0) {
        std::memcpy(dst, data_ + pos_, to_read);
        pos_ += to_read;
    }
    return to_read;
}

MediaResult<int64_t> MemoryReader::seek(int64_t offset, SeekOrigin origin) {
    int64_t target = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            target = offset;
            break;
        case SeekOrigin::Current:
            target = static_cast<int64_t>(pos_) + offset;
            break;
        case SeekOrigin::End:
            target = static_cast<int64_t>(size_) + offset;
            break;
    }

    if (target < 0 || target > static_cast<int64_t>(size_)) {
        return MediaError::InvalidData;
    }
    pos_ = static_cast<size_t>(target);
    return target;
}

MediaResult<size_t> MemoryWriter::write(const uint8_t* src, size_t count) {
    if (!src && count > 0) return MediaError::InvalidData;
    if (count == 0) return size_t{0};

    size_t required = pos_ + count;
    if (required > buffer_.size()) {
        buffer_.resize(required);
    }
    std::memcpy(buffer_.data() + pos_, src, count);
    pos_ += count;
    return count;
}

MediaResult<int64_t> MemoryWriter::seek(int64_t offset, SeekOrigin origin) {
    int64_t target = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            target = offset;
            break;
        case SeekOrigin::Current:
            target = static_cast<int64_t>(pos_) + offset;
            break;
        case SeekOrigin::End:
            target = static_cast<int64_t>(buffer_.size()) + offset;
            break;
    }

    if (target < 0) {
        return MediaError::InvalidData;
    }
    pos_ = static_cast<size_t>(target);
    return target;
}

} // namespace atum

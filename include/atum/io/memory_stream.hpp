#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include "atum/io/io_stream.hpp"

namespace atum {

/// In-memory byte reader operating directly on contiguous memory without heap allocations.
class MemoryReader : public IoReader {
public:
    constexpr MemoryReader() noexcept = default;
    constexpr MemoryReader(const uint8_t* data, size_t size) noexcept
        : data_(data), size_(size), pos_(0) {}
    constexpr explicit MemoryReader(std::span<const uint8_t> bytes) noexcept
        : data_(bytes.data()), size_(bytes.size()), pos_(0) {}

    MediaResult<size_t> read(uint8_t* dst, size_t size) override;
    MediaResult<int64_t> seek(int64_t offset, SeekOrigin origin) override;

    [[nodiscard]] int64_t tell() const noexcept override { return static_cast<int64_t>(pos_); }
    [[nodiscard]] int64_t size() const noexcept override { return static_cast<int64_t>(size_); }
    [[nodiscard]] bool is_seekable() const noexcept override { return true; }

    /// Returns a direct zero-copy span of the remaining bytes from current cursor.
    [[nodiscard]] std::span<const uint8_t> remaining_span() const noexcept {
        if (pos_ >= size_) return {};
        return {data_ + pos_, size_ - pos_};
    }

    /// Returns a direct zero-copy span of the entire buffer.
    [[nodiscard]] std::span<const uint8_t> full_span() const noexcept {
        return {data_, size_};
    }

private:
    const uint8_t* data_{nullptr};
    size_t size_{0};
    size_t pos_{0};
};

/// Dynamic in-memory growable byte writer.
class MemoryWriter : public IoWriter {
public:
    MemoryWriter() = default;
    explicit MemoryWriter(size_t initial_capacity) {
        buffer_.reserve(initial_capacity);
    }

    MediaResult<size_t> write(const uint8_t* src, size_t size) override;
    MediaResult<int64_t> seek(int64_t offset, SeekOrigin origin) override;

    [[nodiscard]] int64_t tell() const noexcept override { return static_cast<int64_t>(pos_); }
    [[nodiscard]] size_t size() const noexcept { return buffer_.size(); }

    MediaResult<void> flush() override { return MediaResult<void>::ok(); }

    [[nodiscard]] const uint8_t* data() const noexcept { return buffer_.data(); }
    [[nodiscard]] std::span<const uint8_t> as_span() const noexcept { return buffer_; }
    [[nodiscard]] std::vector<uint8_t>& buffer() noexcept { return buffer_; }
    [[nodiscard]] const std::vector<uint8_t>& buffer() const noexcept { return buffer_; }

    void clear() noexcept {
        buffer_.clear();
        pos_ = 0;
    }

private:
    std::vector<uint8_t> buffer_;
    size_t pos_{0};
};

} // namespace atum

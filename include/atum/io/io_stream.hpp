#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <string_view>
#include <array>
#include "atum/core/media_types.hpp"

namespace atum {

/// Specifies the position in a stream used for seeking.
enum class SeekOrigin {
    Begin,
    Current,
    End
};

/// Four-character code (FourCC) utility.
constexpr uint32_t make_fourcc(char a, char b, char c, char d) noexcept {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)));
}

/// Abstract streaming byte reader interface with high-performance endian primitives.
class IoReader {
public:
    virtual ~IoReader() = default;

    /// Reads up to `size` bytes into `dst`. Returns the number of bytes read, or MediaError.
    virtual MediaResult<size_t> read(uint8_t* dst, size_t size) = 0;

    /// Moves the read cursor to `offset` relative to `origin`. Returns current position, or MediaError.
    virtual MediaResult<int64_t> seek(int64_t offset, SeekOrigin origin) = 0;

    /// Returns current read position in bytes, or negative on error.
    [[nodiscard]] virtual int64_t tell() const = 0;

    /// Returns total stream size in bytes, or -1 if unknown/streaming.
    [[nodiscard]] virtual int64_t size() const = 0;

    /// Returns true if random seeking is supported.
    [[nodiscard]] virtual bool is_seekable() const noexcept { return true; }

    /// Returns remaining bytes available to read, or -1 if unknown.
    [[nodiscard]] virtual int64_t remaining() const {
        int64_t total = size();
        if (total < 0) return -1;
        int64_t pos = tell();
        return pos >= 0 && pos <= total ? total - pos : 0;
    }

    // --- High-Performance Helper Methods ---

    MediaResult<void> read_exact(uint8_t* dst, size_t size) {
        size_t total_read = 0;
        while (total_read < size) {
            auto res = read(dst + total_read, size - total_read);
            if (!res.is_ok()) return res.error();
            if (*res == 0) return MediaError::EndOfStream;
            total_read += *res;
        }
        return MediaResult<void>::ok();
    }

    MediaResult<void> skip(int64_t bytes) {
        if (bytes == 0) return MediaResult<void>::ok();
        if (is_seekable()) {
            auto res = seek(bytes, SeekOrigin::Current);
            if (res.is_ok()) return MediaResult<void>::ok();
        }
        // Fallback: read and discard
        uint8_t scratch[512];
        int64_t remaining = bytes;
        while (remaining > 0) {
            size_t chunk = static_cast<size_t>(std::min<int64_t>(remaining, sizeof(scratch)));
            auto res = read(scratch, chunk);
            if (!res.is_ok()) return res.error();
            if (*res == 0) return MediaError::EndOfStream;
            remaining -= *res;
        }
        return MediaResult<void>::ok();
    }

    MediaResult<uint8_t> read_u8() {
        uint8_t b = 0;
        auto res = read_exact(&b, 1);
        if (!res.is_ok()) return res.error();
        return b;
    }

    MediaResult<uint16_t> read_u16_be() {
        uint8_t b[2];
        auto res = read_exact(b, 2);
        if (!res.is_ok()) return res.error();
        return static_cast<uint16_t>((b[0] << 8) | b[1]);
    }

    MediaResult<uint16_t> read_u16_le() {
        uint8_t b[2];
        auto res = read_exact(b, 2);
        if (!res.is_ok()) return res.error();
        return static_cast<uint16_t>(b[0] | (b[1] << 8));
    }

    MediaResult<uint32_t> read_u24_be() {
        uint8_t b[3];
        auto res = read_exact(b, 3);
        if (!res.is_ok()) return res.error();
        return static_cast<uint32_t>((b[0] << 16) | (b[1] << 8) | b[2]);
    }

    MediaResult<uint32_t> read_u32_be() {
        uint8_t b[4];
        auto res = read_exact(b, 4);
        if (!res.is_ok()) return res.error();
        return (static_cast<uint32_t>(b[0]) << 24) |
               (static_cast<uint32_t>(b[1]) << 16) |
               (static_cast<uint32_t>(b[2]) << 8)  |
               static_cast<uint32_t>(b[3]);
    }

    MediaResult<uint32_t> read_u32_le() {
        uint8_t b[4];
        auto res = read_exact(b, 4);
        if (!res.is_ok()) return res.error();
        return static_cast<uint32_t>(b[0]) |
               (static_cast<uint32_t>(b[1]) << 8)  |
               (static_cast<uint32_t>(b[2]) << 16) |
               (static_cast<uint32_t>(b[3]) << 24);
    }

    MediaResult<uint64_t> read_u64_be() {
        uint8_t b[8];
        auto res = read_exact(b, 8);
        if (!res.is_ok()) return res.error();
        return (static_cast<uint64_t>(b[0]) << 56) |
               (static_cast<uint64_t>(b[1]) << 48) |
               (static_cast<uint64_t>(b[2]) << 40) |
               (static_cast<uint64_t>(b[3]) << 32) |
               (static_cast<uint64_t>(b[4]) << 24) |
               (static_cast<uint64_t>(b[5]) << 16) |
               (static_cast<uint64_t>(b[6]) << 8)  |
               static_cast<uint64_t>(b[7]);
    }

    MediaResult<uint64_t> read_u64_le() {
        uint8_t b[8];
        auto res = read_exact(b, 8);
        if (!res.is_ok()) return res.error();
        return static_cast<uint64_t>(b[0]) |
               (static_cast<uint64_t>(b[1]) << 8)  |
               (static_cast<uint64_t>(b[2]) << 16) |
               (static_cast<uint64_t>(b[3]) << 24) |
               (static_cast<uint64_t>(b[4]) << 32) |
               (static_cast<uint64_t>(b[5]) << 40) |
               (static_cast<uint64_t>(b[6]) << 48) |
               (static_cast<uint64_t>(b[7]) << 56);
    }

    MediaResult<uint32_t> read_fourcc() {
        return read_u32_be();
    }
};

/// Abstract streaming byte writer interface.
class IoWriter {
public:
    virtual ~IoWriter() = default;

    /// Writes up to `size` bytes from `src`. Returns the number of bytes written, or MediaError.
    virtual MediaResult<size_t> write(const uint8_t* src, size_t size) = 0;

    /// Moves the write cursor to `offset` relative to `origin`.
    virtual MediaResult<int64_t> seek(int64_t offset, SeekOrigin origin) = 0;

    /// Returns current write position in bytes.
    [[nodiscard]] virtual int64_t tell() const = 0;

    /// Flushes any buffered bytes to underlying sink.
    virtual MediaResult<void> flush() = 0;

    // --- Helper Methods ---

    MediaResult<void> write_exact(const uint8_t* src, size_t size) {
        size_t total_written = 0;
        while (total_read_chunk(total_written, size)) {
            auto res = write(src + total_written, size - total_written);
            if (!res.is_ok()) return res.error();
            if (*res == 0) return MediaError::IoError;
            total_written += *res;
        }
        return MediaResult<void>::ok();
    }

    MediaResult<void> write_u8(uint8_t val) {
        return write_exact(&val, 1);
    }

    MediaResult<void> write_u16_be(uint16_t val) {
        uint8_t b[2] = {static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val)};
        return write_exact(b, 2);
    }

    MediaResult<void> write_u16_le(uint16_t val) {
        uint8_t b[2] = {static_cast<uint8_t>(val), static_cast<uint8_t>(val >> 8)};
        return write_exact(b, 2);
    }

    MediaResult<void> write_u24_be(uint32_t val) {
        uint8_t b[3] = {
            static_cast<uint8_t>(val >> 16),
            static_cast<uint8_t>(val >> 8),
            static_cast<uint8_t>(val)
        };
        return write_exact(b, 3);
    }

    MediaResult<void> write_u32_be(uint32_t val) {
        uint8_t b[4] = {
            static_cast<uint8_t>(val >> 24),
            static_cast<uint8_t>(val >> 16),
            static_cast<uint8_t>(val >> 8),
            static_cast<uint8_t>(val)
        };
        return write_exact(b, 4);
    }

    MediaResult<void> write_u32_le(uint32_t val) {
        uint8_t b[4] = {
            static_cast<uint8_t>(val),
            static_cast<uint8_t>(val >> 8),
            static_cast<uint8_t>(val >> 16),
            static_cast<uint8_t>(val >> 24)
        };
        return write_exact(b, 4);
    }

    MediaResult<void> write_u64_be(uint64_t val) {
        uint8_t b[8] = {
            static_cast<uint8_t>(val >> 56),
            static_cast<uint8_t>(val >> 48),
            static_cast<uint8_t>(val >> 40),
            static_cast<uint8_t>(val >> 32),
            static_cast<uint8_t>(val >> 24),
            static_cast<uint8_t>(val >> 16),
            static_cast<uint8_t>(val >> 8),
            static_cast<uint8_t>(val)
        };
        return write_exact(b, 8);
    }

    MediaResult<void> write_u64_le(uint64_t val) {
        uint8_t b[8] = {
            static_cast<uint8_t>(val),
            static_cast<uint8_t>(val >> 8),
            static_cast<uint8_t>(val >> 16),
            static_cast<uint8_t>(val >> 24),
            static_cast<uint8_t>(val >> 32),
            static_cast<uint8_t>(val >> 40),
            static_cast<uint8_t>(val >> 48),
            static_cast<uint8_t>(val >> 56)
        };
        return write_exact(b, 8);
    }

    MediaResult<void> write_fourcc(uint32_t val) {
        return write_u32_be(val);
    }

private:
    static bool total_read_chunk(size_t cur, size_t total) noexcept {
        return cur < total;
    }
};

} // namespace atum

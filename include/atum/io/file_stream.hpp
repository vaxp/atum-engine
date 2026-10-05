#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include "atum/io/io_stream.hpp"

namespace atum {

/// High-performance file reader with 64-bit offsets and RAII file lifecycle.
class FileReader : public IoReader {
public:
    FileReader() noexcept = default;
    ~FileReader() override;

    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    FileReader(FileReader&& other) noexcept;
    FileReader& operator=(FileReader&& other) noexcept;

    /// Opens a file from the filesystem for reading in binary mode.
    static MediaResult<FileReader> open(std::string_view path);

    MediaResult<size_t> read(uint8_t* dst, size_t size) override;
    MediaResult<int64_t> seek(int64_t offset, SeekOrigin origin) override;

    [[nodiscard]] int64_t tell() const override;
    [[nodiscard]] int64_t size() const override;
    [[nodiscard]] bool is_seekable() const noexcept override { return true; }
    [[nodiscard]] bool is_open() const noexcept { return file_ != nullptr; }

    void close() noexcept;

private:
    explicit FileReader(std::FILE* fp, int64_t file_size) noexcept
        : file_(fp), size_(file_size) {}

    std::FILE* file_{nullptr};
    int64_t size_{-1};
};

/// High-performance file writer with RAII file lifecycle.
class FileWriter : public IoWriter {
public:
    FileWriter() noexcept = default;
    ~FileWriter() override;

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    FileWriter(FileWriter&& other) noexcept;
    FileWriter& operator=(FileWriter&& other) noexcept;

    /// Opens or creates a file on disk for writing in binary mode.
    static MediaResult<FileWriter> open(std::string_view path);

    MediaResult<size_t> write(const uint8_t* src, size_t size) override;
    MediaResult<int64_t> seek(int64_t offset, SeekOrigin origin) override;

    [[nodiscard]] int64_t tell() const override;
    MediaResult<void> flush() override;

    [[nodiscard]] bool is_open() const noexcept { return file_ != nullptr; }
    void close() noexcept;

private:
    explicit FileWriter(std::FILE* fp) noexcept : file_(fp) {}

    std::FILE* file_{nullptr};
};

} // namespace atum

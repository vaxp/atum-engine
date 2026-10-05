#include "atum/io/file_stream.hpp"
#include <sys/stat.h>

#if defined(_WIN32)
    #define fseeko64 _fseeki64
    #define ftello64 _ftelli64
#else
    #define fseeko64 fseeko
    #define ftello64 ftello
#endif

namespace atum {

FileReader::~FileReader() {
    close();
}

FileReader::FileReader(FileReader&& other) noexcept
    : file_(other.file_), size_(other.size_) {
    other.file_ = nullptr;
    other.size_ = -1;
}

FileReader& FileReader::operator=(FileReader&& other) noexcept {
    if (this == &other) return *this;
    close();
    file_ = other.file_;
    size_ = other.size_;
    other.file_ = nullptr;
    other.size_ = -1;
    return *this;
}

MediaResult<FileReader> FileReader::open(std::string_view path) {
    std::string null_terminated(path);
    std::FILE* fp = std::fopen(null_terminated.c_str(), "rb");
    if (!fp) {
        return MediaError::IoError;
    }

    int64_t file_size = -1;
    if (fseeko64(fp, 0, SEEK_END) == 0) {
        file_size = ftello64(fp);
        fseeko64(fp, 0, SEEK_SET);
    }

    return FileReader(fp, file_size);
}

MediaResult<size_t> FileReader::read(uint8_t* dst, size_t size) {
    if (!file_) return MediaError::IoError;
    if (size == 0) return size_t{0};

    size_t bytes_read = std::fread(dst, 1, size, file_);
    if (bytes_read == 0) {
        if (std::feof(file_)) {
            return size_t{0};
        }
        return MediaError::IoError;
    }
    return bytes_read;
}

MediaResult<int64_t> FileReader::seek(int64_t offset, SeekOrigin origin) {
    if (!file_) return MediaError::IoError;

    int whence = SEEK_SET;
    switch (origin) {
        case SeekOrigin::Begin: whence = SEEK_SET; break;
        case SeekOrigin::Current: whence = SEEK_CUR; break;
        case SeekOrigin::End: whence = SEEK_END; break;
    }

    if (fseeko64(file_, offset, whence) != 0) {
        return MediaError::IoError;
    }

    int64_t pos = ftello64(file_);
    if (pos < 0) return MediaError::IoError;
    return pos;
}

int64_t FileReader::tell() const {
    if (!file_) return -1;
    return ftello64(file_);
}

int64_t FileReader::size() const {
    return size_;
}

void FileReader::close() noexcept {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
        size_ = -1;
    }
}

// --- FileWriter ---

FileWriter::~FileWriter() {
    close();
}

FileWriter::FileWriter(FileWriter&& other) noexcept
    : file_(other.file_) {
    other.file_ = nullptr;
}

FileWriter& FileWriter::operator=(FileWriter&& other) noexcept {
    if (this == &other) return *this;
    close();
    file_ = other.file_;
    other.file_ = nullptr;
    return *this;
}

MediaResult<FileWriter> FileWriter::open(std::string_view path) {
    std::string null_terminated(path);
    std::FILE* fp = std::fopen(null_terminated.c_str(), "wb");
    if (!fp) {
        return MediaError::IoError;
    }
    return FileWriter(fp);
}

MediaResult<size_t> FileWriter::write(const uint8_t* src, size_t size) {
    if (!file_) return MediaError::IoError;
    if (size == 0) return size_t{0};

    size_t written = std::fwrite(src, 1, size, file_);
    if (written < size) {
        return MediaError::IoError;
    }
    return written;
}

MediaResult<int64_t> FileWriter::seek(int64_t offset, SeekOrigin origin) {
    if (!file_) return MediaError::IoError;

    int whence = SEEK_SET;
    switch (origin) {
        case SeekOrigin::Begin: whence = SEEK_SET; break;
        case SeekOrigin::Current: whence = SEEK_CUR; break;
        case SeekOrigin::End: whence = SEEK_END; break;
    }

    if (fseeko64(file_, offset, whence) != 0) {
        return MediaError::IoError;
    }

    int64_t pos = ftello64(file_);
    if (pos < 0) return MediaError::IoError;
    return pos;
}

int64_t FileWriter::tell() const {
    if (!file_) return -1;
    return ftello64(file_);
}

MediaResult<void> FileWriter::flush() {
    if (!file_) return MediaError::IoError;
    if (std::fflush(file_) != 0) {
        return MediaError::IoError;
    }
    return MediaResult<void>::ok();
}

void FileWriter::close() noexcept {
    if (file_) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
}

} // namespace atum

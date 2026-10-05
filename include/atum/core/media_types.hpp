#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <numeric>
#include <string_view>
#include <utility>
#include <optional>

namespace atum {

/// Sentinel value indicating an invalid or undefined presentation/decompression timestamp.
inline constexpr int64_t NOPTS = static_cast<int64_t>(0x8000000000000000ULL);

/// Primary error codes for the Nisaba Media subsystem.
enum class MediaError : int32_t {
    Ok = 0,
    EndOfStream = -1,
    InvalidData = -2,
    NeedMoreData = -3,
    UnsupportedCodec = -4,
    UnsupportedFormat = -5,
    BufferTooSmall = -6,
    IoError = -7,
    AllocationFailed = -8,
    HardwareError = -9,
    NotImplemented = -10,
    InvalidState = -11,
    CodecNotFound = -12,
    DemuxerNotFound = -13,
};

/// Returns a human-readable description of a MediaError.
constexpr std::string_view media_error_to_string(MediaError err) noexcept {
    switch (err) {
        case MediaError::Ok: return "Success";
        case MediaError::EndOfStream: return "End of stream reached";
        case MediaError::InvalidData: return "Invalid or corrupt bitstream/container data";
        case MediaError::NeedMoreData: return "More input data required to complete operation";
        case MediaError::UnsupportedCodec: return "Codec format is not supported";
        case MediaError::UnsupportedFormat: return "Pixel or audio sample format is unsupported";
        case MediaError::BufferTooSmall: return "Destination buffer is too small";
        case MediaError::IoError: return "I/O read/write error";
        case MediaError::AllocationFailed: return "Memory allocation failed";
        case MediaError::HardwareError: return "Hardware acceleration device error";
        case MediaError::NotImplemented: return "Feature or operation not implemented";
        case MediaError::InvalidState: return "Engine or codec is in an invalid state";
        case MediaError::CodecNotFound: return "Requested codec was not found in registry";
        case MediaError::DemuxerNotFound: return "Suitable container demuxer was not found";
        default: return "Unknown media error";
    }
}

/// Media stream elementary classification.
enum class MediaType : uint8_t {
    Unknown = 0,
    Video,
    Audio,
    Subtitle,
    Data,
    Attachment
};

constexpr std::string_view media_type_to_string(MediaType type) noexcept {
    switch (type) {
        case MediaType::Video: return "Video";
        case MediaType::Audio: return "Audio";
        case MediaType::Subtitle: return "Subtitle";
        case MediaType::Data: return "Data";
        case MediaType::Attachment: return "Attachment";
        default: return "Unknown";
    }
}

/// Rational number (num / den) representing timebases, frame rates, and sample aspect ratios.
struct Rational {
    int32_t num{0};
    int32_t den{1};

    constexpr Rational() noexcept = default;
    constexpr Rational(int32_t n, int32_t d) noexcept : num(n), den(d == 0 ? 1 : d) {
        reduce();
    }

    constexpr void reduce() noexcept {
        if (den < 0) {
            num = -num;
            den = -den;
        }
        if (den == 0) {
            den = 1;
            return;
        }
        int32_t g = std::gcd(num, den);
        if (g > 1) {
            num /= g;
            den /= g;
        }
    }

    [[nodiscard]] constexpr double to_double() const noexcept {
        return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
    }

    [[nodiscard]] constexpr float to_float() const noexcept {
        return den == 0 ? 0.0f : static_cast<float>(num) / static_cast<float>(den);
    }

    [[nodiscard]] constexpr Rational invert() const noexcept {
        return Rational(den, num);
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return den > 0;
    }

    constexpr bool operator==(const Rational& other) const noexcept {
        return static_cast<int64_t>(num) * other.den == static_cast<int64_t>(other.num) * den;
    }

    constexpr bool operator!=(const Rational& other) const noexcept {
        return !(*this == other);
    }

    constexpr Rational operator*(const Rational& other) const noexcept {
        return Rational(num * other.num, den * other.den);
    }

    constexpr Rational operator/(const Rational& other) const noexcept {
        return Rational(num * other.den, den * other.num);
    }

    /// Rescales a value in units of `src_base` to units of `dst_base` using 64-bit integer arithmetic with rounding.
    static constexpr int64_t rescale(int64_t val, Rational src_base, Rational dst_base) noexcept {
        if (val == NOPTS) return NOPTS;
        if (src_base == dst_base) return val;

        // target = val * (src.num / src.den) / (dst.num / dst.den)
        //        = val * (src.num * dst.den) / (src.den * dst.num)
        int64_t b = static_cast<int64_t>(src_base.num) * dst_base.den;
        int64_t c = static_cast<int64_t>(src_base.den) * dst_base.num;
        if (c == 0) return 0;

        // Integer division with rounding to nearest
        int64_t r = c / 2;
        if (val >= 0) {
            return (val * b + r) / c;
        } else {
            return (val * b - r) / c;
        }
    }

    /// Converts timestamp in rational timebase units to fractional seconds.
    static constexpr double to_seconds(int64_t pts, Rational timebase) noexcept {
        if (pts == NOPTS) return 0.0;
        return static_cast<double>(pts) * timebase.to_double();
    }

    /// Converts fractional seconds into timestamp units for a given timebase.
    static constexpr int64_t from_seconds(double seconds, Rational timebase) noexcept {
        if (std::isnan(seconds) || std::isinf(seconds)) return NOPTS;
        return static_cast<int64_t>(std::round(seconds / timebase.to_double()));
    }
};

/// Rescales a timestamp from one timebase to another using 64-bit integer arithmetic with rounding.
inline constexpr int64_t rescale_timestamp(int64_t val, Rational src_base, Rational dst_base) noexcept {
    return Rational::rescale(val, src_base, dst_base);
}

/// Sovereign result container holding either a value T or a MediaError.
template <typename T>
class MediaResult {
public:
    constexpr MediaResult(T val) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_(std::move(val)), error_(MediaError::Ok), has_value_(true) {}

    constexpr MediaResult(MediaError err) noexcept
        : error_(err), has_value_(false) {}

    [[nodiscard]] constexpr bool is_ok() const noexcept { return has_value_ && error_ == MediaError::Ok; }
    [[nodiscard]] constexpr bool is_err() const noexcept { return !is_ok(); }
    [[nodiscard]] constexpr MediaError error() const noexcept { return error_; }

    constexpr const T& value() const & { return value_; }
    constexpr T& value() & { return value_; }
    constexpr T&& value() && { return std::move(value_); }

    constexpr const T* operator->() const noexcept { return &value_; }
    constexpr T* operator->() noexcept { return &value_; }

    constexpr const T& operator*() const & noexcept { return value_; }
    constexpr T& operator*() & noexcept { return value_; }

    constexpr explicit operator bool() const noexcept { return is_ok(); }

private:
    T value_{};
    MediaError error_{MediaError::Ok};
    bool has_value_{false};
};

/// Specialization for void results.
template <>
class MediaResult<void> {
public:
    constexpr MediaResult() noexcept : error_(MediaError::Ok) {}
    constexpr MediaResult(MediaError err) noexcept : error_(err) {}

    [[nodiscard]] constexpr bool is_ok() const noexcept { return error_ == MediaError::Ok; }
    [[nodiscard]] constexpr bool is_err() const noexcept { return !is_ok(); }
    [[nodiscard]] constexpr MediaError error() const noexcept { return error_; }

    constexpr explicit operator bool() const noexcept { return is_ok(); }

    static constexpr MediaResult<void> ok() noexcept { return MediaResult<void>(); }

private:
    MediaError error_{MediaError::Ok};
};

} // namespace atum

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include <memory>
#include "atum/core/media_types.hpp"

namespace atum {

/// Packet attribute flags.
enum class PacketFlags : uint32_t {
    None = 0,
    KeyFrame = (1 << 0),    ///< Packet contains a keyframe / IDR / sync frame.
    Corrupt = (1 << 1),     ///< Packet payload is marked as corrupted.
    Discard = (1 << 2),     ///< Packet should be decoded for state only, not displayed.
};

constexpr PacketFlags operator|(PacketFlags a, PacketFlags b) noexcept {
    return static_cast<PacketFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr PacketFlags operator&(PacketFlags a, PacketFlags b) noexcept {
    return static_cast<PacketFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// An encoded compressed media packet produced by a demuxer or encoder.
class Packet {
public:
    Packet() noexcept = default;
    ~Packet() = default;

    Packet(const Packet& other);
    Packet& operator=(const Packet& other);

    Packet(Packet&& other) noexcept = default;
    Packet& operator=(Packet&& other) noexcept = default;

    /// Allocates an owned packet with pre-allocated payload capacity.
    static Packet allocate(size_t size);

    /// Wraps an existing memory buffer without copying (borrowed view).
    static Packet from_borrowed(const uint8_t* data, size_t size) noexcept;

    /// Creates an owned packet copying from an external byte span.
    static Packet from_bytes(std::span<const uint8_t> bytes);

    [[nodiscard]] const uint8_t* data() const noexcept {
        return is_owned_ ? owned_data_.data() : borrowed_data_;
    }

    [[nodiscard]] uint8_t* data_mut() noexcept {
        ensure_owned();
        return owned_data_.data();
    }

    [[nodiscard]] size_t size() const noexcept {
        return is_owned_ ? owned_data_.size() : borrowed_size_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    [[nodiscard]] std::span<const uint8_t> as_span() const noexcept {
        return {data(), size()};
    }

    void resize(size_t new_size);
    void ensure_owned();

    // Timing and Metadata
    [[nodiscard]] int64_t pts() const noexcept { return pts_; }
    void set_pts(int64_t pts) noexcept { pts_ = pts; }

    [[nodiscard]] int64_t dts() const noexcept { return dts_; }
    void set_dts(int64_t dts) noexcept { dts_ = dts; }

    [[nodiscard]] int64_t duration() const noexcept { return duration_; }
    void set_duration(int64_t dur) noexcept { duration_ = dur; }

    [[nodiscard]] int32_t stream_index() const noexcept { return stream_index_; }
    void set_stream_index(int32_t idx) noexcept { stream_index_ = idx; }

    [[nodiscard]] PacketFlags flags() const noexcept { return flags_; }
    void set_flags(PacketFlags f) noexcept { flags_ = f; }

    [[nodiscard]] bool is_keyframe() const noexcept {
        return (flags_ & PacketFlags::KeyFrame) != PacketFlags::None;
    }

    void set_keyframe(bool is_key) noexcept {
        if (is_key) {
            flags_ = flags_ | PacketFlags::KeyFrame;
        } else {
            flags_ = static_cast<PacketFlags>(static_cast<uint32_t>(flags_) & ~static_cast<uint32_t>(PacketFlags::KeyFrame));
        }
    }

    void clear() noexcept;

private:
    std::vector<uint8_t> owned_data_;
    const uint8_t* borrowed_data_{nullptr};
    size_t borrowed_size_{0};
    bool is_owned_{false};

    int64_t pts_{NOPTS};
    int64_t dts_{NOPTS};
    int64_t duration_{0};
    int32_t stream_index_{-1};
    PacketFlags flags_{PacketFlags::None};
};

} // namespace atum

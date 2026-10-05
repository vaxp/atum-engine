#pragma once

#include <cstdint>
#include <cstddef>
#include "atum/core/media_types.hpp"

namespace atum {

/// Base uncompressed media frame providing common timing and classification metadata.
class Frame {
public:
    virtual ~Frame() = default;

    [[nodiscard]] MediaType media_type() const noexcept { return media_type_; }
    [[nodiscard]] int64_t pts() const noexcept { return pts_; }
    void set_pts(int64_t pts) noexcept { pts_ = pts; }

    [[nodiscard]] int64_t dts() const noexcept { return dts_; }
    void set_dts(int64_t dts) noexcept { dts_ = dts; }

    [[nodiscard]] int64_t duration() const noexcept { return duration_; }
    void set_duration(int64_t dur) noexcept { duration_ = dur; }

    [[nodiscard]] Rational timebase() const noexcept { return timebase_; }
    void set_timebase(Rational tb) noexcept { timebase_ = tb; }

    [[nodiscard]] bool is_keyframe() const noexcept { return is_keyframe_; }
    void set_keyframe(bool key) noexcept { is_keyframe_ = key; }

    [[nodiscard]] virtual bool is_valid() const noexcept = 0;

protected:
    constexpr explicit Frame(MediaType type) noexcept : media_type_(type) {}

    MediaType media_type_{MediaType::Unknown};
    int64_t pts_{NOPTS};
    int64_t dts_{NOPTS};
    int64_t duration_{0};
    Rational timebase_{1, 1000};
    bool is_keyframe_{false};
};

} // namespace atum

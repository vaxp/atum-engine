#pragma once

#include <cstdint>
#include <string_view>
#include "atum/core/media_types.hpp"

namespace atum {

/// VP9 Profile.
enum class Vp9Profile : uint8_t {
    Profile0 = 0, ///< 8-bit 4:2:0
    Profile1 = 1, ///< 8-bit 4:2:2 / 4:4:4
    Profile2 = 2, ///< 10/12-bit 4:2:0
    Profile3 = 3, ///< 10/12-bit 4:2:2 / 4:4:4
};

constexpr std::string_view vp9_profile_to_string(Vp9Profile profile) noexcept {
    switch (profile) {
        case Vp9Profile::Profile0: return "VP9 Profile 0 (8-bit 4:2:0)";
        case Vp9Profile::Profile1: return "VP9 Profile 1 (8-bit 4:2:2/4:4:4)";
        case Vp9Profile::Profile2: return "VP9 Profile 2 (10/12-bit 4:2:0)";
        case Vp9Profile::Profile3: return "VP9 Profile 3 (10/12-bit 4:2:2/4:4:4)";
        default: return "Unknown";
    }
}

/// VP9 Frame Type.
enum class Vp9FrameType : uint8_t {
    KeyFrame = 0,
    InterFrame = 1,
};

/// VP9 Color Space enumeration.
enum class Vp9ColorSpace : uint8_t {
    Unknown = 0,
    BT601 = 1,
    BT709 = 2,
    SMPTE170 = 3,
    SMPTE240 = 4,
    BT2020 = 5,
    Reserved = 6,
    SRGB = 7,
};

/// Standard 3-byte VP9 sync code: 0x49, 0x83, 0x42.
inline constexpr uint8_t VP9_SYNC_CODE[3] = {0x49, 0x83, 0x42};

} // namespace atum

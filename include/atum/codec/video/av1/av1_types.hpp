#pragma once

#include <cstdint>
#include <string_view>
#include "atum/core/media_types.hpp"

namespace atum {

/// AV1 Open Bitstream Unit (OBU) Types (Section 6.2.2).
enum class Av1ObuType : uint8_t {
    SequenceHeader = 1,
    TemporalDelimiter = 2,
    FrameHeader = 3,
    TileGroup = 4,
    Metadata = 5,
    Frame = 6,
    RedundantFrameHeader = 7,
    TileList = 8,
    Padding = 15,
};

constexpr std::string_view av1_obu_type_to_string(Av1ObuType type) noexcept {
    switch (type) {
        case Av1ObuType::SequenceHeader: return "Sequence Header";
        case Av1ObuType::TemporalDelimiter: return "Temporal Delimiter";
        case Av1ObuType::FrameHeader: return "Frame Header";
        case Av1ObuType::TileGroup: return "Tile Group";
        case Av1ObuType::Metadata: return "Metadata";
        case Av1ObuType::Frame: return "Frame";
        case Av1ObuType::RedundantFrameHeader: return "Redundant Frame Header";
        case Av1ObuType::TileList: return "Tile List";
        case Av1ObuType::Padding: return "Padding";
        default: return "Reserved";
    }
}

/// AV1 Frame Types (Section 6.8.2).
enum class Av1FrameType : uint8_t {
    KeyFrame = 0,
    InterFrame = 1,
    IntraOnlyFrame = 2,
    SwitchFrame = 3,
};

constexpr std::string_view av1_frame_type_to_string(Av1FrameType type) noexcept {
    switch (type) {
        case Av1FrameType::KeyFrame: return "Key Frame";
        case Av1FrameType::InterFrame: return "Inter Frame";
        case Av1FrameType::IntraOnlyFrame: return "Intra-only Frame";
        case Av1FrameType::SwitchFrame: return "Switch Frame";
        default: return "Unknown";
    }
}

/// AV1 Profiles (Section A.2).
enum class Av1Profile : uint8_t {
    Main = 0,         ///< 8 or 10-bit 4:2:0
    High = 1,         ///< 8 or 10-bit 4:4:4
    Professional = 2, ///< 8, 10 or 12-bit 4:2:2 or 12-bit 4:2:0/4:4:4
};

constexpr std::string_view av1_profile_to_string(Av1Profile profile) noexcept {
    switch (profile) {
        case Av1Profile::Main: return "AV1 Main Profile";
        case Av1Profile::High: return "AV1 High Profile";
        case Av1Profile::Professional: return "AV1 Professional Profile";
        default: return "Unknown Profile";
    }
}

} // namespace atum

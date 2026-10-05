#pragma once

#include <cstdint>
#include <string_view>
#include "atum/core/media_types.hpp"
#include "atum/bitstream/nal_unit.hpp"

namespace atum {

/// H.264 / AVC Profiles (ISO/IEC 14496-10).
enum class H264Profile : uint8_t {
    Unknown = 0,
    Baseline = 66,
    Main = 77,
    Extended = 88,
    High = 100,
    High10 = 110,
    High422 = 122,
    High444 = 244,
};

constexpr std::string_view h264_profile_to_string(H264Profile profile) noexcept {
    switch (profile) {
        case H264Profile::Baseline: return "Baseline";
        case H264Profile::Main: return "Main";
        case H264Profile::Extended: return "Extended";
        case H264Profile::High: return "High";
        case H264Profile::High10: return "High 10";
        case H264Profile::High422: return "High 4:2:2";
        case H264Profile::High444: return "High 4:4:4";
        default: return "Unknown";
    }
}

/// H.264 Slice Types according to ISO/IEC 14496-10 Table 7-6.
enum class H264SliceType : uint8_t {
    P = 0,
    B = 1,
    I = 2,
    SP = 3,
    SI = 4,
};

constexpr std::string_view h264_slice_type_to_string(H264SliceType type) noexcept {
    switch (type) {
        case H264SliceType::P: return "P-Slice";
        case H264SliceType::B: return "B-Slice";
        case H264SliceType::I: return "I-Slice";
        case H264SliceType::SP: return "SP-Slice";
        case H264SliceType::SI: return "SI-Slice";
        default: return "Unknown-Slice";
    }
}

/// Normalizes raw slice_type (0..9) to standard H264SliceType (0..4).
constexpr H264SliceType normalize_h264_slice_type(uint32_t raw_type) noexcept {
    return static_cast<H264SliceType>(raw_type % 5);
}

/// H.264 Chroma Formats.
enum class H264ChromaFormat : uint8_t {
    Monochrome = 0,
    YUV420 = 1,
    YUV422 = 2,
    YUV444 = 3,
};

} // namespace atum

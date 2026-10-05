#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <string_view>
#include "atum/core/media_types.hpp"

namespace atum {

/// H.264 / AVC NAL Unit Types.
enum class H264NalType : uint8_t {
    Unspecified = 0,
    SliceNonIdr = 1,
    SliceDataA = 2,
    SliceDataB = 3,
    SliceDataC = 4,
    SliceIdr = 5,       ///< IDR Keyframe slice
    Sei = 6,            ///< Supplemental Enhancement Information
    Sps = 7,            ///< Sequence Parameter Set
    Pps = 8,            ///< Picture Parameter Set
    Aud = 9,            ///< Access Unit Delimiter
    EndOfSeq = 10,
    EndOfStream = 11,
    FilterData = 12,
};

constexpr std::string_view h264_nal_type_to_string(H264NalType type) noexcept {
    switch (type) {
        case H264NalType::SliceNonIdr: return "Slice Non-IDR";
        case H264NalType::SliceIdr: return "IDR Keyframe";
        case H264NalType::Sei: return "SEI";
        case H264NalType::Sps: return "SPS";
        case H264NalType::Pps: return "PPS";
        case H264NalType::Aud: return "AUD";
        default: return "Other/Reserved";
    }
}

/// Extracted NAL Unit view.
struct NalUnit {
    uint8_t forbidden_zero_bit{0};
    uint8_t ref_idc{0};
    uint8_t type{0};
    std::span<const uint8_t> payload;   ///< Raw NAL payload including header or stripped RBSP
    size_t start_code_size{0};          ///< 3 or 4 bytes
};

/// High-performance NAL parser and RBSP unescape processor.
class NalParser {
public:
    /// Searches for standard Annex-B start codes (0x000001 or 0x00000001).
    /// Returns offset of the start code, or size if not found.
    static size_t find_next_start_code(const uint8_t* data, size_t size, size_t& out_start_code_len) noexcept;

    /// Splits an Annex-B bitstream into individual NAL units.
    static std::vector<NalUnit> split_annex_b(std::span<const uint8_t> bitstream);

    /// Converts EBSP (Encapsulated Byte Sequence Payload) to RBSP (Raw Byte Sequence Payload)
    /// by removing 0x03 emulation prevention bytes.
    static std::vector<uint8_t> ebsp_to_rbsp(std::span<const uint8_t> ebsp);

    /// Converts RBSP to EBSP by injecting 0x03 emulation prevention bytes where needed.
    static std::vector<uint8_t> rbsp_to_ebsp(std::span<const uint8_t> rbsp);
};

} // namespace atum

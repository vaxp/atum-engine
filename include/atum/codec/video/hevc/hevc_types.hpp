#pragma once

#include <cstdint>
#include <string_view>
#include <span>
#include "atum/core/media_types.hpp"

namespace atum {

/// HEVC / H.265 NAL Unit Types (ISO/IEC 23008-2 Table 7-1).
enum class HevcNalType : uint8_t {
    TRAIL_N = 0,
    TRAIL_R = 1,
    TSA_N = 2,
    TSA_R = 3,
    STSA_N = 4,
    STSA_R = 5,
    RADL_N = 6,
    RADL_R = 7,
    RASL_N = 8,
    RASL_R = 9,
    RSV_VCL_N10 = 10,
    RSV_VCL_R11 = 11,
    RSV_VCL_N12 = 12,
    RSV_VCL_R13 = 13,
    RSV_VCL_N14 = 14,
    RSV_VCL_R15 = 15,
    BLA_W_LP = 16,
    BLA_W_RADL = 17,
    BLA_N_LP = 18,
    IDR_W_RADL = 19,
    IDR_N_LP = 20,
    CRA_NUT = 21,
    RSV_IRAP_VCL22 = 22,
    RSV_IRAP_VCL23 = 23,
    VPS_NUT = 32,
    SPS_NUT = 33,
    PPS_NUT = 34,
    AUD_NUT = 35,
    EOS_NUT = 36,
    EOB_NUT = 37,
    FD_NUT = 38,
    PREFIX_SEI_NUT = 39,
    SUFFIX_SEI_NUT = 40,
};

constexpr bool hevc_is_vcl_nal(HevcNalType type) noexcept {
    return static_cast<uint8_t>(type) <= 31;
}

constexpr bool hevc_is_irap_nal(HevcNalType type) noexcept {
    auto val = static_cast<uint8_t>(type);
    return val >= 16 && val <= 23;
}

constexpr std::string_view hevc_nal_type_to_string(HevcNalType type) noexcept {
    switch (type) {
        case HevcNalType::TRAIL_N: return "TRAIL_N";
        case HevcNalType::TRAIL_R: return "TRAIL_R";
        case HevcNalType::IDR_W_RADL: return "IDR_W_RADL";
        case HevcNalType::IDR_N_LP: return "IDR_N_LP";
        case HevcNalType::CRA_NUT: return "CRA_NUT";
        case HevcNalType::VPS_NUT: return "VPS";
        case HevcNalType::SPS_NUT: return "SPS";
        case HevcNalType::PPS_NUT: return "PPS";
        case HevcNalType::AUD_NUT: return "AUD";
        case HevcNalType::PREFIX_SEI_NUT: return "PREFIX_SEI";
        case HevcNalType::SUFFIX_SEI_NUT: return "SUFFIX_SEI";
        default: return "Other/Reserved";
    }
}

/// HEVC Slice Types according to ISO/IEC 23008-2 Table 7-7.
enum class HevcSliceType : uint8_t {
    B = 0,
    P = 1,
    I = 2,
};

constexpr std::string_view hevc_slice_type_to_string(HevcSliceType type) noexcept {
    switch (type) {
        case HevcSliceType::B: return "B-Slice";
        case HevcSliceType::P: return "P-Slice";
        case HevcSliceType::I: return "I-Slice";
        default: return "Unknown-Slice";
    }
}

/// HEVC 2-byte NAL Unit Header.
struct HevcNalHeader {
    bool forbidden_zero_bit{false};
    HevcNalType nal_unit_type{HevcNalType::TRAIL_N};
    uint8_t nuh_layer_id{0};
    uint8_t nuh_temporal_id_plus1{0};

    static MediaResult<HevcNalHeader> parse(std::span<const uint8_t> data) noexcept {
        if (data.size() < 2) return MediaError::InvalidData;
        HevcNalHeader hdr{};
        hdr.forbidden_zero_bit = ((data[0] >> 7) & 1) != 0;
        hdr.nal_unit_type = static_cast<HevcNalType>((data[0] >> 1) & 0x3F);
        hdr.nuh_layer_id = static_cast<uint8_t>(((data[0] & 1) << 5) | ((data[1] >> 3) & 0x1F));
        hdr.nuh_temporal_id_plus1 = static_cast<uint8_t>(data[1] & 0x07);
        return hdr;
    }
};

} // namespace atum

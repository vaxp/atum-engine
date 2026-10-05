#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <span>
#include "atum/color/hdr_metadata.hpp"

namespace atum {

/// Supplemental Enhancement Information (SEI) & Metadata OBU parser
/// for extracting broadcast and theatrical HDR mastering metadata (MDCV)
/// and content light levels (CLL / FALL).
class SeiParser {
public:
    static constexpr uint32_t SEI_TYPE_BUFFERING_PERIOD = 0;
    static constexpr uint32_t SEI_TYPE_PICTURE_TIMING   = 1;
    static constexpr uint32_t SEI_TYPE_USER_DATA_UNREG  = 5;
    static constexpr uint32_t SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME = 137;
    static constexpr uint32_t SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO        = 144;

    static constexpr uint32_t AV1_METADATA_TYPE_HDR_CLL  = 1;
    static constexpr uint32_t AV1_METADATA_TYPE_HDR_MDCV = 2;

    /// Removes emulation prevention bytes (0x00 0x00 0x03 -> 0x00 0x00) from NAL unit EBSP
    [[nodiscard]] static std::vector<uint8_t> unescape_ebsp(const uint8_t* data, size_t size);

    /// Parses H.264 or HEVC SEI NAL unit (handles EBSP unescaping and multiple SEI messages)
    static bool parse_h264_h265_sei(const uint8_t* data, size_t size, HdrMetadata& out_meta);

    /// Parses AV1 Metadata OBU (OBU type 5)
    static bool parse_av1_metadata_obu(const uint8_t* data, size_t size, HdrMetadata& out_meta);

    /// Parses Mastering Display Colour Volume payload (Type 137 / MDCV)
    static bool parse_mdcv(const uint8_t* payload, size_t size, MasteringDisplayMetadata& out_mdcv) noexcept;

    /// Parses Content Light Level payload (Type 144 / CLL)
    static bool parse_cll(const uint8_t* payload, size_t size, ContentLightLevel& out_cll) noexcept;
};

} // namespace atum

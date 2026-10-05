#pragma once

#include <cstdint>
#include <string_view>
#include "atum/surface/pixel_format.hpp"

namespace atum {

/// Color primaries indicating chromaticity coordinates (ITU-T H.265 / H.264 / AV1).
enum class ColorPrimaries : uint8_t {
    Unknown = 0,
    BT709 = 1,       ///< ITU-R BT.709-6, sRGB, HDTV
    Unspecified = 2,
    BT470M = 4,      ///< NTSC 1953
    BT470BG = 5,     ///< PAL / SECAM 625-line
    SMPTE170M = 6,   ///< NTSC 525-line (BT.601)
    SMPTE240M = 7,
    Film = 8,        ///< Generic film (illuminant C)
    BT2020 = 9,      ///< ITU-R BT.2020 / BT.2100 (UHDTV Wide Color Gamut)
    SMPTE428 = 10,   ///< CIE 1931 XYZ
    DCI_P3 = 11,     ///< SMPTE RP 431-2 (DCI-P3 theatrical)
    Display_P3 = 12, ///< SMPTE EG 432-1 (Display P3, D65 white point)
};

/// Transfer characteristics (OETF / EOTF standard).
enum class TransferCharacteristic : uint8_t {
    Unknown = 0,
    BT709 = 1,       ///< ITU-R BT.709
    Unspecified = 2,
    Gamma22 = 4,     ///< Assumed display gamma 2.2
    Gamma28 = 5,     ///< Assumed display gamma 2.8
    SMPTE170M = 6,   ///< BT.601
    SMPTE240M = 7,
    Linear = 8,      ///< Linear transfer $E = L$
    Log100 = 9,      ///< Logarithmic (100:1 range)
    Log316 = 10,     ///< Logarithmic (316:1 range)
    IEC61966_2_4 = 11,
    BT1361 = 12,
    IEC61966_2_1 = 13, ///< sRGB
    BT2020_10 = 14,  ///< ITU-R BT.2020 10-bit
    BT2020_12 = 15,  ///< ITU-R BT.2020 12-bit
    SMPTE2084 = 16,  ///< SMPTE ST 2084 (Perceptual Quantizer - PQ / HDR10)
    SMPTE428 = 17,
    ARIB_STD_B67 = 18, ///< ARIB STD-B67 (Hybrid Log-Gamma - HLG / BT.2100)
};

/// Matrix coefficients defining YUV <-> RGB relationship.
enum class MatrixCoefficients : uint8_t {
    Identity = 0,    ///< GBR (often used for RGB planes)
    BT709 = 1,       ///< ITU-R BT.709
    Unspecified = 2,
    FCC = 4,
    BT470BG = 5,     ///< BT.601 PAL
    SMPTE170M = 6,   ///< BT.601 NTSC
    SMPTE240M = 7,
    YCgCo = 8,
    BT2020_NCL = 9,  ///< ITU-R BT.2020 non-constant luminance
    BT2020_CL = 10,  ///< ITU-R BT.2020 constant luminance
    SMPTE2085 = 11,
    ChromaDerived_NCL = 12,
    ChromaDerived_CL = 13,
    ICtCp = 14,      ///< ITU-R BT.2100-0 ICtCp
};

/// Mastering Display Color Volume (SEI message type 137 / AV1 MDCV).
struct MasteringDisplayMetadata {
    // CIE 1931 xy coordinates normalized to [0, 1] (bitstream usually encodes scaled by 50000)
    float display_primaries[3][2]{{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}; // R, G, B
    float white_point[2]{0.0f, 0.0f};                                          // White (x, y)
    float max_luminance{1000.0f};                                              // cd/m^2 (nits)
    float min_luminance{0.0001f};                                              // cd/m^2 (nits)
    bool has_mastering_display{false};
};

/// Content Light Level Information (SEI message type 144 / AV1 CLL).
struct ContentLightLevel {
    uint16_t max_content_light_level{1000};      ///< MaxCLL in cd/m^2 (nits)
    uint16_t max_pic_average_light_level{400};   ///< MaxFALL in cd/m^2 (nits)
    bool has_cll{false};
};

/// Complete High Dynamic Range (HDR) description for a video frame.
struct HdrMetadata {
    ColorPrimaries primaries{ColorPrimaries::BT709};
    TransferCharacteristic transfer{TransferCharacteristic::BT709};
    MatrixCoefficients matrix{MatrixCoefficients::BT709};
    ColorRange range{ColorRange::Studio};

    MasteringDisplayMetadata mastering_display;
    ContentLightLevel light_level;

    [[nodiscard]] bool is_hdr() const noexcept {
        return transfer == TransferCharacteristic::SMPTE2084 ||
               transfer == TransferCharacteristic::ARIB_STD_B67 ||
               mastering_display.has_mastering_display ||
               light_level.has_cll;
    }
};

} // namespace atum

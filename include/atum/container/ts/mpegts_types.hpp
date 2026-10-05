#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include "atum/core/media_types.hpp"

namespace atum {

inline constexpr size_t TS_PACKET_SIZE = 188;
inline constexpr uint8_t TS_SYNC_BYTE = 0x47;

inline constexpr uint16_t PID_PAT = 0x0000;
inline constexpr uint16_t PID_NULL = 0x1FFF;

/// Canonical MPEG-TS Stream Types (ISO/IEC 13818-1 Table 2-34).
enum class TsStreamType : uint8_t {
    Mpeg1Video    = 0x01,
    Mpeg2Video    = 0x02,
    Mpeg1Audio    = 0x03, // MP3
    Mpeg2Audio    = 0x04,
    AacAdts       = 0x0F, // ISO/IEC 13818-7 AAC with ADTS
    Mpeg4Visual   = 0x10,
    H264          = 0x1B, // AVC / ISO/IEC 14496-10
    HEVC          = 0x24, // H.265 / ISO/IEC 23008-2
    Ac3           = 0x81, // Dolby Digital / ATSC A/52
    Eac3          = 0x87, // Enhanced AC-3
    Opus          = 0x90, // Opus in TS
    Unknown       = 0xFF
};

/// Decoded TS Packet Header.
struct TsHeader {
    bool transport_error{false};
    bool payload_unit_start{false};
    bool transport_priority{false};
    uint16_t pid{0};
    uint8_t scrambling_control{0};
    uint8_t adaptation_field_control{0}; // 1=payload, 2=adapt, 3=both
    uint8_t continuity_counter{0};

    // Adaptation Field fields
    bool has_adaptation{false};
    uint8_t adaptation_length{0};
    bool discontinuity_indicator{false};
    bool random_access_indicator{false}; // Keyframe hint
    bool has_pcr{false};
    uint64_t pcr_27mhz{0};
    size_t payload_offset{4};
    size_t payload_size{184};
};

/// Program Elementary Stream descriptor in PMT.
struct TsProgramStream {
    uint16_t pid{0};
    TsStreamType stream_type{TsStreamType::Unknown};
    int32_t stream_index{-1};
    std::vector<uint8_t> descriptors;
};

/// Program descriptor in PMT.
struct TsProgram {
    uint16_t program_number{0};
    uint16_t pmt_pid{0};
    uint16_t pcr_pid{0};
    std::vector<TsProgramStream> streams;
};

/// Computes standard MPEG-2 CRC-32 (polynomial 0x04C11DB7, init 0xFFFFFFFF).
class TsCrc32 {
public:
    static uint32_t calculate(std::span<const uint8_t> data) noexcept {
        uint32_t crc = 0xFFFFFFFF;
        for (uint8_t byte : data) {
            for (int i = 7; i >= 0; --i) {
                bool bit = ((byte >> i) & 1) ^ ((crc >> 31) & 1);
                crc <<= 1;
                if (bit) {
                    crc ^= 0x04C11DB7;
                }
            }
        }
        return crc;
    }
};

} // namespace atum

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/io/io_stream.hpp"

namespace atum {

/// Constant indicating unknown / undefined EBML element size.
inline constexpr uint64_t EBML_UNKNOWN_SIZE = 0xFFFFFFFFFFFFFFFFULL;

/// Canonical EBML Element Identifiers (RFC 8794 & Matroska / WebM Specifications).
namespace ebml_id {
    // Top-Level / Header Elements
    inline constexpr uint32_t EBML                = 0x1A45DFA3;
    inline constexpr uint32_t EBMLVersion         = 0x4286;
    inline constexpr uint32_t EBMLReadVersion     = 0x42F7;
    inline constexpr uint32_t EBMLMaxIDLength     = 0x42F2;
    inline constexpr uint32_t EBMLMaxSizeLength   = 0x42F3;
    inline constexpr uint32_t DocType             = 0x4282;
    inline constexpr uint32_t DocTypeVersion      = 0x4287;
    inline constexpr uint32_t DocTypeReadVersion  = 0x4285;

    // Segment & Global Hierarchy
    inline constexpr uint32_t Segment             = 0x18538067;
    inline constexpr uint32_t SeekHead            = 0x114D9B74;
    inline constexpr uint32_t Seek                = 0x4DBB;
    inline constexpr uint32_t SeekID              = 0x53AB;
    inline constexpr uint32_t SeekPosition        = 0x53AC;

    // Info Elements
    inline constexpr uint32_t Info                = 0x1549A966;
    inline constexpr uint32_t TimestampScale      = 0x2AD7B1; // TimecodeScale (ns per tick, default 1000000)
    inline constexpr uint32_t Duration            = 0x4489;
    inline constexpr uint32_t MuxingApp           = 0x4D80;
    inline constexpr uint32_t WritingApp          = 0x5741;
    inline constexpr uint32_t DateUTC             = 0x4461;

    // Track Elements
    inline constexpr uint32_t Tracks              = 0x1654AE6B;
    inline constexpr uint32_t TrackEntry          = 0xAE;
    inline constexpr uint32_t TrackNumber         = 0xD7;
    inline constexpr uint32_t TrackUID            = 0x73C5;
    inline constexpr uint32_t TrackType           = 0x83;     // 1=Video, 2=Audio, 17=Subtitle
    inline constexpr uint32_t FlagEnabled         = 0xB9;
    inline constexpr uint32_t FlagDefault         = 0x88;
    inline constexpr uint32_t FlagForced          = 0x55AA;
    inline constexpr uint32_t FlagLacing          = 0x9C;
    inline constexpr uint32_t DefaultDuration     = 0x23E383; // Nanoseconds per frame
    inline constexpr uint32_t CodecID             = 0x86;
    inline constexpr uint32_t CodecPrivate        = 0x63A2;
    inline constexpr uint32_t CodecName           = 0x258688;

    // Video Track Elements
    inline constexpr uint32_t Video               = 0xE0;
    inline constexpr uint32_t FlagInterlaced      = 0x9A;
    inline constexpr uint32_t PixelWidth          = 0xB0;
    inline constexpr uint32_t PixelHeight         = 0xBA;
    inline constexpr uint32_t DisplayWidth        = 0x54B0;
    inline constexpr uint32_t DisplayHeight       = 0x54BA;

    // Audio Track Elements
    inline constexpr uint32_t Audio               = 0xE1;
    inline constexpr uint32_t SamplingFrequency   = 0xB5;
    inline constexpr uint32_t OutputSamplingFrequency = 0x78B5;
    inline constexpr uint32_t Channels            = 0x9F;
    inline constexpr uint32_t BitDepth            = 0x6264;

    // Cluster & Block Elements
    inline constexpr uint32_t Cluster             = 0x1F43B675;
    inline constexpr uint32_t Timestamp           = 0xE7;     // Cluster Timecode (relative to Segment)
    inline constexpr uint32_t PrevSize            = 0xAB;
    inline constexpr uint32_t SimpleBlock         = 0xA3;
    inline constexpr uint32_t BlockGroup          = 0xA0;
    inline constexpr uint32_t Block               = 0xA1;
    inline constexpr uint32_t BlockDuration       = 0x9B;

    // Cues (Seeking Index)
    inline constexpr uint32_t Cues                = 0x1C53BB6B;
    inline constexpr uint32_t CuePoint            = 0xBB;
    inline constexpr uint32_t CueTime             = 0xB3;
    inline constexpr uint32_t CueTrackPositions   = 0xB7;
    inline constexpr uint32_t CueTrack            = 0xF7;
    inline constexpr uint32_t CueClusterPosition  = 0xF1;
    inline constexpr uint32_t CueBlockNumber      = 0x5378;
} // namespace ebml_id

/// Track type values in Matroska / WebM.
enum class MkvTrackType : uint8_t {
    Video    = 1,
    Audio    = 2,
    Complex  = 3,
    Logo     = 16,
    Subtitle = 17,
    Button   = 18,
    Control  = 32
};

/// Block lacing mode.
enum class MkvLacingMode : uint8_t {
    None  = 0,
    Xiph  = 1,
    Fixed = 2,
    Ebml  = 3
};

/// Represents an element header (ID, size, and absolute offset of data).
struct EbmlElementHeader {
    uint32_t id{0};
    uint64_t size{0};
    int64_t data_offset{0};
    int64_t header_size{0};
};

/// EBML / VINT parsing and serialization utilities.
class Ebml {
public:
    /// Decodes an EBML Element ID from the reader without stripping the leading length indicator bit.
    static MediaResult<uint32_t> read_element_id(IoReader& reader);

    /// Decodes an EBML Variable-Length Quantity (VINT) data size, stripping the leading length indicator bit.
    static MediaResult<uint64_t> read_vint_size(IoReader& reader);

    /// Decodes an unsigned integer up to 8 bytes.
    static MediaResult<uint64_t> read_uint(IoReader& reader, size_t size);

    /// Decodes a signed integer up to 8 bytes (2's complement).
    static MediaResult<int64_t> read_int(IoReader& reader, size_t size);

    /// Decodes a 32-bit or 64-bit IEEE-754 floating-point number.
    static MediaResult<double> read_float(IoReader& reader, size_t size);

    /// Decodes a string element of given length.
    static MediaResult<std::string> read_string(IoReader& reader, size_t size);

    /// Reads next element header at current cursor.
    static MediaResult<EbmlElementHeader> read_element_header(IoReader& reader);

    // --- Writing / Serialization Primitives ---

    /// Encodes an EBML Element ID into buffer. Returns number of bytes written.
    static size_t write_element_id(uint8_t* dst, uint32_t id) noexcept;

    /// Encodes a VINT size into buffer. Returns number of bytes written.
    static size_t write_vint_size(uint8_t* dst, uint64_t val, size_t fixed_width = 0) noexcept;

    /// Writes an unsigned integer element.
    static MediaResult<void> write_uint_element(IoWriter& writer, uint32_t id, uint64_t val);

    /// Writes a float/double element.
    static MediaResult<void> write_float_element(IoWriter& writer, uint32_t id, double val);

    /// Writes a string element.
    static MediaResult<void> write_string_element(IoWriter& writer, uint32_t id, std::string_view str);

    /// Writes a binary element.
    static MediaResult<void> write_binary_element(IoWriter& writer, uint32_t id, std::span<const uint8_t> data);

    /// Writes a master element header with known size.
    static MediaResult<void> write_master_header(IoWriter& writer, uint32_t id, uint64_t payload_size);
};

} // namespace atum

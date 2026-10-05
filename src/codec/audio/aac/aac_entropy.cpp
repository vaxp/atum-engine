#include "atum/codec/audio/aac/aac_entropy.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace atum {

// ============================================================================
// Scalefactor band offset tables (ISO/IEC 14496-3 Table 4.110 - 4.128)
// ============================================================================

static constexpr uint16_t SFB_44100_LONG[50] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 72, 80, 88, 96, 108, 120,
    132, 144, 160, 176, 196, 216, 240, 264, 292, 320, 352, 384, 416, 448, 480, 512,
    544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896, 928, 1024
};

static constexpr uint16_t SFB_44100_SHORT[15] = {
    0, 4, 8, 12, 16, 20, 24, 32, 40, 48, 64, 80, 96, 112, 128
};

static constexpr uint16_t SFB_48000_LONG[50] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 72, 80, 88, 96, 108, 120,
    132, 144, 160, 176, 196, 216, 240, 264, 292, 320, 352, 384, 416, 448, 480, 512,
    544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896, 928, 1024
};

static constexpr uint16_t SFB_48000_SHORT[15] = {
    0, 4, 8, 12, 16, 20, 28, 36, 44, 56, 68, 80, 96, 112, 128
};

static constexpr uint16_t SFB_32000_LONG[52] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 72, 80, 88, 96, 108, 120,
    132, 144, 160, 176, 196, 216, 240, 264, 292, 320, 352, 384, 416, 448, 480, 512,
    544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896, 928, 960, 992, 1024
};

static constexpr uint16_t SFB_32000_SHORT[15] = {
    0, 4, 8, 12, 16, 20, 24, 32, 40, 48, 64, 80, 96, 112, 128
};

std::span<const uint16_t> AacEntropyDecoder::get_sfb_offsets(uint32_t sample_rate, bool is_short) noexcept {
    if (sample_rate >= 46000) {
        return is_short ? std::span<const uint16_t>(SFB_48000_SHORT) : std::span<const uint16_t>(SFB_48000_LONG);
    } else if (sample_rate >= 38000) {
        return is_short ? std::span<const uint16_t>(SFB_44100_SHORT) : std::span<const uint16_t>(SFB_44100_LONG);
    } else {
        return is_short ? std::span<const uint16_t>(SFB_32000_SHORT) : std::span<const uint16_t>(SFB_32000_LONG);
    }
}

// ============================================================================
// Binary Huffman Prefix Tree Representation
// ============================================================================

struct HuffmanNode {
    int16_t child[2]{-1, -1};
    int16_t symbol{-1};
};

class HuffmanTree {
public:
    HuffmanTree() = default;

    void init(const uint32_t* codes, const uint8_t* bits, size_t count) {
        nodes_.clear();
        nodes_.push_back(HuffmanNode{}); // Root node = index 0

        for (size_t i = 0; i < count; ++i) {
            uint32_t code = codes[i];
            uint8_t bit_count = bits[i];
            if (bit_count == 0) continue;

            size_t curr = 0;
            for (int b = bit_count - 1; b >= 0; --b) {
                uint32_t bit = (code >> b) & 1;
                if (nodes_[curr].child[bit] == -1) {
                    size_t next_idx = nodes_.size();
                    nodes_[curr].child[bit] = static_cast<int16_t>(next_idx);
                    nodes_.push_back(HuffmanNode{});
                }
                curr = static_cast<size_t>(nodes_[curr].child[bit]);
            }
            nodes_[curr].symbol = static_cast<int16_t>(i);
        }
    }

    [[nodiscard]] MediaResult<int16_t> decode(BitReader& reader) const noexcept {
        if (nodes_.empty()) return MediaError::InvalidState;
        size_t curr = 0;
        while (nodes_[curr].symbol == -1) {
            auto b = reader.read_bit();
            if (!b) return b.error();
            int16_t next = nodes_[curr].child[*b];
            if (next == -1) {
                return MediaError::InvalidData;
            }
            curr = static_cast<size_t>(next);
        }
        return nodes_[curr].symbol;
    }

private:
    std::vector<HuffmanNode> nodes_;
};

// ============================================================================
// Scalefactor Huffman Codebook
// ============================================================================

static const uint32_t AAC_SCALEFACTOR_CODES[121] = {
    0x3ffe8, 0x3ffe6, 0x3ffe7, 0x3ffe5, 0x7fff5, 0x7fff1, 0x7ffed, 0x7fff6,
    0x7ffee, 0x7ffef, 0x7fff0, 0x7fffc, 0x7fffd, 0x7ffff, 0x7fffe, 0x7fff7,
    0x7fff8, 0x7fffb, 0x7fff9, 0x3ffe4, 0x7fffa, 0x3ffe3, 0x1ffef, 0x1fff0,
    0x0fff5, 0x1ffee, 0x0fff2, 0x0fff3, 0x0fff4, 0x0fff1, 0x07ff6, 0x07ff7,
    0x03ff9, 0x03ff5, 0x03ff7, 0x03ff3, 0x03ff6, 0x03ff2, 0x01ff7, 0x01ff5,
    0x00ff9, 0x00ff7, 0x00ff6, 0x007f9, 0x00ff4, 0x007f8, 0x003f9, 0x003f7,
    0x003f5, 0x001f8, 0x001f7, 0x000fa, 0x000f8, 0x000f6, 0x00079, 0x0003a,
    0x00038, 0x0001a, 0x0000b, 0x00004, 0x00000, 0x0000a, 0x0000c, 0x0001b,
    0x00039, 0x0003b, 0x00078, 0x0007a, 0x000f7, 0x000f9, 0x001f6, 0x001f9,
    0x003f4, 0x003f6, 0x003f8, 0x007f5, 0x007f4, 0x007f6, 0x007f7, 0x00ff5,
    0x00ff8, 0x01ff4, 0x01ff6, 0x01ff8, 0x03ff8, 0x03ff4, 0x0fff0, 0x07ff4,
    0x0fff6, 0x07ff5, 0x3ffe2, 0x7ffd9, 0x7ffda, 0x7ffdb, 0x7ffdc, 0x7ffdd,
    0x7ffde, 0x7ffd8, 0x7ffd2, 0x7ffd3, 0x7ffd4, 0x7ffd5, 0x7ffd6, 0x7fff2,
    0x7ffdf, 0x7ffe7, 0x7ffe8, 0x7ffe9, 0x7ffea, 0x7ffeb, 0x7ffe6, 0x7ffe0,
    0x7ffe1, 0x7ffe2, 0x7ffe3, 0x7ffe4, 0x7ffe5, 0x7ffd7, 0x7ffec, 0x7fff4,
    0x7fff3
};

static const uint8_t AAC_SCALEFACTOR_BITS[121] = {
    18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 18, 19, 18, 17, 17, 16, 17, 16, 16, 16, 16, 15, 15,
    14, 14, 14, 14, 14, 14, 13, 13, 12, 12, 12, 11, 12, 11, 10, 10,
    10,  9,  9,  8,  8,  8,  7,  6,  6,  5,  4,  3,  1,  4,  4,  5,
     6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 10, 11, 11, 11, 11, 12,
    12, 13, 13, 13, 14, 14, 16, 15, 16, 15, 18, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19, 19
};

// ============================================================================
// Spectral Huffman Codebooks 1 .. 11 Data
// ============================================================================

static const uint16_t CODES1[81] = {
    0x7f8, 0x1f1, 0x7fd, 0x3f5, 0x068, 0x3f0, 0x7f7, 0x1ec,
    0x7f5, 0x3f1, 0x072, 0x3f4, 0x074, 0x011, 0x076, 0x1eb,
    0x06c, 0x3f6, 0x7fc, 0x1e1, 0x7f1, 0x1f0, 0x061, 0x1f6,
    0x7f2, 0x1ea, 0x7fb, 0x1f2, 0x069, 0x1ed, 0x077, 0x017,
    0x06f, 0x1e6, 0x064, 0x1e5, 0x067, 0x015, 0x062, 0x012,
    0x000, 0x014, 0x065, 0x016, 0x06d, 0x1e9, 0x063, 0x1e4,
    0x06b, 0x013, 0x071, 0x1e3, 0x070, 0x1f3, 0x7fe, 0x1e7,
    0x7f3, 0x1ef, 0x060, 0x1ee, 0x7f0, 0x1e2, 0x7fa, 0x3f3,
    0x06a, 0x1e8, 0x075, 0x010, 0x073, 0x1f4, 0x06e, 0x3f7,
    0x7f6, 0x1e0, 0x7f9, 0x3f2, 0x066, 0x1f5, 0x7ff, 0x1f7,
    0x7f4
};

static const uint8_t BITS1[81] = {
    11,  9, 11, 10,  7, 10, 11,  9, 11, 10,  7, 10,  7,  5,  7,  9,
     7, 10, 11,  9, 11,  9,  7,  9, 11,  9, 11,  9,  7,  9,  7,  5,
     7,  9,  7,  9,  7,  5,  7,  5,  1,  5,  7,  5,  7,  9,  7,  9,
     7,  5,  7,  9,  7,  9, 11,  9, 11,  9,  7,  9, 11,  9, 11, 10,
     7,  9,  7,  5,  7,  9,  7, 10, 11,  9, 11, 10,  7,  9, 11,  9,
    11
};

static const uint16_t CODES2[81] = {
    0x1f3, 0x06f, 0x1fd, 0x0eb, 0x023, 0x0ea, 0x1f7, 0x0e8,
    0x1fa, 0x0f2, 0x02d, 0x070, 0x020, 0x006, 0x02b, 0x06e,
    0x028, 0x0e9, 0x1f9, 0x066, 0x0f8, 0x0e7, 0x01b, 0x0f1,
    0x1f4, 0x06b, 0x1f5, 0x0ec, 0x02a, 0x06c, 0x02c, 0x00a,
    0x027, 0x067, 0x01a, 0x0f5, 0x024, 0x008, 0x01f, 0x009,
    0x000, 0x007, 0x01d, 0x00b, 0x030, 0x0ef, 0x01c, 0x064,
    0x01e, 0x00c, 0x029, 0x0f3, 0x02f, 0x0f0, 0x1fc, 0x071,
    0x1f2, 0x0f4, 0x021, 0x0e6, 0x0f7, 0x068, 0x1f8, 0x0ee,
    0x022, 0x065, 0x031, 0x002, 0x026, 0x0ed, 0x025, 0x06a,
    0x1fb, 0x072, 0x1fe, 0x069, 0x02e, 0x0f6, 0x1ff, 0x06d,
    0x1f6
};

static const uint8_t BITS2[81] = {
    9, 7, 9, 8, 6, 8, 9, 8, 9, 8, 6, 7, 6, 5, 6, 7,
    6, 8, 9, 7, 8, 8, 6, 8, 9, 7, 9, 8, 6, 7, 6, 5,
    6, 7, 6, 8, 6, 5, 6, 5, 3, 5, 6, 5, 6, 8, 6, 7,
    6, 5, 6, 8, 6, 8, 9, 7, 9, 8, 6, 8, 8, 7, 9, 8,
    6, 7, 6, 4, 6, 8, 6, 7, 9, 7, 9, 7, 6, 8, 9, 7,
    9
};

static const uint16_t CODES3[81] = {
    0x0000, 0x0009, 0x00ef, 0x000b, 0x0019, 0x00f0, 0x01eb, 0x01e6,
    0x03f2, 0x000a, 0x0035, 0x01ef, 0x0034, 0x0037, 0x01e9, 0x01ed,
    0x01e7, 0x03f3, 0x01ee, 0x03ed, 0x1ffa, 0x01ec, 0x01f2, 0x07f9,
    0x07f8, 0x03f8, 0x0ff8, 0x0008, 0x0038, 0x03f6, 0x0036, 0x0075,
    0x03f1, 0x03eb, 0x03ec, 0x0ff4, 0x0018, 0x0076, 0x07f4, 0x0039,
    0x0074, 0x03ef, 0x01f3, 0x01f4, 0x07f6, 0x01e8, 0x03ea, 0x1ffc,
    0x00f2, 0x01f1, 0x0ffb, 0x03f5, 0x07f3, 0x0ffc, 0x00ee, 0x03f7,
    0x7ffe, 0x01f0, 0x07f5, 0x7ffd, 0x1ffb, 0x3ffa, 0xffff, 0x00f1,
    0x03f0, 0x3ffc, 0x01ea, 0x03ee, 0x3ffb, 0x0ff6, 0x0ffa, 0x7ffc,
    0x07f2, 0x0ff5, 0xfffe, 0x03f4, 0x07f7, 0x7ffb, 0x0ff7, 0x0ff9,
    0x7ffa
};

static const uint8_t BITS3[81] = {
     1,  4,  8,  4,  5,  8,  9,  9, 10,  4,  6,  9,  6,  6,  9,  9,
     9, 10,  9, 10, 13,  9,  9, 11, 11, 10, 12,  4,  6, 10,  6,  7,
    10, 10, 10, 12,  5,  7, 11,  6,  7, 10,  9,  9, 11,  9, 10, 13,
     8,  9, 12, 10, 11, 12,  8, 10, 15,  9, 11, 15, 13, 14, 16,  8,
    10, 14,  9, 10, 14, 12, 12, 15, 11, 12, 16, 10, 11, 15, 12, 12,
    15
};

static const uint16_t CODES4[81] = {
    0x007, 0x016, 0x0f6, 0x018, 0x008, 0x0ef, 0x1ef, 0x0f3,
    0x7f8, 0x019, 0x017, 0x0ed, 0x015, 0x001, 0x0e2, 0x0f0,
    0x070, 0x3f0, 0x1ee, 0x0f1, 0x7fa, 0x0ee, 0x0e4, 0x3f2,
    0x7f6, 0x3ef, 0x7fd, 0x005, 0x014, 0x0f2, 0x009, 0x004,
    0x0e5, 0x0f4, 0x0e8, 0x3f4, 0x006, 0x002, 0x0e7, 0x003,
    0x000, 0x06b, 0x0e3, 0x069, 0x1f3, 0x0eb, 0x0e6, 0x3f6,
    0x06e, 0x06a, 0x1f4, 0x3ec, 0x1f0, 0x3f9, 0x0f5, 0x0ec,
    0x7fb, 0x0ea, 0x06f, 0x3f7, 0x7f9, 0x3f3, 0xfff, 0x0e9,
    0x06d, 0x3f8, 0x06c, 0x068, 0x1f5, 0x3ee, 0x1f2, 0x7f4,
    0x7f7, 0x3f1, 0xffe, 0x3ed, 0x1f1, 0x7f5, 0x7fe, 0x3f5,
    0x7fc
};

static const uint8_t BITS4[81] = {
     3,  5,  8,  5,  4,  8,  9,  8, 11,  5,  5,  8,  5,  3,  8,  8,
     7, 10,  9,  8, 11,  8,  8, 10, 11, 10, 11,  3,  5,  8,  4,  3,
     8,  8,  8, 10,  4,  3,  8,  3,  2,  7,  8,  7,  9,  8,  8, 10,
     7,  7,  9, 10,  9, 10,  8,  8, 11,  8,  7, 10, 11, 10, 12,  8,
     7, 10,  7,  7,  9, 10,  9, 11, 11, 10, 12, 10,  9, 11, 11, 10,
    11
};

static const uint16_t CODES5[81] = {
    0x1fff, 0x0ff7, 0x07f4, 0x07e8, 0x03f1, 0x07ee, 0x07f9, 0x0ff8,
    0x1ffd, 0x0ffd, 0x07f1, 0x03e8, 0x01e8, 0x00f0, 0x01ec, 0x03ee,
    0x07f2, 0x0ffa, 0x0ff4, 0x03ef, 0x01f2, 0x00e8, 0x0070, 0x00ec,
    0x01f0, 0x03ea, 0x07f3, 0x07eb, 0x01eb, 0x00ea, 0x001a, 0x0008,
    0x0019, 0x00ee, 0x01ef, 0x07ed, 0x03f0, 0x00f2, 0x0073, 0x000b,
    0x0000, 0x000a, 0x0071, 0x00f3, 0x07e9, 0x07ef, 0x01ee, 0x00ef,
    0x0018, 0x0009, 0x001b, 0x00eb, 0x01e9, 0x07ec, 0x07f6, 0x03eb,
    0x01f3, 0x00ed, 0x0072, 0x00e9, 0x01f1, 0x03ed, 0x07f7, 0x0ff6,
    0x07f0, 0x03e9, 0x01ed, 0x00f1, 0x01ea, 0x03ec, 0x07f8, 0x0ff9,
    0x1ffc, 0x0ffc, 0x0ff5, 0x07ea, 0x03f3, 0x03f2, 0x07f5, 0x0ffb,
    0x1ffe
};

static const uint8_t BITS5[81] = {
    13, 12, 11, 11, 10, 11, 11, 12, 13, 12, 11, 10,  9,  8,  9, 10,
    11, 12, 12, 10,  9,  8,  7,  8,  9, 10, 11, 11,  9,  8,  5,  4,
     5,  8,  9, 11, 10,  8,  7,  4,  1,  4,  7,  8, 11, 11,  9,  8,
     5,  4,  5,  8,  9, 11, 11, 10,  9,  8,  7,  8,  9, 10, 11, 12,
    11, 10,  9,  8,  9, 10, 11, 12, 13, 12, 12, 11, 10, 10, 11, 12,
    13
};

static const uint16_t CODES6[81] = {
    0x7fe, 0x3fd, 0x1f1, 0x1eb, 0x1f4, 0x1ea, 0x1f0, 0x3fc,
    0x7fd, 0x3f6, 0x1e5, 0x0ea, 0x06c, 0x071, 0x068, 0x0f0,
    0x1e6, 0x3f7, 0x1f3, 0x0ef, 0x032, 0x027, 0x028, 0x026,
    0x031, 0x0eb, 0x1f7, 0x1e8, 0x06f, 0x02e, 0x008, 0x004,
    0x006, 0x029, 0x06b, 0x1ee, 0x1ef, 0x072, 0x02d, 0x002,
    0x000, 0x003, 0x02f, 0x073, 0x1fa, 0x1e7, 0x06e, 0x02b,
    0x007, 0x001, 0x005, 0x02c, 0x06d, 0x1ec, 0x1f9, 0x0ee,
    0x030, 0x024, 0x02a, 0x025, 0x033, 0x0ec, 0x1f2, 0x3f8,
    0x1e4, 0x0ed, 0x06a, 0x070, 0x069, 0x074, 0x0f1, 0x3fa,
    0x7ff, 0x3f9, 0x1f6, 0x1ed, 0x1f8, 0x1e9, 0x1f5, 0x3fb,
    0x7fc
};

static const uint8_t BITS6[81] = {
    11, 10,  9,  9,  9,  9,  9, 10, 11, 10,  9,  8,  7,  7,  7,  8,
     9, 10,  9,  8,  6,  6,  6,  6,  6,  8,  9,  9,  7,  6,  4,  4,
     4,  6,  7,  9,  9,  7,  6,  4,  4,  4,  6,  7,  9,  9,  7,  6,
     4,  4,  4,  6,  7,  9,  9,  8,  6,  6,  6,  6,  6,  8,  9, 10,
     9,  8,  7,  7,  7,  7,  8, 10, 11, 10,  9,  9,  9,  9,  9, 10,
    11
};

static const uint16_t CODES7[64] = {
    0x000, 0x005, 0x037, 0x074, 0x0f2, 0x1eb, 0x3ed, 0x7f7,
    0x004, 0x00c, 0x035, 0x071, 0x0ec, 0x0ee, 0x1ee, 0x1f5,
    0x036, 0x034, 0x072, 0x0ea, 0x0f1, 0x1e9, 0x1f3, 0x3f5,
    0x073, 0x070, 0x0eb, 0x0f0, 0x1f1, 0x1f0, 0x3ec, 0x3fa,
    0x0f3, 0x0ed, 0x1e8, 0x1ef, 0x3ef, 0x3f1, 0x3f9, 0x7fb,
    0x1ed, 0x0ef, 0x1ea, 0x1f2, 0x3f3, 0x3f8, 0x7f9, 0x7fc,
    0x3ee, 0x1ec, 0x1f4, 0x3f4, 0x3f7, 0x7f8, 0xffd, 0xffe,
    0x7f6, 0x3f0, 0x3f2, 0x3f6, 0x7fa, 0x7fd, 0xffc, 0xfff
};

static const uint8_t BITS7[64] = {
     1,  3,  6,  7,  8,  9, 10, 11,  3,  4,  6,  7,  8,  8,  9,  9,
     6,  6,  7,  8,  8,  9,  9, 10,  7,  7,  8,  8,  9,  9, 10, 10,
     8,  8,  9,  9, 10, 10, 10, 11,  9,  8,  9,  9, 10, 10, 11, 11,
    10,  9,  9, 10, 10, 11, 12, 12, 11, 10, 10, 10, 11, 11, 12, 12
};

static const uint16_t CODES8[64] = {
    0x00e, 0x005, 0x010, 0x030, 0x06f, 0x0f1, 0x1fa, 0x3fe,
    0x003, 0x000, 0x004, 0x012, 0x02c, 0x06a, 0x075, 0x0f8,
    0x00f, 0x002, 0x006, 0x014, 0x02e, 0x069, 0x072, 0x0f5,
    0x02f, 0x011, 0x013, 0x02a, 0x032, 0x06c, 0x0ec, 0x0fa,
    0x071, 0x02b, 0x02d, 0x031, 0x06d, 0x070, 0x0f2, 0x1f9,
    0x0ef, 0x068, 0x033, 0x06b, 0x06e, 0x0ee, 0x0f9, 0x3fc,
    0x1f8, 0x074, 0x073, 0x0ed, 0x0f0, 0x0f6, 0x1f6, 0x1fd,
    0x3fd, 0x0f3, 0x0f4, 0x0f7, 0x1f7, 0x1fb, 0x1fc, 0x3ff
};

static const uint8_t BITS8[64] = {
     5,  4,  5,  6,  7,  8,  9, 10,  4,  3,  4,  5,  6,  7,  7,  8,
     5,  4,  4,  5,  6,  7,  7,  8,  6,  5,  5,  6,  6,  7,  8,  8,
     7,  6,  6,  6,  7,  7,  8,  9,  8,  7,  6,  7,  7,  8,  8, 10,
     9,  7,  7,  8,  8,  8,  9,  9, 10,  8,  8,  8,  9,  9,  9, 10
};

static const uint16_t CODES9[169] = {
    0x0000, 0x0005, 0x0037, 0x00e7, 0x01de, 0x03ce, 0x03d9, 0x07c8,
    0x07cd, 0x0fc8, 0x0fdd, 0x1fe4, 0x1fec, 0x0004, 0x000c, 0x0035,
    0x0072, 0x00ea, 0x00ed, 0x01e2, 0x03d1, 0x03d3, 0x03e0, 0x07d8,
    0x0fcf, 0x0fd5, 0x0036, 0x0034, 0x0071, 0x00e8, 0x00ec, 0x01e1,
    0x03cf, 0x03dd, 0x03db, 0x07d0, 0x0fc7, 0x0fd4, 0x0fe4, 0x00e6,
    0x0070, 0x00e9, 0x01dd, 0x01e3, 0x03d2, 0x03dc, 0x07cc, 0x07ca,
    0x07de, 0x0fd8, 0x0fea, 0x1fdb, 0x01df, 0x00eb, 0x01dc, 0x01e6,
    0x03d5, 0x03de, 0x07cb, 0x07dd, 0x07dc, 0x0fcd, 0x0fe2, 0x0fe7,
    0x1fe1, 0x03d0, 0x01e0, 0x01e4, 0x03d6, 0x07c5, 0x07d1, 0x07db,
    0x0fd2, 0x07e0, 0x0fd9, 0x0feb, 0x1fe3, 0x1fe9, 0x07c4, 0x01e5,
    0x03d7, 0x07c6, 0x07cf, 0x07da, 0x0fcb, 0x0fda, 0x0fe3, 0x0fe9,
    0x1fe6, 0x1ff3, 0x1ff7, 0x07d3, 0x03d8, 0x03e1, 0x07d4, 0x07d9,
    0x0fd3, 0x0fde, 0x1fdd, 0x1fd9, 0x1fe2, 0x1fea, 0x1ff1, 0x1ff6,
    0x07d2, 0x03d4, 0x03da, 0x07c7, 0x07d7, 0x07e2, 0x0fce, 0x0fdb,
    0x1fd8, 0x1fee, 0x3ff0, 0x1ff4, 0x3ff2, 0x07e1, 0x03df, 0x07c9,
    0x07d6, 0x0fca, 0x0fd0, 0x0fe5, 0x0fe6, 0x1feb, 0x1fef, 0x3ff3,
    0x3ff4, 0x3ff5, 0x0fe0, 0x07ce, 0x07d5, 0x0fc6, 0x0fd1, 0x0fe1,
    0x1fe0, 0x1fe8, 0x1ff0, 0x3ff1, 0x3ff8, 0x3ff6, 0x7ffc, 0x0fe8,
    0x07df, 0x0fc9, 0x0fd7, 0x0fdc, 0x1fdc, 0x1fdf, 0x1fed, 0x1ff5,
    0x3ff9, 0x3ffb, 0x7ffd, 0x7ffe, 0x1fe7, 0x0fcc, 0x0fd6, 0x0fdf,
    0x1fde, 0x1fda, 0x1fe5, 0x1ff2, 0x3ffa, 0x3ff7, 0x3ffc, 0x3ffd,
    0x7fff
};

static const uint8_t BITS9[169] = {
     1,  3,  6,  8,  9, 10, 10, 11, 11, 12, 12, 13, 13,  3,  4,  6,
     7,  8,  8,  9, 10, 10, 10, 11, 12, 12,  6,  6,  7,  8,  8,  9,
    10, 10, 10, 11, 12, 12, 12,  8,  7,  8,  9,  9, 10, 10, 11, 11,
    11, 12, 12, 13,  9,  8,  9,  9, 10, 10, 11, 11, 11, 12, 12, 12,
    13, 10,  9,  9, 10, 11, 11, 11, 12, 11, 12, 12, 13, 13, 11,  9,
    10, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 11, 10, 10, 11, 11,
    12, 12, 13, 13, 13, 13, 13, 13, 11, 10, 10, 11, 11, 11, 12, 12,
    13, 13, 14, 13, 14, 11, 10, 11, 11, 12, 12, 12, 12, 13, 13, 14,
    14, 14, 12, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 12,
    11, 12, 12, 12, 13, 13, 13, 13, 14, 14, 15, 15, 13, 12, 12, 12,
    13, 13, 13, 13, 14, 14, 14, 14, 15
};

static const uint16_t CODES10[169] = {
    0x022, 0x008, 0x01d, 0x026, 0x05f, 0x0d3, 0x1cf, 0x3d0,
    0x3d7, 0x3ed, 0x7f0, 0x7f6, 0xffd, 0x007, 0x000, 0x001,
    0x009, 0x020, 0x054, 0x060, 0x0d5, 0x0dc, 0x1d4, 0x3cd,
    0x3de, 0x7e7, 0x01c, 0x002, 0x006, 0x00c, 0x01e, 0x028,
    0x05b, 0x0cd, 0x0d9, 0x1ce, 0x1dc, 0x3d9, 0x3f1, 0x025,
    0x00b, 0x00a, 0x00d, 0x024, 0x057, 0x061, 0x0cc, 0x0dd,
    0x1cc, 0x1de, 0x3d3, 0x3e7, 0x05d, 0x021, 0x01f, 0x023,
    0x027, 0x059, 0x064, 0x0d8, 0x0df, 0x1d2, 0x1e2, 0x3dd,
    0x3ee, 0x0d1, 0x055, 0x029, 0x056, 0x058, 0x062, 0x0ce,
    0x0e0, 0x0e2, 0x1da, 0x3d4, 0x3e3, 0x7eb, 0x1c9, 0x05e,
    0x05a, 0x05c, 0x063, 0x0ca, 0x0da, 0x1c7, 0x1ca, 0x1e0,
    0x3db, 0x3e8, 0x7ec, 0x1e3, 0x0d2, 0x0cb, 0x0d0, 0x0d7,
    0x0db, 0x1c6, 0x1d5, 0x1d8, 0x3ca, 0x3da, 0x7ea, 0x7f1,
    0x1e1, 0x0d4, 0x0cf, 0x0d6, 0x0de, 0x0e1, 0x1d0, 0x1d6,
    0x3d1, 0x3d5, 0x3f2, 0x7ee, 0x7fb, 0x3e9, 0x1cd, 0x1c8,
    0x1cb, 0x1d1, 0x1d7, 0x1df, 0x3cf, 0x3e0, 0x3ef, 0x7e6,
    0x7f8, 0xffa, 0x3eb, 0x1dd, 0x1d3, 0x1d9, 0x1db, 0x3d2,
    0x3cc, 0x3dc, 0x3ea, 0x7ed, 0x7f3, 0x7f9, 0xff9, 0x7f2,
    0x3ce, 0x1e4, 0x3cb, 0x3d8, 0x3d6, 0x3e2, 0x3e5, 0x7e8,
    0x7f4, 0x7f5, 0x7f7, 0xffb, 0x7fa, 0x3ec, 0x3df, 0x3e1,
    0x3e4, 0x3e6, 0x3f0, 0x7e9, 0x7ef, 0xff8, 0xffe, 0xffc,
    0xfff
};

static const uint8_t BITS10[169] = {
     6,  5,  6,  6,  7,  8,  9, 10, 10, 10, 11, 11, 12,  5,  4,  4,
     5,  6,  7,  7,  8,  8,  9, 10, 10, 11,  6,  4,  5,  5,  6,  6,
     7,  8,  8,  9,  9, 10, 10,  6,  5,  5,  5,  6,  7,  7,  8,  8,
     9,  9, 10, 10,  7,  6,  6,  6,  6,  7,  7,  8,  8,  9,  9, 10,
    10,  8,  7,  6,  7,  7,  7,  8,  8,  8,  9, 10, 10, 11,  9,  7,
     7,  7,  7,  8,  8,  9,  9,  9, 10, 10, 11,  9,  8,  8,  8,  8,
     8,  9,  9,  9, 10, 10, 11, 11,  9,  8,  8,  8,  8,  8,  9,  9,
    10, 10, 10, 11, 11, 10,  9,  9,  9,  9,  9,  9, 10, 10, 10, 11,
    11, 12, 10,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11, 11, 12, 11,
    10,  9, 10, 10, 10, 10, 10, 11, 11, 11, 11, 12, 11, 10, 10, 10,
    10, 10, 10, 11, 11, 12, 12, 12, 12
};

static const uint16_t CODES11[289] = {
    0x000, 0x006, 0x019, 0x03d, 0x09c, 0x0c6, 0x1a7, 0x390,
    0x3c2, 0x3df, 0x7e6, 0x7f3, 0xffb, 0x7ec, 0xffa, 0xffe,
    0x38e, 0x005, 0x001, 0x008, 0x014, 0x037, 0x042, 0x092,
    0x0af, 0x191, 0x1a5, 0x1b5, 0x39e, 0x3c0, 0x3a2, 0x3cd,
    0x7d6, 0x0ae, 0x017, 0x007, 0x009, 0x018, 0x039, 0x040,
    0x08e, 0x0a3, 0x0b8, 0x199, 0x1ac, 0x1c1, 0x3b1, 0x396,
    0x3be, 0x3ca, 0x09d, 0x03c, 0x015, 0x016, 0x01a, 0x03b,
    0x044, 0x091, 0x0a5, 0x0be, 0x196, 0x1ae, 0x1b9, 0x3a1,
    0x391, 0x3a5, 0x3d5, 0x094, 0x09a, 0x036, 0x038, 0x03a,
    0x041, 0x08c, 0x09b, 0x0b0, 0x0c3, 0x19e, 0x1ab, 0x1bc,
    0x39f, 0x38f, 0x3a9, 0x3cf, 0x093, 0x0bf, 0x03e, 0x03f,
    0x043, 0x045, 0x09e, 0x0a7, 0x0b9, 0x194, 0x1a2, 0x1ba,
    0x1c3, 0x3a6, 0x3a7, 0x3bb, 0x3d4, 0x09f, 0x1a0, 0x08f,
    0x08d, 0x090, 0x098, 0x0a6, 0x0b6, 0x0c4, 0x19f, 0x1af,
    0x1bf, 0x399, 0x3bf, 0x3b4, 0x3c9, 0x3e7, 0x0a8, 0x1b6,
    0x0ab, 0x0a4, 0x0aa, 0x0b2, 0x0c2, 0x0c5, 0x198, 0x1a4,
    0x1b8, 0x38c, 0x3a4, 0x3c4, 0x3c6, 0x3dd, 0x3e8, 0x0ad,
    0x3af, 0x192, 0x0bd, 0x0bc, 0x18e, 0x197, 0x19a, 0x1a3,
    0x1b1, 0x38d, 0x398, 0x3b7, 0x3d3, 0x3d1, 0x3db, 0x7dd,
    0x0b4, 0x3de, 0x1a9, 0x19b, 0x19c, 0x1a1, 0x1aa, 0x1ad,
    0x1b3, 0x38b, 0x3b2, 0x3b8, 0x3ce, 0x3e1, 0x3e0, 0x7d2,
    0x7e5, 0x0b7, 0x7e3, 0x1bb, 0x1a8, 0x1a6, 0x1b0, 0x1b2,
    0x1b7, 0x39b, 0x39a, 0x3ba, 0x3b5, 0x3d6, 0x7d7, 0x3e4,
    0x7d8, 0x7ea, 0x0ba, 0x7e8, 0x3a0, 0x1bd, 0x1b4, 0x38a,
    0x1c4, 0x392, 0x3aa, 0x3b0, 0x3bc, 0x3d7, 0x7d4, 0x7dc,
    0x7db, 0x7d5, 0x7f0, 0x0c1, 0x7fb, 0x3c8, 0x3a3, 0x395,
    0x39d, 0x3ac, 0x3ae, 0x3c5, 0x3d8, 0x3e2, 0x3e6, 0x7e4,
    0x7e7, 0x7e0, 0x7e9, 0x7f7, 0x190, 0x7f2, 0x393, 0x1be,
    0x1c0, 0x394, 0x397, 0x3ad, 0x3c3, 0x3c1, 0x3d2, 0x7da,
    0x7d9, 0x7df, 0x7eb, 0x7f4, 0x7fa, 0x195, 0x7f8, 0x3bd,
    0x39c, 0x3ab, 0x3a8, 0x3b3, 0x3b9, 0x3d0, 0x3e3, 0x3e5,
    0x7e2, 0x7de, 0x7ed, 0x7f1, 0x7f9, 0x7fc, 0x193, 0xffd,
    0x3dc, 0x3b6, 0x3c7, 0x3cc, 0x3cb, 0x3d9, 0x3da, 0x7d3,
    0x7e1, 0x7ee, 0x7ef, 0x7f5, 0x7f6, 0xffc, 0xfff, 0x19d,
    0x1c2, 0x0b5, 0x0a1, 0x096, 0x097, 0x095, 0x099, 0x0a0,
    0x0a2, 0x0ac, 0x0a9, 0x0b1, 0x0b3, 0x0bb, 0x0c0, 0x18f,
    0x004
};

static const uint8_t BITS11[289] = {
     4,  5,  6,  7,  8,  8,  9, 10, 10, 10, 11, 11, 12, 11, 12, 12,
    10,  5,  4,  5,  6,  7,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10,
    11,  8,  6,  5,  5,  6,  7,  7,  8,  8,  8,  9,  9,  9, 10, 10,
    10, 10,  8,  7,  6,  6,  6,  7,  7,  8,  8,  8,  9,  9,  9, 10,
    10, 10, 10,  8,  8,  7,  7,  7,  7,  8,  8,  8,  8,  9,  9,  9,
    10, 10, 10, 10,  8,  8,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9,
     9, 10, 10, 10, 10,  8,  9,  8,  8,  8,  8,  8,  8,  8,  9,  9,
     9, 10, 10, 10, 10, 10,  8,  9,  8,  8,  8,  8,  8,  8,  9,  9,
     9, 10, 10, 10, 10, 10, 10,  8, 10,  9,  8,  8,  9,  9,  9,  9,
     9, 10, 10, 10, 10, 10, 10, 11,  8, 10,  9,  9,  9,  9,  9,  9,
     9, 10, 10, 10, 10, 10, 10, 11, 11,  8, 11,  9,  9,  9,  9,  9,
     9, 10, 10, 10, 10, 10, 11, 10, 11, 11,  8, 11, 10,  9,  9, 10,
     9, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11,  8, 11, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11,  9, 11, 10,  9,
     9, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11,  9, 11, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11,  9, 12,
    10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 12, 12,  9,
     9,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  9,
     5
};

// Global static tables container
struct AacHuffmanTables {
    HuffmanTree scalefactor_tree;
    HuffmanTree codebook_trees[12]; // 1-indexed (1..11 used)

    AacHuffmanTables() {
        // Scalefactor tree
        scalefactor_tree.init(AAC_SCALEFACTOR_CODES, AAC_SCALEFACTOR_BITS, 121);

        // Codebooks 1..11
        codebook_trees[1].init(reinterpret_cast<const uint32_t*>(CODES1), BITS1, 81);
        codebook_trees[2].init(reinterpret_cast<const uint32_t*>(CODES2), BITS2, 81);
        codebook_trees[3].init(reinterpret_cast<const uint32_t*>(CODES3), BITS3, 81);
        codebook_trees[4].init(reinterpret_cast<const uint32_t*>(CODES4), BITS4, 81);
        codebook_trees[5].init(reinterpret_cast<const uint32_t*>(CODES5), BITS5, 81);
        codebook_trees[6].init(reinterpret_cast<const uint32_t*>(CODES6), BITS6, 81);
        codebook_trees[7].init(reinterpret_cast<const uint32_t*>(CODES7), BITS7, 64);
        codebook_trees[8].init(reinterpret_cast<const uint32_t*>(CODES8), BITS8, 64);
        codebook_trees[9].init(reinterpret_cast<const uint32_t*>(CODES9), BITS9, 169);
        codebook_trees[10].init(reinterpret_cast<const uint32_t*>(CODES10), BITS10, 169);
        codebook_trees[11].init(reinterpret_cast<const uint32_t*>(CODES11), BITS11, 289);
    }
};

static const AacHuffmanTables& get_aac_tables() {
    static const AacHuffmanTables tables;
    return tables;
}

// ============================================================================
// ICS Info Decoding (ISO/IEC 14496-3 Section 4.5.2.3.1)
// ============================================================================

MediaResult<AacIcsInfo> AacEntropyDecoder::decode_ics_info(BitReader& reader) noexcept {
    auto res_bit = reader.read_bit();
    if (!res_bit) return res_bit.error(); // ics_reserved_bit

    auto seq_res = reader.read_bits(2);
    if (!seq_res) return seq_res.error();
    auto shape_res = reader.read_bit();
    if (!shape_res) return shape_res.error();

    AacIcsInfo ics{};
    ics.window_sequence = static_cast<AacWindowSequence>(*seq_res);
    ics.window_shape = static_cast<AacWindowShape>(*shape_res);

    if (ics.window_sequence == AacWindowSequence::EightShortSequence) {
        auto max_sfb_res = reader.read_bits(4);
        if (!max_sfb_res) return max_sfb_res.error();
        ics.max_sfb = static_cast<uint8_t>(*max_sfb_res);

        auto grp_res = reader.read_bits(7);
        if (!grp_res) return grp_res.error();
        uint32_t grouping = *grp_res;

        ics.num_windows = 8;
        ics.num_window_groups = 1;
        ics.window_group_length[0] = 1;

        for (int i = 0; i < 7; ++i) {
            bool same_group = ((grouping >> (6 - i)) & 1) != 0;
            if (same_group) {
                ics.window_group_length[ics.num_window_groups - 1]++;
            } else {
                ics.num_window_groups++;
                ics.window_group_length[ics.num_window_groups - 1] = 1;
            }
        }
    } else {
        auto max_sfb_res = reader.read_bits(6);
        if (!max_sfb_res) return max_sfb_res.error();
        ics.max_sfb = static_cast<uint8_t>(*max_sfb_res);

        auto pred_res = reader.read_bit();
        if (!pred_res) return pred_res.error(); // predictor_data_present
        if (*pred_res != 0) {
            // Predictor reset bit & units in LC profile are absent or must be 0
            return MediaError::NotImplemented;
        }

        ics.num_windows = 1;
        ics.num_window_groups = 1;
        ics.window_group_length[0] = 1;
    }

    return ics;
}

// ============================================================================
// Section Data Decoding (ISO/IEC 14496-3 Section 4.5.2.3.2)
// ============================================================================

MediaResult<void> AacEntropyDecoder::decode_section_data(
    BitReader& reader,
    const AacIcsInfo& ics,
    uint8_t* codebooks
) noexcept {
    std::memset(codebooks, 0, ics.num_window_groups * AAC_MAX_SFB);

    const uint32_t sect_bits = (ics.window_sequence == AacWindowSequence::EightShortSequence) ? 3 : 5;
    const uint32_t max_incr = (1u << sect_bits) - 1;

    for (uint8_t g = 0; g < ics.num_window_groups; ++g) {
        uint8_t sfb = 0;
        while (sfb < ics.max_sfb) {
            auto cb_res = reader.read_bits(4);
            if (!cb_res) return cb_res.error();
            uint8_t sect_cb = static_cast<uint8_t>(*cb_res);

            uint32_t sect_len = 0;
            while (true) {
                auto incr_res = reader.read_bits(sect_bits);
                if (!incr_res) return incr_res.error();
                uint32_t incr = *incr_res;
                sect_len += incr;
                if (incr != max_incr) break;
            }

            for (uint32_t i = 0; i < sect_len && (sfb + i) < ics.max_sfb; ++i) {
                codebooks[g * AAC_MAX_SFB + sfb + i] = sect_cb;
            }
            sfb += static_cast<uint8_t>(sect_len);
        }
    }
    return {};
}

// ============================================================================
// Scale Factor Data Decoding (ISO/IEC 14496-3 Section 4.5.2.3.3)
// ============================================================================

MediaResult<void> AacEntropyDecoder::decode_scalefactors(
    BitReader& reader,
    const AacIcsInfo& ics,
    uint8_t global_gain,
    const uint8_t* codebooks,
    int16_t* scalefactors
) noexcept {
    const auto& tables = get_aac_tables();
    int16_t current_sf = static_cast<int16_t>(global_gain);

    for (uint8_t g = 0; g < ics.num_window_groups; ++g) {
        for (uint8_t sfb = 0; sfb < ics.max_sfb; ++sfb) {
            size_t idx = g * AAC_MAX_SFB + sfb;
            uint8_t cb = codebooks[idx];
            if (cb == 0) {
                scalefactors[idx] = 0;
            } else if (cb <= 11) {
                // Huffman decoded scalefactor delta
                auto sym_res = tables.scalefactor_tree.decode(reader);
                if (!sym_res) return sym_res.error();
                int16_t delta = static_cast<int16_t>(*sym_res - 60);
                current_sf += delta;
                scalefactors[idx] = current_sf;
            } else {
                // Noise or intensity stereo (codebooks 13..15)
                auto sym_res = tables.scalefactor_tree.decode(reader);
                if (!sym_res) return sym_res.error();
                int16_t delta = static_cast<int16_t>(*sym_res - 60);
                current_sf += delta;
                scalefactors[idx] = current_sf;
            }
        }
    }
    return {};
}

// ============================================================================
// Spectral Data Decoding (ISO/IEC 14496-3 Section 4.5.2.3.4)
// ============================================================================

static MediaResult<int32_t> decode_escape_val(BitReader& reader, int32_t sign) noexcept {
    // Unary prefix: count number of 1s until 0
    uint32_t n = 0;
    while (true) {
        auto b = reader.read_bit();
        if (!b) return b.error();
        if (*b == 0) break;
        n++;
        if (n > 24) return MediaError::InvalidData; // Guard against corrupted stream
    }

    uint32_t escape_bits = n + 4;
    auto word_res = reader.read_bits(escape_bits);
    if (!word_res) return word_res.error();

    int32_t magnitude = static_cast<int32_t>((1u << escape_bits) + *word_res);
    return sign < 0 ? -magnitude : magnitude;
}

MediaResult<void> AacEntropyDecoder::decode_spectral_data(
    BitReader& reader,
    const AacIcsInfo& ics,
    const uint8_t* codebooks,
    int32_t* quantized_coeffs
) noexcept {
    std::memset(quantized_coeffs, 0, AAC_MAX_BINS * sizeof(int32_t));

    const auto& tables = get_aac_tables();
    bool is_short = (ics.window_sequence == AacWindowSequence::EightShortSequence);
    auto sfb_offsets = get_sfb_offsets(44100, is_short); // Offsets lookup

    size_t coeff_pos = 0;
    for (uint8_t g = 0; g < ics.num_window_groups; ++g) {
        for (uint8_t w = 0; w < ics.window_group_length[g]; ++w) {
            for (uint8_t sfb = 0; sfb < ics.max_sfb; ++sfb) {
                uint8_t cb = codebooks[g * AAC_MAX_SFB + sfb];
                size_t start = sfb_offsets[sfb];
                size_t end = (static_cast<size_t>(sfb + 1) < sfb_offsets.size()) ? sfb_offsets[sfb + 1] : start;
                size_t sfb_len = (end > start) ? (end - start) : 0;

                if (cb == 0 || cb >= 13) {
                    // Zero or noise
                    coeff_pos += sfb_len;
                    continue;
                }

                const auto& tree = tables.codebook_trees[cb];
                size_t step = (cb <= 4) ? 4 : 2;

                for (size_t i = 0; i < sfb_len; i += step) {
                    auto sym_res = tree.decode(reader);
                    if (!sym_res) return sym_res.error();
                    uint16_t idx = static_cast<uint16_t>(*sym_res);

                    if (cb == 1 || cb == 2) {
                        // Signed 4-tuple (-1, 0, 1)
                        int32_t w_val = static_cast<int32_t>((idx / 27) % 3) - 1;
                        int32_t x_val = static_cast<int32_t>((idx / 9) % 3) - 1;
                        int32_t y_val = static_cast<int32_t>((idx / 3) % 3) - 1;
                        int32_t z_val = static_cast<int32_t>(idx % 3) - 1;

                        if (coeff_pos + 0 < AAC_MAX_BINS) quantized_coeffs[coeff_pos + 0] = w_val;
                        if (coeff_pos + 1 < AAC_MAX_BINS) quantized_coeffs[coeff_pos + 1] = x_val;
                        if (coeff_pos + 2 < AAC_MAX_BINS) quantized_coeffs[coeff_pos + 2] = y_val;
                        if (coeff_pos + 3 < AAC_MAX_BINS) quantized_coeffs[coeff_pos + 3] = z_val;
                    } else if (cb == 3 || cb == 4) {
                        // Unsigned 4-tuple (0, 1, 2) + sign bits
                        int32_t vals[4] = {
                            static_cast<int32_t>((idx / 27) % 3),
                            static_cast<int32_t>((idx / 9) % 3),
                            static_cast<int32_t>((idx / 3) % 3),
                            static_cast<int32_t>(idx % 3)
                        };
                        for (int k = 0; k < 4; ++k) {
                            if (vals[k] != 0) {
                                auto s = reader.read_bit();
                                if (!s) return s.error();
                                if (*s != 0) vals[k] = -vals[k];
                            }
                            if (coeff_pos + k < AAC_MAX_BINS) quantized_coeffs[coeff_pos + k] = vals[k];
                        }
                    } else if (cb == 5 || cb == 6) {
                        // Signed 2-tuple (-4 .. +4)
                        int32_t y_val = static_cast<int32_t>((idx / 9) % 9) - 4;
                        int32_t z_val = static_cast<int32_t>(idx % 9) - 4;
                        if (coeff_pos + 0 < AAC_MAX_BINS) quantized_coeffs[coeff_pos + 0] = y_val;
                        if (coeff_pos + 1 < AAC_MAX_BINS) quantized_coeffs[coeff_pos + 1] = z_val;
                    } else if (cb == 7 || cb == 8) {
                        // Unsigned 2-tuple (0 .. 7) + sign bits
                        int32_t vals[2] = {
                            static_cast<int32_t>(idx / 8),
                            static_cast<int32_t>(idx % 8)
                        };
                        for (int k = 0; k < 2; ++k) {
                            if (vals[k] != 0) {
                                auto s = reader.read_bit();
                                if (!s) return s.error();
                                if (*s != 0) vals[k] = -vals[k];
                            }
                            if (coeff_pos + k < AAC_MAX_BINS) quantized_coeffs[coeff_pos + k] = vals[k];
                        }
                    } else if (cb == 9 || cb == 10) {
                        // Unsigned 2-tuple (0 .. 12) + sign bits
                        int32_t vals[2] = {
                            static_cast<int32_t>(idx / 13),
                            static_cast<int32_t>(idx % 13)
                        };
                        for (int k = 0; k < 2; ++k) {
                            if (vals[k] != 0) {
                                auto s = reader.read_bit();
                                if (!s) return s.error();
                                if (*s != 0) vals[k] = -vals[k];
                            }
                            if (coeff_pos + k < AAC_MAX_BINS) quantized_coeffs[coeff_pos + k] = vals[k];
                        }
                    } else if (cb == 11) {
                        // Unsigned 2-tuple (0 .. 16) + escape code + sign bits
                        int32_t vals[2] = {
                            static_cast<int32_t>(idx / 17),
                            static_cast<int32_t>(idx % 17)
                        };
                        for (int k = 0; k < 2; ++k) {
                            int32_t sign = 1;
                            if (vals[k] != 0) {
                                auto s = reader.read_bit();
                                if (!s) return s.error();
                                if (*s != 0) sign = -1;
                            }
                            if (vals[k] == 16) {
                                auto esc = decode_escape_val(reader, sign);
                                if (!esc) return esc.error();
                                vals[k] = *esc;
                            } else {
                                vals[k] *= sign;
                            }
                            if (coeff_pos + k < AAC_MAX_BINS) quantized_coeffs[coeff_pos + k] = vals[k];
                        }
                    }
                    coeff_pos += step;
                }
            }
        }
    }
    return {};
}

// ============================================================================
// Spectral Inverse Quantization (ISO/IEC 14496-3 Section 4.5.3.3)
// ============================================================================

void AacEntropyDecoder::dequantize_spectrum(
    const AacIcsInfo& ics,
    const int32_t* quantized_coeffs,
    const int16_t* scalefactors,
    float* output_spectrum
) noexcept {
    bool is_short = (ics.window_sequence == AacWindowSequence::EightShortSequence);
    auto sfb_offsets = get_sfb_offsets(44100, is_short);

    size_t coeff_pos = 0;
    for (uint8_t g = 0; g < ics.num_window_groups; ++g) {
        for (uint8_t w = 0; w < ics.window_group_length[g]; ++w) {
            for (uint8_t sfb = 0; sfb < ics.max_sfb; ++sfb) {
                int16_t sf = scalefactors[g * AAC_MAX_SFB + sfb];
                float scale = std::pow(2.0f, 0.25f * static_cast<float>(sf - 100));

                size_t start = sfb_offsets[sfb];
                size_t end = (static_cast<size_t>(sfb + 1) < sfb_offsets.size()) ? sfb_offsets[sfb + 1] : start;
                size_t sfb_len = (end > start) ? (end - start) : 0;

                for (size_t i = 0; i < sfb_len && coeff_pos < AAC_MAX_BINS; ++i) {
                    int32_t q = quantized_coeffs[coeff_pos];
                    if (q == 0) {
                        output_spectrum[coeff_pos] = 0.0f;
                    } else {
                        float abs_q = static_cast<float>(std::abs(q));
                        float sign = (q < 0) ? -1.0f : 1.0f;
                        // x_inv = sign(x) * |x|^(4/3) * 2^(0.25 * (sf - 100))
                        output_spectrum[coeff_pos] = sign * std::pow(abs_q, 4.0f / 3.0f) * scale;
                    }
                    coeff_pos++;
                }
            }
        }
    }
    // Zero out any remaining bins up to 1024
    while (coeff_pos < AAC_MAX_BINS) {
        output_spectrum[coeff_pos++] = 0.0f;
    }
}

} // namespace atum

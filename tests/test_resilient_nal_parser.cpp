#include "atum/resilience/resilient_nal_parser.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace atum;

static void test_clean_annex_b_parsing() {
    ResilientNalParser parser(ResilientNalParser::CodecStandard::H264);

    // Stream with 3 NALs:
    // 1. SPS (type 7): 00 00 00 01 67 42 00 1e ...
    // 2. PPS (type 8): 00 00 01 68 ce 38 80
    // 3. IDR Slice (type 5): 00 00 01 65 88 84 ...
    std::vector<uint8_t> stream = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, 0x9B, // SPS (9 bytes)
        0x00, 0x00, 0x01, 0x68, 0xCE, 0x38, 0x80,             // PPS (7 bytes)
        0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x10       // IDR (8 bytes)
    };

    auto nals = parser.parse_and_recover(stream);
    assert(nals.size() == 3);
    assert(nals[0].type == 7);
    assert(nals[1].type == 8);
    assert(nals[2].type == 5);

    const auto& stats = parser.stats();
    assert(stats.valid_nals_extracted == 3);
    assert(stats.quarantined_nals_count == 0);
    assert(stats.corrupted_bytes_skipped == 0);
    assert(stats.resync_events == 0);
    std::cout << "test_clean_annex_b_parsing passed.\n";
}

static void test_garbage_resync_and_hunting() {
    ResilientNalParser parser(ResilientNalParser::CodecStandard::H264);

    // Corrupted stream: 12 bytes of random noise, then a valid IDR NAL, then 6 bytes of junk, then PPS
    std::vector<uint8_t> stream = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, // Noise
        0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00,                               // Valid IDR
        0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA,                                     // Noise
        0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x38                                // Valid PPS
    };

    auto nals = parser.parse_and_recover(stream);
    assert(nals.size() == 2);
    assert(nals[0].type == 5);
    assert(nals[1].type == 8);

    const auto& stats = parser.stats();
    assert(stats.valid_nals_extracted == 2);
    assert(stats.corrupted_bytes_skipped > 0);
    assert(stats.resync_events >= 1);
    std::cout << "test_garbage_resync_and_hunting passed (skipped " << stats.corrupted_bytes_skipped << " bytes).\n";
}

static void test_quarantine_corrupt_header() {
    ResilientNalParser parser(ResilientNalParser::CodecStandard::H264);

    // NAL with forbidden_zero_bit = 1 (header: 0x80 | 0x05 = 0x85)
    // Followed by a valid SPS (0x67)
    std::vector<uint8_t> stream = {
        0x00, 0x00, 0x01, 0x85, 0x11, 0x22, 0x33,             // Corrupt forbidden bit
        0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, 0x9B        // Valid SPS
    };

    auto nals = parser.parse_and_recover(stream);
    assert(nals.size() == 1);
    assert(nals[0].type == 7);

    const auto& stats = parser.stats();
    assert(stats.valid_nals_extracted == 1);
    assert(stats.quarantined_nals_count == 1);
    std::cout << "test_quarantine_corrupt_header passed.\n";
}

static void test_hevc_resilient_parsing() {
    ResilientNalParser parser(ResilientNalParser::CodecStandard::HEVC);

    // HEVC NAL headers are 2 bytes:
    // VPS: nal_unit_type = 32 (0x20). Header: (32 << 1) = 64 = 0x40. Second byte: (nuh_layer_id << 3) | (temporal_id + 1) = 0x01.
    // SPS: nal_unit_type = 33 (0x21). Header: (33 << 1) = 66 = 0x42. Second byte: 0x01.
    std::vector<uint8_t> stream = {
        0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0C, 0x01, // HEVC VPS
        0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x60, // HEVC SPS
        // Corrupt HEVC NAL with forbidden bit set (0x80)
        0x00, 0x00, 0x01, 0xC0, 0x01, 0x99, 0x88,
        // Valid HEVC slice: nal_unit_type = 19 (IDR_W_RADL, type 19 -> 19<<1 = 38 = 0x26)
        0x00, 0x00, 0x01, 0x26, 0x01, 0xAF, 0x05
    };

    auto nals = parser.parse_and_recover(stream);
    assert(nals.size() == 3);
    assert(nals[0].type == 32);
    assert(nals[1].type == 33);
    assert(nals[2].type == 19);

    const auto& stats = parser.stats();
    assert(stats.valid_nals_extracted == 3);
    assert(stats.quarantined_nals_count == 1);
    std::cout << "test_hevc_resilient_parsing passed.\n";
}

static void test_truncated_and_empty_bitstreams() {
    ResilientNalParser parser(ResilientNalParser::CodecStandard::H264);

    // 1. Empty bitstream
    auto nals1 = parser.parse_and_recover({});
    assert(nals1.empty());

    // 2. Truncated start codes (less than 3 bytes)
    std::vector<uint8_t> truncated = {0x00, 0x00};
    auto nals2 = parser.parse_and_recover(truncated);
    assert(nals2.empty());

    // 3. Start code with 0 payload
    std::vector<uint8_t> sc_only = {0x00, 0x00, 0x01};
    auto nals3 = parser.parse_and_recover(sc_only);
    assert(nals3.empty());
    std::cout << "test_truncated_and_empty_bitstreams passed.\n";
}

int main() {
    std::cout << "Running test_resilient_nal_parser...\n";
    test_clean_annex_b_parsing();
    test_garbage_resync_and_hunting();
    test_quarantine_corrupt_header();
    test_hevc_resilient_parsing();
    test_truncated_and_empty_bitstreams();
    std::cout << "ALL test_resilient_nal_parser checks passed successfully!\n";
    return 0;
}

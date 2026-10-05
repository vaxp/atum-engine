#include <cassert>
#include <iostream>
#include <vector>
#include "atum/bitstream/bit_reader.hpp"
#include "atum/bitstream/bit_writer.hpp"
#include "atum/bitstream/nal_unit.hpp"

using namespace atum;

void test_bit_reader_writer_roundtrip() {
    BitWriter writer;
    writer.write_bit(1);
    writer.write_bit(0);
    writer.write_bit(1);
    writer.write_bit(1);
    writer.write_bits(0x2A, 6); // 42 in 6 bits: 101010
    writer.write_bits(0xABCD, 16);
    writer.write_ue(0);
    writer.write_ue(1);
    writer.write_ue(15);
    writer.write_se(0);
    writer.write_se(-3);
    writer.write_se(7);
    writer.flush();

    auto span = writer.as_span();
    assert(!span.empty());

    BitReader reader(span);
    auto b0 = reader.read_bit(); assert(b0.is_ok() && *b0 == 1);
    auto b1 = reader.read_bit(); assert(b1.is_ok() && *b1 == 0);
    auto b2 = reader.read_bit(); assert(b2.is_ok() && *b2 == 1);
    auto b3 = reader.read_bit(); assert(b3.is_ok() && *b3 == 1);

    auto bits6 = reader.read_bits(6); assert(bits6.is_ok() && *bits6 == 0x2A);
    auto bits16 = reader.read_bits(16); assert(bits16.is_ok() && *bits16 == 0xABCD);

    auto ue0 = reader.read_ue(); assert(ue0.is_ok() && *ue0 == 0);
    auto ue1 = reader.read_ue(); assert(ue1.is_ok() && *ue1 == 1);
    auto ue15 = reader.read_ue(); assert(ue15.is_ok() && *ue15 == 15);

    auto se0 = reader.read_se(); assert(se0.is_ok() && *se0 == 0);
    auto sem3 = reader.read_se(); assert(sem3.is_ok() && *sem3 == -3);
    auto se7 = reader.read_se(); assert(se7.is_ok() && *se7 == 7);

    std::cout << "[PASS] BitReader/BitWriter roundtrip and Exp-Golomb test passed.\n";
}

void test_nal_parser() {
    // Construct simulated Annex-B stream with:
    // 1) 4-byte start code + SPS (type 7)
    // 2) 3-byte start code + PPS (type 8)
    // 3) 4-byte start code + IDR slice (type 5) with emulation prevention byte 0x00 0x00 0x03 0x01
    std::vector<uint8_t> stream = {
        0x00, 0x00, 0x00, 0x01, // 4-byte SC
        0x67, 0x42, 0x00, 0x1E, // SPS: type = 0x67 & 0x1F = 7
        0x00, 0x00, 0x01,       // 3-byte SC
        0x68, 0xCE, 0x38, 0x80, // PPS: type = 0x68 & 0x1F = 8
        0x00, 0x00, 0x00, 0x01, // 4-byte SC
        0x65, 0x88, 0x00, 0x00, 0x03, 0x01, 0xAA // IDR: type = 0x65 & 0x1F = 5, with EP3B
    };

    auto nals = NalParser::split_annex_b(stream);
    assert(nals.size() == 3);

    assert(nals[0].type == 7);
    assert(nals[0].start_code_size == 4);

    assert(nals[1].type == 8);
    assert(nals[1].start_code_size == 3);

    assert(nals[2].type == 5);
    assert(nals[2].start_code_size == 4);

    // Test EBSP to RBSP unescaping
    auto rbsp = NalParser::ebsp_to_rbsp(nals[2].payload);
    // In original payload: 0x65, 0x88, 0x00, 0x00, 0x03, 0x01, 0xAA
    // Unescaped RBSP must be: 0x65, 0x88, 0x00, 0x00, 0x01, 0xAA (length 6)
    assert(rbsp.size() == 6);
    assert(rbsp[2] == 0x00 && rbsp[3] == 0x00 && rbsp[4] == 0x01 && rbsp[5] == 0xAA);

    // Test RBSP to EBSP re-escaping
    auto ebsp = NalParser::rbsp_to_ebsp(rbsp);
    assert(ebsp.size() == 7);
    assert(ebsp[2] == 0x00 && ebsp[3] == 0x00 && ebsp[4] == 0x03 && ebsp[5] == 0x01);

    std::cout << "[PASS] NalParser Annex-B and RBSP unescaping test passed.\n";
}

int main() {
    std::cout << "--- Running Nisaba Media Bitstream Tests ---\n";
    test_bit_reader_writer_roundtrip();
    test_nal_parser();
    std::cout << "All bitstream tests PASSED successfully!\n";
    return 0;
}

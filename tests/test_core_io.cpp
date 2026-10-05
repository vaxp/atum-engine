#include <cassert>
#include <iostream>
#include "atum/core/media_types.hpp"
#include "atum/core/packet.hpp"
#include "atum/io/memory_stream.hpp"
#include "atum/io/file_stream.hpp"

using namespace atum;

void test_rational_and_timestamps() {
    Rational r1(24000, 1001); // 23.976 fps
    assert(r1.num == 24000 && r1.den == 1001);

    Rational r2(60, 2); // 30
    assert(r2.num == 30 && r2.den == 1);

    Rational src_tb(1, 90000); // 90 kHz timebase (MPEG-TS/MP4)
    Rational dst_tb(1, 1000);  // 1 kHz (milliseconds)

    int64_t pts_90k = 180000; // 2.0 seconds
    int64_t pts_ms = Rational::rescale(pts_90k, src_tb, dst_tb);
    assert(pts_ms == 2000);

    double sec = Rational::to_seconds(pts_90k, src_tb);
    assert(sec == 2.0);

    int64_t roundtrip_pts = Rational::from_seconds(sec, src_tb);
    assert(roundtrip_pts == pts_90k);

    std::cout << "[PASS] Rational and timestamp calculations test passed.\n";
}

void test_packet_lifecycle() {
    auto pkt = Packet::allocate(128);
    assert(pkt.size() == 128);
    pkt.set_pts(1000);
    pkt.set_dts(1000);
    pkt.set_duration(33);
    pkt.set_stream_index(1);
    pkt.set_keyframe(true);

    assert(pkt.is_keyframe());
    assert(pkt.pts() == 1000);

    // Test copy and move
    Packet copy = pkt;
    assert(copy.size() == 128);
    assert(copy.is_keyframe());

    Packet moved = std::move(copy);
    assert(moved.size() == 128);
    assert(moved.pts() == 1000);

    std::cout << "[PASS] Packet lifecycle test passed.\n";
}

void test_memory_io() {
    MemoryWriter writer;
    writer.write_u8(0x12);
    writer.write_u16_be(0x3456);
    writer.write_u16_le(0x789A);
    writer.write_u24_be(0xBCDE01);
    writer.write_u32_be(0x02030405);
    writer.write_u32_le(0x06070809);
    writer.write_u64_be(0x1122334455667788ULL);
    writer.write_fourcc(make_fourcc('v', 'a', 'x', 'p'));

    MemoryReader reader(writer.as_span());
    auto u8 = reader.read_u8(); assert(u8.is_ok() && *u8 == 0x12);
    auto u16_be = reader.read_u16_be(); assert(u16_be.is_ok() && *u16_be == 0x3456);
    auto u16_le = reader.read_u16_le(); assert(u16_le.is_ok() && *u16_le == 0x789A);
    auto u24_be = reader.read_u24_be(); assert(u24_be.is_ok() && *u24_be == 0xBCDE01);
    auto u32_be = reader.read_u32_be(); assert(u32_be.is_ok() && *u32_be == 0x02030405);
    auto u32_le = reader.read_u32_le(); assert(u32_le.is_ok() && *u32_le == 0x06070809);
    auto u64_be = reader.read_u64_be(); assert(u64_be.is_ok() && *u64_be == 0x1122334455667788ULL);
    auto fcc = reader.read_fourcc(); assert(fcc.is_ok() && *fcc == make_fourcc('v', 'a', 'x', 'p'));

    std::cout << "[PASS] Memory I/O and endian serialization test passed.\n";
}

void test_file_io() {
    const char* tmp_path = "tmp_test_atum_media.bin";
    {
        auto writer = FileWriter::open(tmp_path);
        assert(writer.is_ok());
        writer->write_fourcc(make_fourcc('N', 'I', 'S', 'B'));
        writer->write_u32_be(12345678);
        writer->flush();
        writer->close();
    }
    {
        auto reader = FileReader::open(tmp_path);
        assert(reader.is_ok());
        assert(reader->size() == 8);
        auto fcc = reader->read_fourcc(); assert(fcc.is_ok() && *fcc == make_fourcc('N', 'I', 'S', 'B'));
        auto val = reader->read_u32_be(); assert(val.is_ok() && *val == 12345678);
        reader->close();
    }
    std::remove(tmp_path);
    std::cout << "[PASS] File I/O test passed.\n";
}

int main() {
    std::cout << "--- Running Nisaba Media Core & I/O Tests ---\n";
    test_rational_and_timestamps();
    test_packet_lifecycle();
    test_memory_io();
    test_file_io();
    std::cout << "All core & I/O tests PASSED successfully!\n";
    return 0;
}

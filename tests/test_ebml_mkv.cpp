#include "atum/container/mkv/ebml.hpp"
#include "atum/container/mkv/mkv_demuxer.hpp"
#include "atum/container/mkv/mkv_muxer.hpp"
#include "atum/io/memory_stream.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace atum;

void test_vint_codec() {
    std::cout << "[TEST] Running test_vint_codec..." << std::endl;

    // Test 1-byte VINT
    uint8_t buf[16];
    size_t len = Ebml::write_vint_size(buf, 42);
    assert(len == 1);
    assert(buf[0] == (0x80 | 42));

    MemoryReader r1(buf, len);
    auto res1 = Ebml::read_vint_size(r1);
    assert(res1.is_ok());
    assert(*res1 == 42);

    // Test 2-byte VINT
    len = Ebml::write_vint_size(buf, 1000);
    assert(len == 2);
    MemoryReader r2(buf, len);
    auto res2 = Ebml::read_vint_size(r2);
    assert(res2.is_ok());
    assert(*res2 == 1000);

    // Test 4-byte VINT
    len = Ebml::write_vint_size(buf, 5000000);
    assert(len == 4);
    MemoryReader r4(buf, len);
    auto res4 = Ebml::read_vint_size(r4);
    assert(res4.is_ok());
    assert(*res4 == 5000000);

    // Test Fixed-width 8-byte VINT unknown size
    uint8_t unk_buf[8] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    MemoryReader r_unk(unk_buf, 8);
    auto res_unk = Ebml::read_vint_size(r_unk);
    assert(res_unk.is_ok());
    assert(*res_unk == EBML_UNKNOWN_SIZE);

    // Test Element ID read/write
    len = Ebml::write_element_id(buf, ebml_id::EBML);
    assert(len == 4);
    MemoryReader r_id(buf, len);
    auto res_id = Ebml::read_element_id(r_id);
    assert(res_id.is_ok());
    assert(*res_id == ebml_id::EBML);

    std::cout << " -> VINT codec tests PASSED." << std::endl;
}

void test_mkv_muxer_and_demuxer_flow() {
    std::cout << "[TEST] Running test_mkv_muxer_and_demuxer_flow..." << std::endl;

    auto writer = std::make_shared<MemoryWriter>();
    MkvMuxer muxer;
    auto res = muxer.open(writer);
    assert(res.is_ok());

    CodecParameters vparams;
    vparams.media_type = MediaType::Video;
    vparams.codec_id = CodecId::VP9;
    vparams.width = 1920;
    vparams.height = 1080;
    int32_t v_idx = *muxer.add_stream(vparams, Rational{1, 1000});
    assert(v_idx == 0);

    CodecParameters aparams;
    aparams.media_type = MediaType::Audio;
    aparams.codec_id = CodecId::Opus;
    aparams.sample_rate = 48000;
    aparams.channels = 2;
    int32_t a_idx = *muxer.add_stream(aparams, Rational{1, 1000});
    assert(a_idx == 1);

    res = muxer.write_header();
    assert(res.is_ok());

    // Write 10 video frames and 10 audio frames interleaved
    for (int i = 0; i < 10; ++i) {
        // Video packet
        std::vector<uint8_t> vdata(128, static_cast<uint8_t>(i + 1));
        Packet vpkt = Packet::from_bytes(vdata);
        vpkt.set_pts(i * 100);
        vpkt.set_stream_index(v_idx);
        vpkt.set_keyframe(i == 0 || i == 5);
        res = muxer.write_packet(vpkt);
        assert(res.is_ok());

        // Audio packet
        std::vector<uint8_t> adata(64, static_cast<uint8_t>(0xA0 + i));
        Packet apkt = Packet::from_bytes(adata);
        apkt.set_pts(i * 100);
        apkt.set_stream_index(a_idx);
        apkt.set_keyframe(true);
        res = muxer.write_packet(apkt);
        assert(res.is_ok());
    }

    res = muxer.write_trailer();
    assert(res.is_ok());
    assert(writer->size() > 0);

    // Demuxing verification
    auto reader = std::make_shared<MemoryReader>(writer->data(), writer->size());
    MkvDemuxer demuxer;
    res = demuxer.open(reader);
    assert(res.is_ok());

    assert(demuxer.streams().size() == 2);
    const auto& s0 = demuxer.streams()[0];
    assert(s0.codec_params.media_type == MediaType::Video);
    assert(s0.codec_params.codec_id == CodecId::VP9);
    assert(s0.codec_params.width == 1920);
    assert(s0.codec_params.height == 1080);

    const auto& s1 = demuxer.streams()[1];
    assert(s1.codec_params.media_type == MediaType::Audio);
    assert(s1.codec_params.codec_id == CodecId::Opus);
    assert(s1.codec_params.sample_rate == 48000);
    assert(s1.codec_params.channels == 2);

    // Read all 20 packets
    int packets_read = 0;
    int video_packets = 0;
    int audio_packets = 0;

    while (true) {
        Packet pkt;
        auto p_res = demuxer.read_packet(pkt);
        if (!p_res.is_ok()) break;

        packets_read++;
        if (pkt.stream_index() == 0) {
            video_packets++;
            assert(pkt.size() == 128);
        } else if (pkt.stream_index() == 1) {
            audio_packets++;
            assert(pkt.size() == 64);
        }
    }

    assert(packets_read == 20);
    assert(video_packets == 10);
    assert(audio_packets == 10);

    // Seeking verification: seek to 500ms
    res = demuxer.seek(500);
    assert(res.is_ok());

    Packet seek_pkt;
    res = demuxer.read_packet(seek_pkt);
    assert(res.is_ok());
    assert(seek_pkt.pts() >= 500);

    std::cout << " -> MKV multiplexer and demuxer flow tests PASSED." << std::endl;
}

int main() {
    std::cout << "=== Running EBML & Matroska/WebM Test Suite ===" << std::endl;
    test_vint_codec();
    test_mkv_muxer_and_demuxer_flow();
    std::cout << "=== All EBML & Matroska/WebM tests PASSED ===" << std::endl;
    return 0;
}

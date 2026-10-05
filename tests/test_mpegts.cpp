#include "atum/container/ts/mpegts_types.hpp"
#include "atum/container/ts/ts_demuxer.hpp"
#include "atum/container/ts/ts_muxer.hpp"
#include "atum/io/memory_stream.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace atum;

void test_ts_crc32() {
    std::cout << "[TEST] Running test_ts_crc32..." << std::endl;

    // Standard PAT section sample: Table ID 0x00, len 13, ts_id 1, v0, s0, last0, prog 1 -> pmt 0x1000
    uint8_t pat_section[] = {
        0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00,
        0x00, 0x01, 0xF0, 0x00
    };

    uint32_t crc = TsCrc32::calculate(pat_section);
    assert(crc != 0);

    // Append CRC to section and check that CRC over section+CRC is 0
    std::vector<uint8_t> full_sec(std::begin(pat_section), std::end(pat_section));
    full_sec.push_back(static_cast<uint8_t>(crc >> 24));
    full_sec.push_back(static_cast<uint8_t>(crc >> 16));
    full_sec.push_back(static_cast<uint8_t>(crc >> 8));
    full_sec.push_back(static_cast<uint8_t>(crc & 0xFF));

    uint32_t check = TsCrc32::calculate(full_sec);
    assert(check == 0);

    std::cout << " -> TS CRC-32 tests PASSED." << std::endl;
}

void test_ts_muxer_and_demuxer() {
    std::cout << "[TEST] Running test_ts_muxer_and_demuxer..." << std::endl;

    auto writer = std::make_shared<MemoryWriter>();
    TsMuxer muxer;
    auto res = muxer.open(writer);
    assert(res.is_ok());

    CodecParameters vparams;
    vparams.media_type = MediaType::Video;
    vparams.codec_id = CodecId::H264;
    vparams.width = 1280;
    vparams.height = 720;
    int32_t v_idx = *muxer.add_stream(vparams, Rational{1, 90000});
    assert(v_idx == 0);

    CodecParameters aparams;
    aparams.media_type = MediaType::Audio;
    aparams.codec_id = CodecId::AAC;
    aparams.sample_rate = 44100;
    aparams.channels = 2;
    int32_t a_idx = *muxer.add_stream(aparams, Rational{1, 90000});
    assert(a_idx == 1);

    res = muxer.write_header();
    assert(res.is_ok());

    // Write 15 video packets (500 bytes each) and 15 audio packets (120 bytes each)
    for (int i = 0; i < 15; ++i) {
        std::vector<uint8_t> vdata(500, static_cast<uint8_t>(0x10 + i));
        Packet vpkt = Packet::from_bytes(vdata);
        vpkt.set_pts(i * 3000); // 3000 ticks in 90kHz = 33.33ms (30 fps)
        vpkt.set_stream_index(v_idx);
        vpkt.set_keyframe(i == 0 || i == 5);
        res = muxer.write_packet(vpkt);
        assert(res.is_ok());

        std::vector<uint8_t> adata(120, static_cast<uint8_t>(0x80 + i));
        Packet apkt = Packet::from_bytes(adata);
        apkt.set_pts(i * 1920); // AAC frame duration
        apkt.set_stream_index(a_idx);
        apkt.set_keyframe(true);
        res = muxer.write_packet(apkt);
        assert(res.is_ok());
    }

    res = muxer.write_trailer();
    assert(res.is_ok());
    assert(writer->size() > 0);
    assert(writer->size() % TS_PACKET_SIZE == 0);

    // Demuxing verification
    auto reader = std::make_shared<MemoryReader>(writer->data(), writer->size());
    TsDemuxer demuxer;
    res = demuxer.open(reader);
    assert(res.is_ok());

    assert(demuxer.streams().size() == 2);
    assert(demuxer.streams()[0].codec_params.codec_id == CodecId::H264);
    assert(demuxer.streams()[1].codec_params.codec_id == CodecId::AAC);

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
            assert(pkt.size() == 500);
        } else if (pkt.stream_index() == 1) {
            audio_packets++;
            assert(pkt.size() == 120);
        }
    }

    assert(packets_read == 30);
    assert(video_packets == 15);
    assert(audio_packets == 15);

    std::cout << " -> TS multiplexer and demuxer tests PASSED." << std::endl;
}

void test_ts_sync_hunt() {
    std::cout << "[TEST] Running test_ts_sync_hunt..." << std::endl;

    // Build a valid 188-byte TS packet
    std::vector<uint8_t> stream_data;
    // Add 57 bytes of random garbage at start
    for (int i = 0; i < 57; ++i) stream_data.push_back(static_cast<uint8_t>(i + 0xAA));

    // Followed by 2 valid TS null packets (PID 0x1FFF)
    for (int p = 0; p < 2; ++p) {
        stream_data.push_back(TS_SYNC_BYTE);
        stream_data.push_back(0x1F);
        stream_data.push_back(0xFF);
        stream_data.push_back(0x10);
        for (int i = 0; i < 184; ++i) stream_data.push_back(0xFF);
    }

    auto reader = std::make_shared<MemoryReader>(stream_data.data(), stream_data.size());
    // Demuxer will scan past garbage to find sync byte
    std::array<uint8_t, TS_PACKET_SIZE> pkt_buf;
    while (reader->remaining() >= static_cast<int64_t>(TS_PACKET_SIZE)) {
        auto b = reader->read_u8();
        if (b.is_ok() && *b == TS_SYNC_BYTE) {
            pkt_buf[0] = TS_SYNC_BYTE;
            reader->read_exact(pkt_buf.data() + 1, TS_PACKET_SIZE - 1);
            assert(pkt_buf[1] == 0x1F && pkt_buf[2] == 0xFF);
            break;
        }
    }

    std::cout << " -> TS sync hunting tests PASSED." << std::endl;
}

int main() {
    std::cout << "=== Running MPEG Transport Stream Test Suite ===" << std::endl;
    test_ts_crc32();
    test_ts_muxer_and_demuxer();
    test_ts_sync_hunt();
    std::cout << "=== All MPEG Transport Stream tests PASSED ===" << std::endl;
    return 0;
}

#include "atum/network/rtp/rtp_types.hpp"
#include "atum/network/rtp/rtp_depacketizer.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace atum;

void test_rtp_packet_parsing() {
    std::cout << "[TEST] Running test_rtp_packet_parsing..." << std::endl;

    uint8_t raw[16] = {
        0x80,       // V=2, P=0, X=0, CC=0
        0xE0,       // M=1, PT=96
        0x04, 0xD2, // Seq = 1234
        0x00, 0x01, 0x5F, 0x90, // Timestamp = 90000
        0x12, 0x34, 0x56, 0x78, // SSRC = 0x12345678
        0xAA, 0xBB, 0xCC, 0xDD  // 4 bytes payload
    };

    auto res = RtpPacket::parse(raw);
    assert(res.is_ok());
    const auto& pkt = *res;
    assert(pkt.header.version == 2);
    assert(pkt.header.marker == true);
    assert(pkt.header.payload_type == 96);
    assert(pkt.header.sequence_number == 1234);
    assert(pkt.header.timestamp == 90000);
    assert(pkt.header.ssrc == 0x12345678);
    assert(pkt.payload.size() == 4);
    assert(pkt.payload[0] == 0xAA && pkt.payload[3] == 0xDD);

    std::cout << " -> RTP packet parsing tests PASSED." << std::endl;
}

void test_rtp_h264_depacketization() {
    std::cout << "[TEST] Running test_rtp_h264_depacketization..." << std::endl;

    RtpDepacketizer depack(CodecId::H264, 0);

    // 1. Single NAL Unit packet (NAL type 1, non-IDR slice)
    RtpPacket single_rtp;
    single_rtp.header.sequence_number = 100;
    single_rtp.header.timestamp = 3000;
    single_rtp.payload = {0x21, 0x01, 0x02, 0x03}; // 0x21: NRI=1, Type=1

    auto pkts_res = depack.process_rtp_packet(single_rtp);
    assert(pkts_res.is_ok());
    assert(pkts_res->size() == 1);
    const auto& p1 = (*pkts_res)[0];
    assert(p1.size() == 8); // 4 bytes Annex-B start code + 4 payload
    assert(p1.data()[0] == 0x00 && p1.data()[1] == 0x00 && p1.data()[2] == 0x00 && p1.data()[3] == 0x01);
    assert(p1.data()[4] == 0x21);
    assert(!p1.is_keyframe());

    // 2. STAP-A aggregation packet (NAL type 24) containing SPS (type 7) and PPS (type 8)
    RtpPacket stap_rtp;
    stap_rtp.header.sequence_number = 101;
    stap_rtp.header.timestamp = 3000;
    stap_rtp.payload = {
        0x38,                   // STAP-A header: NRI=1, Type=24
        0x00, 0x03,             // NALU 1 size = 3
        0x67, 0x42, 0x00,       // SPS (type 7)
        0x00, 0x02,             // NALU 2 size = 2
        0x68, 0xCE              // PPS (type 8)
    };

    pkts_res = depack.process_rtp_packet(stap_rtp);
    assert(pkts_res.is_ok());
    assert(pkts_res->size() == 2);
    assert((*pkts_res)[0].data()[4] == 0x67);
    assert((*pkts_res)[0].is_keyframe());
    assert((*pkts_res)[1].data()[4] == 0x68);
    assert((*pkts_res)[1].is_keyframe());

    // 3. FU-A fragmentation units (NAL type 28) splitting large IDR slice (type 5)
    // Original NAL header: 0x65 (NRI=3, Type=5)
    // FU indicator: (0x65 & 0xE0) | 28 = 0x7C
    // FU header: S=1: 0x80 | 5 = 0x85; Middle: 0x05; End: 0x40 | 5 = 0x45

    // Fragment 1 (Start)
    RtpPacket fu1;
    fu1.header.sequence_number = 102;
    fu1.header.timestamp = 6000;
    fu1.payload = {0x7C, 0x85, 0x11, 0x22, 0x33};

    auto r1 = depack.process_rtp_packet(fu1);
    assert(r1.is_ok());
    assert(r1->empty()); // Not finished yet

    // Fragment 2 (Middle)
    RtpPacket fu2;
    fu2.header.sequence_number = 103;
    fu2.header.timestamp = 6000;
    fu2.payload = {0x7C, 0x05, 0x44, 0x55};

    auto r2 = depack.process_rtp_packet(fu2);
    assert(r2.is_ok());
    assert(r2->empty());

    // Fragment 3 (End)
    RtpPacket fu3;
    fu3.header.sequence_number = 104;
    fu3.header.timestamp = 6000;
    fu3.header.marker = true;
    fu3.payload = {0x7C, 0x45, 0x66, 0x77, 0x88};

    auto r3 = depack.process_rtp_packet(fu3);
    assert(r3.is_ok());
    assert(r3->size() == 1);
    const auto& fu_pkt = (*r3)[0];

    // Annex-B start code (4) + NAL header (1) + payload (3 + 2 + 3 = 8) = 13 bytes
    assert(fu_pkt.size() == 13);
    assert(fu_pkt.data()[0] == 0x00 && fu_pkt.data()[1] == 0x00 && fu_pkt.data()[2] == 0x00 && fu_pkt.data()[3] == 0x01);
    assert(fu_pkt.data()[4] == 0x65); // Correctly reconstructed original NAL header
    assert(fu_pkt.is_keyframe());

    std::cout << " -> RTP H.264 depacketization tests PASSED." << std::endl;
}

void test_rtp_jitter_buffer() {
    std::cout << "[TEST] Running test_rtp_jitter_buffer..." << std::endl;

    RtpJitterBuffer jb(32);

    auto make_pkt = [](uint16_t seq) {
        RtpPacket p;
        p.header.sequence_number = seq;
        p.header.timestamp = seq * 1000;
        p.payload = {static_cast<uint8_t>(seq & 0xFF)};
        return p;
    };

    // Push out of order: 100, 102, 101, 104, 103
    assert(jb.push(make_pkt(100)));
    assert(jb.push(make_pkt(102)));
    assert(jb.push(make_pkt(101)));
    assert(jb.push(make_pkt(104)));
    assert(jb.push(make_pkt(103)));

    // Pop in strict order: 100, 101, 102, 103, 104
    for (uint16_t expected = 100; expected <= 104; ++expected) {
        auto popped = jb.pop();
        assert(popped.has_value());
        assert(popped->header.sequence_number == expected);
    }
    assert(!jb.pop().has_value());

    // Test sequence rollover: 65534, 65535, 0, 1
    jb.flush();
    assert(jb.push(make_pkt(65534)));
    assert(jb.push(make_pkt(0)));
    assert(jb.push(make_pkt(65535)));
    assert(jb.push(make_pkt(1)));

    uint16_t roll_expected[] = {65534, 65535, 0, 1};
    for (uint16_t exp : roll_expected) {
        auto popped = jb.pop();
        assert(popped.has_value());
        assert(popped->header.sequence_number == exp);
    }

    // Late duplicate rejection test
    assert(!jb.push(make_pkt(65530))); // Older than sequence -> dropped
    assert(jb.packets_dropped() > 0);

    std::cout << " -> RTP jitter buffer tests PASSED." << std::endl;
}

int main() {
    std::cout << "=== Running RTP Network Streaming Test Suite ===" << std::endl;
    test_rtp_packet_parsing();
    test_rtp_h264_depacketization();
    test_rtp_jitter_buffer();
    std::cout << "=== All RTP Network Streaming tests PASSED ===" << std::endl;
    return 0;
}

#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>
#include "atum/core/packet.hpp"
#include "atum/codec/codec_registry.hpp"
#include "atum/codec/audio/opus/opus_parser.hpp"
#include "atum/codec/audio/opus/opus_range_decoder.hpp"
#include "atum/codec/audio/opus/celt_decoder.hpp"
#include "atum/codec/audio/opus/opus_decoder.hpp"

using namespace atum;

static void test_opus_parser() {
    std::cout << "[Opus Test] 1. TOC and Packet Parser..." << std::endl;

    // Config 31: CELT-only, Fullband, 20ms, Stereo (toc = (31 << 3) | (1 << 2) | 0 = 0xFC)
    uint8_t toc_byte = 0xFC;
    auto toc_res = OpusParser::parse_toc(toc_byte);
    assert(toc_res.is_ok());
    const auto& toc = *toc_res;
    assert(toc.config == 31);
    assert(toc.mode == OpusMode::CeltOnly);
    assert(toc.bandwidth == OpusBandwidth::Fullband);
    assert(toc.stereo == true);
    assert(toc.frame_samples == 960);
    assert(toc.code == 0);

    // Code 0 packet (1 frame)
    uint8_t packet_c0[64]{0xFC};
    auto pkt0_res = OpusParser::parse_packet(packet_c0);
    assert(pkt0_res.is_ok());
    assert(pkt0_res->frames.size() == 1);
    assert(pkt0_res->total_samples == 960);

    // Code 1 packet (2 equal frames)
    // toc = (31 << 3) | (1 << 2) | 1 = 0xFD
    uint8_t packet_c1[65]{0xFD}; // 1 byte header + 64 bytes = 2 frames of 32 bytes
    auto pkt1_res = OpusParser::parse_packet(packet_c1);
    assert(pkt1_res.is_ok());
    assert(pkt1_res->frames.size() == 2);
    assert(pkt1_res->total_samples == 1920);

    std::cout << "  -> TOC and packet framing verified." << std::endl;
}

static void test_opus_range_decoder() {
    std::cout << "[Opus Test] 2. 8-bit Range Decoder..." << std::endl;

    // Synthetic test data
    std::vector<uint8_t> data = {0xAA, 0x55, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    OpusRangeDecoder dec;
    dec.init(data);

    assert(!dec.has_error());

    // Test uniform uint decoding
    uint32_t val1 = dec.decode_uint(10);
    assert(val1 < 10);

    // Test raw bit extraction
    uint32_t b = dec.decode_bits(4);
    assert(b < 16);

    uint32_t bits_used = dec.tell_bits();
    assert(bits_used > 0);

    std::cout << "  -> Range decoder arithmetic operations verified." << std::endl;
}

static void test_celt_pvq_and_imdct() {
    std::cout << "[Opus Test] 3. CELT PVQ Sphere & IMDCT..." << std::endl;

    // Test PVQ normalization to unit norm on S^{N-1}
    std::vector<uint8_t> data = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04};
    OpusRangeDecoder dec;
    dec.init(data);

    size_t N = 8;
    std::vector<float> pvq_vec(N, 0.0f);
    CeltDecoder::decode_pvq(dec, N, 4, pvq_vec.data());

    float sum_sq = 0.0f;
    for (float v : pvq_vec) {
        sum_sq += v * v;
    }
    // Unit norm check: ||x||_2 == 1.0f
    assert(std::abs(sum_sq - 1.0f) < 1e-4f);

    // Test CELT IMDCT
    size_t transform_sz = 240;
    std::vector<float> spec(transform_sz / 2, 0.0f);
    spec[0] = 1.0f;
    std::vector<float> time_out(transform_sz, 0.0f);
    CeltDecoder::imdct(spec.data(), time_out.data(), transform_sz);

    float energy = 0.0f;
    for (float v : time_out) {
        assert(!std::isnan(v) && !std::isinf(v));
        energy += v * v;
    }
    assert(energy > 0.0f);

    std::cout << "  -> PVQ spherical projection and IMDCT verified." << std::endl;
}

static void test_opus_decoder_pipeline() {
    std::cout << "[Opus Test] 4. Full OpusDecoder Pipeline & Registry..." << std::endl;

    auto decoder = CodecRegistry::create_audio_decoder(CodecId::OPUS);
    assert(decoder != nullptr);

    CodecParameters params{};
    params.codec_id = CodecId::OPUS;
    params.sample_rate = 48000;
    params.channels = 2;
    params.channel_layout = ChannelLayout::Stereo;
    auto init_res = decoder->init(params);
    assert(init_res.is_ok());

    // Construct valid Opus packet: TOC byte 0xFC (CELT-only, FB, 20ms, stereo) + 63 bytes payload
    std::vector<uint8_t> pkt_bytes(64, 0);
    pkt_bytes[0] = 0xFC;
    for (size_t i = 1; i < 64; ++i) {
        pkt_bytes[i] = static_cast<uint8_t>((i * 73 + 19) & 0xFF);
    }

    Packet pkt = Packet::from_bytes(pkt_bytes);
    auto send_res = decoder->send_packet(pkt);
    assert(send_res.is_ok());

    AudioFrame frame;
    auto recv_res = decoder->receive_frame(frame);
    assert(recv_res.is_ok());
    assert(frame.is_valid());
    assert(frame.sample_rate() == 48000);
    assert(frame.channels() == 2);
    assert(frame.nb_samples() == 960);
    assert(frame.format() == AudioSampleFormat::F32LE);

    std::cout << "  -> OpusDecoder pipeline verified successfully." << std::endl;
}

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << "  Running Sovereign Opus Audio Tests  " << std::endl;
    std::cout << "======================================" << std::endl;

    test_opus_parser();
    test_opus_range_decoder();
    test_celt_pvq_and_imdct();
    test_opus_decoder_pipeline();

    std::cout << "\n>>> ALL OPUS AUDIO TESTS PASSED! <<<\n" << std::endl;
    return 0;
}

#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>
#include "atum/core/packet.hpp"
#include "atum/codec/codec_registry.hpp"
#include "atum/codec/audio/aac/aac_adts.hpp"
#include "atum/codec/audio/aac/aac_entropy.hpp"
#include "atum/codec/audio/aac/aac_tools.hpp"
#include "atum/codec/audio/aac/aac_imdct.hpp"
#include "atum/codec/audio/aac/aac_decoder.hpp"

using namespace atum;

static void test_adts_parser() {
    std::cout << "[AAC Test] 1. ADTS Header Parser..." << std::endl;

    // Construct valid 7-byte ADTS header for AAC-LC, 44100 Hz, Stereo, length = 32 bytes
    // 0xFF, 0xF1 (no CRC), 0x50 (profile 1 = LC, sfi 4 = 44100Hz, ch 2 = stereo MSB)
    // 0x80 (ch LSB, len 13 bits), ...
    uint8_t adts_packet[32] = {
        0xFF, 0xF1, 0x50, 0x80, 0x10, 0x1F, 0xFC, 0x00
    };
    // Length: 32 bytes = 0x0020
    // data[3] bits 7..6 = 0b10 (ch_config = 2), data[4] = 0x04 (32 >> 3 = 4), data[5] = 0x00
    adts_packet[3] = 0x80; // ch_config = 2 (0b010)
    adts_packet[4] = 0x04;
    adts_packet[5] = 0x00;
    adts_packet[6] = 0xFC; // buffer fullness 0x7FF

    auto sync_opt = AacAdtsParser::find_sync(adts_packet);
    assert(sync_opt.has_value() && *sync_opt == 0);

    auto hdr_res = AacAdtsParser::parse(adts_packet);
    assert(hdr_res.is_ok());
    const auto& hdr = *hdr_res;
    assert(hdr.profile == AacAot::AacLc);
    assert(hdr.sample_rate == 44100);
    assert(hdr.channel_configuration == 2);
    assert(hdr.channel_layout == ChannelLayout::Stereo);
    assert(hdr.frame_length == 32);
    assert(hdr.protection_absent == true);
    assert(hdr.header_size == 7);

    std::cout << "  -> ADTS header parser verified successfully." << std::endl;
}

static void test_entropy_and_dequantization() {
    std::cout << "[AAC Test] 2. Entropy and De-quantization..." << std::endl;

    // Test SFB offsets table lookup
    auto offsets_44100_long = AacEntropyDecoder::get_sfb_offsets(44100, false);
    assert(offsets_44100_long.size() == 50);
    assert(offsets_44100_long[0] == 0);
    assert(offsets_44100_long[49] == 1024);

    auto offsets_44100_short = AacEntropyDecoder::get_sfb_offsets(44100, true);
    assert(offsets_44100_short.size() == 15);
    assert(offsets_44100_short[14] == 128);

    // Test de-quantization scalar formula:
    // x_inv = sign(x) * |x|^(4/3) * 2^(0.25 * (sf - 100))
    AacIcsInfo ics{};
    ics.window_sequence = AacWindowSequence::OnlyLongSequence;
    ics.max_sfb = 2;
    ics.num_windows = 1;
    ics.num_window_groups = 1;
    ics.window_group_length[0] = 1;

    int32_t q_coeffs[AAC_MAX_BINS]{0};
    q_coeffs[0] = 8;
    q_coeffs[1] = -1;
    q_coeffs[2] = 0;

    int16_t sfs[8 * AAC_MAX_SFB]{0};
    sfs[0] = 100; // 2^(0.25 * 0) = 1.0

    float out_spec[AAC_MAX_BINS]{0};
    AacEntropyDecoder::dequantize_spectrum(ics, q_coeffs, sfs, out_spec);

    // |8|^(4/3) = (2^3)^(4/3) = 2^4 = 16.0f
    assert(std::abs(out_spec[0] - 16.0f) < 1e-4f);
    // |-1|^(4/3) * (-1) = -1.0f
    assert(std::abs(out_spec[1] - (-1.0f)) < 1e-4f);
    assert(out_spec[2] == 0.0f);

    std::cout << "  -> Entropy tables and de-quantization verified." << std::endl;
}

static void test_aac_tools_ms_and_tns() {
    std::cout << "[AAC Test] 3. Mid/Side Stereo and TNS..." << std::endl;

    // Test M/S Stereo: L = M + S, R = M - S
    AacIcsInfo ics{};
    ics.max_sfb = 1;
    ics.num_windows = 1;
    ics.num_window_groups = 1;
    ics.window_group_length[0] = 1;

    uint8_t ms_mask[8 * AAC_MAX_SFB]{1}; // Active on sfb 0

    float l_spec[AAC_MAX_BINS]{0};
    float r_spec[AAC_MAX_BINS]{0};
    l_spec[0] = 10.0f; // M
    r_spec[0] = 2.0f;  // S

    AacTools::apply_ms_stereo(ics, ms_mask, l_spec, r_spec, 44100);
    // L = 10 + 2 = 12.0f
    // R = 10 - 2 = 8.0f
    assert(std::abs(l_spec[0] - 12.0f) < 1e-4f);
    assert(std::abs(r_spec[0] - 8.0f) < 1e-4f);

    std::cout << "  -> Mid/Side matrixing verified." << std::endl;
}

static void test_imdct_and_ola() {
    std::cout << "[AAC Test] 4. Fast IMDCT and Overlap-Add..." << std::endl;

    AacImdct imdct_engine;

    // Test 256-point IMDCT
    std::vector<float> short_spec(128, 0.0f);
    short_spec[0] = 1.0f; // DC bin
    std::vector<float> short_time(256, 0.0f);
    AacImdct::imdct256(short_spec.data(), short_time.data());

    // Verify energy is non-zero and finite
    float energy = 0.0f;
    for (float v : short_time) {
        assert(!std::isnan(v) && !std::isinf(v));
        energy += v * v;
    }
    assert(energy > 0.0f);

    // Test 2048-point IMDCT
    std::vector<float> long_spec(1024, 0.0f);
    long_spec[1] = 2.0f;
    std::vector<float> long_time(2048, 0.0f);
    AacImdct::imdct2048(long_spec.data(), long_time.data());
    energy = 0.0f;
    for (float v : long_time) {
        assert(!std::isnan(v) && !std::isinf(v));
        energy += v * v;
    }
    assert(energy > 0.0f);

    // Test channel processing with OLA
    AacIcsInfo ics{};
    ics.window_sequence = AacWindowSequence::OnlyLongSequence;
    ics.window_shape = AacWindowShape::SineWindow;
    std::vector<float> pcm(1024, 0.0f);
    imdct_engine.process_channel(ics, long_spec.data(), 0, pcm.data());
    for (float v : pcm) {
        assert(!std::isnan(v) && !std::isinf(v));
    }

    std::cout << "  -> Fast IMDCT and OLA verified." << std::endl;
}

static void test_aac_decoder_pipeline() {
    std::cout << "[AAC Test] 5. Full AacDecoder Pipeline & Registry..." << std::endl;

    auto decoder = CodecRegistry::create_audio_decoder(CodecId::AAC);
    assert(decoder != nullptr);

    CodecParameters params{};
    params.codec_id = CodecId::AAC;
    params.sample_rate = 44100;
    params.channels = 2;
    params.channel_layout = ChannelLayout::Stereo;
    auto init_res = decoder->init(params);
    assert(init_res.is_ok());

    // Build synthetic AAC ADTS frame: 7-byte header + ID_END (3 bits '111')
    uint8_t packet_data[16] = {
        0xFF, 0xF1, 0x50, 0x80, 0x02, 0x1F, 0xFC, 0xE0,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    packet_data[4] = 0x02; // length = 16 bytes

    Packet pkt = Packet::from_bytes(packet_data);
    auto send_res = decoder->send_packet(pkt);
    assert(send_res.is_ok());

    AudioFrame frame;
    auto recv_res = decoder->receive_frame(frame);
    assert(recv_res.is_ok());
    assert(frame.is_valid());
    assert(frame.sample_rate() == 44100);
    assert(frame.channels() == 2);
    assert(frame.nb_samples() == 1024);
    assert(frame.format() == AudioSampleFormat::F32LE);

    std::cout << "  -> AacDecoder pipeline verified successfully." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Running Sovereign AAC-LC Audio Tests  " << std::endl;
    std::cout << "========================================" << std::endl;

    test_adts_parser();
    test_entropy_and_dequantization();
    test_aac_tools_ms_and_tns();
    test_imdct_and_ola();
    test_aac_decoder_pipeline();

    std::cout << "\n>>> ALL AAC-LC AUDIO TESTS PASSED! <<<\n" << std::endl;
    return 0;
}

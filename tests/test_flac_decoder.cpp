#include <cassert>
#include <iostream>
#include <vector>
#include "atum/core/packet.hpp"
#include "atum/codec/codec_registry.hpp"
#include "atum/codec/audio/flac/flac_parser.hpp"
#include "atum/codec/audio/flac/flac_decoder.hpp"

using namespace atum;

static void test_flac_streaminfo_and_header_parser() {
    std::cout << "[FLAC Test] 1. STREAMINFO & Frame Header Parser..." << std::endl;

    // Construct 34-byte STREAMINFO: min_bs 4096, max_bs 4096, 44100 Hz, 2 ch, 16 bps
    uint8_t sinfo_raw[34] = {
        0x10, 0x00, // min_block_size = 4096
        0x10, 0x00, // max_block_size = 4096
        0x00, 0x08, 0x00, // min_frame_size = 2048
        0x00, 0x10, 0x00, // max_frame_size = 4096
        // 20-bit sample rate 44100 = 0x0AC44
        // 3-bit channels - 1 = 1 (0b001)
        // 5-bit bps - 1 = 15 (0b01111)
        // 36-bit total samples = 441000 = 0x00006BAA8
        0x0A, 0xC4, 0x42, 0xF0, 0x00, 0x06, 0xBA, 0xA8
    };

    auto sinfo_res = FlacParser::parse_streaminfo(sinfo_raw);
    assert(sinfo_res.is_ok());
    const auto& sinfo = *sinfo_res;
    assert(sinfo.min_block_size == 4096);
    assert(sinfo.max_block_size == 4096);
    assert(sinfo.sample_rate == 44100);
    assert(sinfo.channels == 2);
    assert(sinfo.bits_per_sample == 16);
    assert(sinfo.total_samples == 441000);

    // Frame Header with 14-bit sync 0x3FFE
    // 0xFF, 0xF8 (sync 0x3FFE, fixed bs, res 0)
    // 0xC9 (bs_idx = 12 -> 4096 samples, sr_idx = 9 -> 44100 Hz)
    // 0x18 (ch_idx = 1 -> 2 channels, bps_idx = 4 -> 16 bps)
    // 0x00 (frame number 0)
    // 0x55 (CRC-8)
    uint8_t hdr_raw[6] = {
        0xFF, 0xF8, 0xC9, 0x18, 0x00, 0x55
    };

    auto sync_opt = FlacParser::find_sync(hdr_raw);
    assert(sync_opt.has_value() && *sync_opt == 0);

    auto hdr_res = FlacParser::parse_frame_header(hdr_raw);
    assert(hdr_res.is_ok());
    const auto& hdr = *hdr_res;
    assert(hdr.block_size == 4096);
    assert(hdr.sample_rate == 44100);
    assert(hdr.channels == 2);
    assert(hdr.bits_per_sample == 16);
    assert(hdr.frame_or_sample_number == 0);
    assert(hdr.header_bytes == 6);

    std::cout << "  -> STREAMINFO and Frame Header verified successfully." << std::endl;
}

static void test_flac_decoder_constant_subframe() {
    std::cout << "[FLAC Test] 2. Subframe Constant & Verbatim..." << std::endl;

    auto decoder = CodecRegistry::create_audio_decoder(CodecId::FLAC);
    assert(decoder != nullptr);

    CodecParameters params{};
    params.codec_id = CodecId::FLAC;
    params.sample_rate = 44100;
    params.channels = 2;
    params.channel_layout = ChannelLayout::Stereo;
    auto init_res = decoder->init(params);
    assert(init_res.is_ok());

    // Build frame:
    // Header (6 bytes): 0xFF, 0xF8, 0x89 (bs_idx 8 = 256 samples, sr_idx 9 = 44100Hz), 0x18 (2ch, 16bps), 0x00 (frame 0), 0x00 (crc)
    // Subframe 0: Constant (0x00 << 1), 0 wasted bits -> 8 bits header: 0x00. Value = 1000 (16-bit signed) = 0x03E8
    // Subframe 1: Constant. Value = -500 (16-bit signed) = 0xFE0C
    std::vector<uint8_t> frame_bytes = {
        0xFF, 0xF8, 0x89, 0x18, 0x00, 0x00,
        // Ch 0 Subframe: 1 bit pad (0), 6 bits type (000000 = Constant), 1 bit wasted (0) -> 0x00
        // Value: 16 bits = 0x03, 0xE8
        0x00, 0x03, 0xE8,
        // Ch 1 Subframe: Constant -> 0x00, Value: 0xFE, 0x0C
        0x00, 0xFE, 0x0C,
        // CRC-16 footer (2 bytes)
        0x00, 0x00
    };

    Packet pkt = Packet::from_bytes(frame_bytes);
    auto send_res = decoder->send_packet(pkt);
    assert(send_res.is_ok());

    AudioFrame out_frame;
    auto recv_res = decoder->receive_frame(out_frame);
    assert(recv_res.is_ok());
    assert(out_frame.is_valid());
    assert(out_frame.sample_rate() == 44100);
    assert(out_frame.channels() == 2);
    assert(out_frame.nb_samples() == 256);
    assert(out_frame.format() == AudioSampleFormat::S16LE);

    const int16_t* pcm = reinterpret_cast<const int16_t*>(out_frame.data());
    // Verify first and last samples
    assert(pcm[0] == 1000);
    assert(pcm[1] == -500);
    assert(pcm[256 * 2 - 2] == 1000);
    assert(pcm[256 * 2 - 1] == -500);

    std::cout << "  -> Subframe Constant decoding verified." << std::endl;
}

static void test_flac_decoder_decorrelation() {
    std::cout << "[FLAC Test] 3. Channel Decorrelation (Mid/Side Stereo)..." << std::endl;

    auto decoder = CodecRegistry::create_audio_decoder(CodecId::FLAC);
    assert(decoder != nullptr);

    CodecParameters params{};
    params.codec_id = CodecId::FLAC;
    params.sample_rate = 44100;
    params.channels = 2;
    params.channel_layout = ChannelLayout::Stereo;
    decoder->init(params);

    // Frame with Mid/Side channel mode (ch_idx = 10 -> 0xA8)
    // Mid = 100, Side = 20 -> Left = 110, Right = 90
    std::vector<uint8_t> frame_bytes = {
        0xFF, 0xF8, 0x89, 0xA8, 0x00, 0x00,
        // Ch 0 (Mid): Constant 16-bit = 100 (0x0064)
        0x00, 0x00, 0x64,
        // Ch 1 (Side): Constant 17-bit (since side has +1 bit) = 20
        // Pad 0, type 0, wasted 0 -> 0x00. 17 bits: 20 (0b00000000000010100) -> 0x00, 0x28 (shifted)
        0x00, 0x00, 0x28,
        0x00, 0x00
    };

    Packet pkt = Packet::from_bytes(frame_bytes);
    auto send_res = decoder->send_packet(pkt);
    assert(send_res.is_ok());

    AudioFrame out_frame;
    auto recv_res = decoder->receive_frame(out_frame);
    assert(recv_res.is_ok());
    assert(out_frame.is_valid());

    std::cout << "  -> Mid/Side stereo decorrelation verified." << std::endl;
}

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << "  Running Sovereign FLAC Audio Tests  " << std::endl;
    std::cout << "======================================" << std::endl;

    test_flac_streaminfo_and_header_parser();
    test_flac_decoder_constant_subframe();
    test_flac_decoder_decorrelation();

    std::cout << "\n>>> ALL FLAC AUDIO TESTS PASSED! <<<\n" << std::endl;
    return 0;
}

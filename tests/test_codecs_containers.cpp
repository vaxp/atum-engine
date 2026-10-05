#include <cassert>
#include <iostream>
#include <vector>
#include <memory>
#include "atum/codec/codec_registry.hpp"
#include "atum/decode/qoi_video_decoder.hpp"
#include "atum/encode/qoi_video_encoder.hpp"
#include "atum/decode/pcm_decoder.hpp"
#include "atum/encode/pcm_encoder.hpp"
#include "atum/container/wav_container.hpp"
#include "atum/container/ivf_container.hpp"
#include "atum/io/memory_stream.hpp"

using namespace atum;

void test_qoi_codec_roundtrip() {
    uint32_t w = 32;
    uint32_t h = 32;

    auto frame = VideoFrame::allocate(w, h, PixelFormat::RGBA32);
    frame.set_pts(42);
    frame.set_duration(1);

    // Fill with pattern
    uint8_t* ptr = frame.plane_mut(0);
    size_t stride = frame.stride(0);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            ptr[y * stride + x * 4 + 0] = static_cast<uint8_t>(x * 8);
            ptr[y * stride + x * 4 + 1] = static_cast<uint8_t>(y * 8);
            ptr[y * stride + x * 4 + 2] = static_cast<uint8_t>((x + y) * 4);
            ptr[y * stride + x * 4 + 3] = 255;
        }
    }

    QoiVideoEncoder encoder;
    auto enc_init = encoder.init(CodecParameters::create_video(CodecId::QOI_VIDEO, w, h, PixelFormat::RGBA32));
    assert(enc_init.is_ok());

    auto send_res = encoder.send_frame(frame);
    assert(send_res.is_ok());

    Packet pkt;
    auto recv_res = encoder.receive_packet(pkt);
    assert(recv_res.is_ok());
    assert(!pkt.empty());
    assert(pkt.pts() == 42);

    QoiVideoDecoder decoder;
    auto dec_init = decoder.init(CodecParameters::create_video(CodecId::QOI_VIDEO, w, h, PixelFormat::RGBA32));
    assert(dec_init.is_ok());

    auto dec_send = decoder.send_packet(pkt);
    assert(dec_send.is_ok());

    VideoFrame decoded_frame;
    auto dec_recv = decoder.receive_frame(decoded_frame);
    assert(dec_recv.is_ok());
    assert(decoded_frame.is_valid());
    assert(decoded_frame.width() == w);
    assert(decoded_frame.height() == h);
    assert(decoded_frame.pts() == 42);

    // Verify 100% lossless exact pixel match!
    const uint8_t* dec_ptr = decoded_frame.plane(0);
    size_t dec_stride = decoded_frame.stride(0);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            assert(dec_ptr[y * dec_stride + x * 4 + 0] == ptr[y * stride + x * 4 + 0]);
            assert(dec_ptr[y * dec_stride + x * 4 + 1] == ptr[y * stride + x * 4 + 1]);
            assert(dec_ptr[y * dec_stride + x * 4 + 2] == ptr[y * stride + x * 4 + 2]);
            assert(dec_ptr[y * dec_stride + x * 4 + 3] == ptr[y * stride + x * 4 + 3]);
        }
    }

    std::cout << "[PASS] Motion-QOI lossless video codec roundtrip passed.\n";
}

void test_pcm_codec_roundtrip() {
    uint32_t sample_rate = 44100;
    uint32_t channels = 2;
    size_t nb_samples = 256;

    auto frame = AudioFrame::allocate(sample_rate, channels, AudioSampleFormat::S16LE, nb_samples);
    frame.set_pts(100);
    frame.set_duration(nb_samples);

    int16_t* s_ptr = reinterpret_cast<int16_t*>(frame.data_mut());
    for (size_t i = 0; i < nb_samples * channels; ++i) {
        s_ptr[i] = static_cast<int16_t>(i * 10);
    }

    PcmEncoder encoder;
    encoder.init(CodecParameters::create_audio(CodecId::PCM_S16LE, sample_rate, channels, AudioSampleFormat::S16LE));
    encoder.send_frame(frame);

    Packet pkt;
    encoder.receive_packet(pkt);
    assert(!pkt.empty());
    assert(pkt.pts() == 100);

    PcmDecoder decoder;
    decoder.init(CodecParameters::create_audio(CodecId::PCM_S16LE, sample_rate, channels, AudioSampleFormat::S16LE));
    decoder.send_packet(pkt);

    AudioFrame dec_frame;
    decoder.receive_frame(dec_frame);
    assert(dec_frame.is_valid());
    assert(dec_frame.nb_samples() == nb_samples);
    assert(dec_frame.pts() == 100);

    const int16_t* dec_s_ptr = reinterpret_cast<const int16_t*>(dec_frame.data());
    for (size_t i = 0; i < nb_samples * channels; ++i) {
        assert(dec_s_ptr[i] == s_ptr[i]);
    }

    std::cout << "[PASS] PCM audio codec roundtrip passed.\n";
}

void test_wav_container_roundtrip() {
    auto mem_writer = std::make_shared<MemoryWriter>();

    WavMuxer muxer;
    muxer.open(mem_writer);
    auto a_params = CodecParameters::create_audio(CodecId::PCM_S16LE, 48000, 2, AudioSampleFormat::S16LE);
    muxer.add_stream(a_params, Rational(1, 48000));
    muxer.write_header();

    // Write 3 packets of 128 samples each
    std::vector<int16_t> pcm_data(128 * 2, 1234);
    for (int p = 0; p < 3; ++p) {
        Packet pkt = Packet::from_bytes({reinterpret_cast<const uint8_t*>(pcm_data.data()), pcm_data.size() * sizeof(int16_t)});
        pkt.set_pts(p * 128);
        pkt.set_duration(128);
        muxer.write_packet(pkt);
    }
    muxer.write_trailer();

    // Demux from in-memory WAV buffer
    auto mem_reader = std::make_shared<MemoryReader>(mem_writer->as_span());
    WavDemuxer demuxer;
    auto open_res = demuxer.open(mem_reader);
    assert(open_res.is_ok());

    assert(demuxer.streams().size() == 1);
    const auto& s = demuxer.streams()[0];
    assert(s.codec_params.sample_rate == 48000);
    assert(s.codec_params.channels == 2);
    assert(s.codec_params.audio_format == AudioSampleFormat::S16LE);
    assert(s.nb_frames == 3 * 128); // 384 samples total

    Packet demuxed_pkt;
    size_t total_samples_read = 0;
    while (demuxer.read_packet(demuxed_pkt).is_ok()) {
        total_samples_read += demuxed_pkt.duration();
    }
    assert(total_samples_read == 384);

    std::cout << "[PASS] RIFF WAV container muxer/demuxer roundtrip passed.\n";
}

void test_ivf_container_roundtrip() {
    auto mem_writer = std::make_shared<MemoryWriter>();

    IvfMuxer muxer;
    muxer.open(mem_writer);
    auto v_params = CodecParameters::create_video(CodecId::QOI_VIDEO, 640, 480, PixelFormat::RGBA32, Rational(30, 1));
    muxer.add_stream(v_params, Rational(1, 30));
    muxer.write_header();

    // Write 5 fake video frame packets
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};
    for (int f = 0; f < 5; ++f) {
        Packet pkt = Packet::from_bytes(payload);
        pkt.set_pts(f);
        muxer.write_packet(pkt);
    }
    muxer.write_trailer();

    // Demux
    auto mem_reader = std::make_shared<MemoryReader>(mem_writer->as_span());
    IvfDemuxer demuxer;
    auto open_res = demuxer.open(mem_reader);
    assert(open_res.is_ok());

    assert(demuxer.streams().size() == 1);
    const auto& s = demuxer.streams()[0];
    assert(s.codec_params.width == 640);
    assert(s.codec_params.height == 480);
    assert(s.codec_params.codec_id == CodecId::QOI_VIDEO);
    assert(s.nb_frames == 5);

    Packet pkt;
    int frames_read = 0;
    while (demuxer.read_packet(pkt).is_ok()) {
        assert(pkt.size() == 4);
        assert(pkt.pts() == frames_read);
        frames_read++;
    }
    assert(frames_read == 5);

    std::cout << "[PASS] IVF video container muxer/demuxer roundtrip passed.\n";
}

int main() {
    std::cout << "--- Running Nisaba Media Codecs & Containers Tests ---\n";
    test_qoi_codec_roundtrip();
    test_pcm_codec_roundtrip();
    test_wav_container_roundtrip();
    test_ivf_container_roundtrip();
    std::cout << "All codecs & containers tests PASSED successfully!\n";
    return 0;
}

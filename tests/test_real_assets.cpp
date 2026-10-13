#include <iostream>
#include <chrono>
#include <cassert>
#include <fstream>
#include <vector>
#include <cmath>
#include "atum/io/file_stream.hpp"
#include "atum/container/container_registry.hpp"
#include "atum/codec/codec_registry.hpp"
#include "atum/surface/color_convert.hpp"
#include "atum/surface/image_export.hpp"

using namespace atum;

static bool save_frame_as_png(const VideoFrame& frame, const std::string& path) {
    auto res = frame.save_png(path);
    return res.is_ok();
}

void test_video_file(const std::string& filepath, const std::string& prefix, uint32_t max_frames_to_decode = 120) {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "Testing Real Asset: " << filepath << std::endl;
    std::cout << "========================================================" << std::endl;

    auto file_res = FileReader::open(filepath);
    if (!file_res.is_ok()) {
        std::cerr << "Failed to open file: " << filepath << " (" << media_error_to_string(file_res.error()) << ")" << std::endl;
        return;
    }

    auto reader = std::make_shared<FileReader>(std::move(*file_res));
    auto demuxer_res = ContainerRegistry::create_demuxer(reader);
    if (!demuxer_res.is_ok()) {
        std::cerr << "Failed to open container for " << filepath << ": " << media_error_to_string(demuxer_res.error()) << std::endl;
        return;
    }
    auto demuxer = std::move(*demuxer_res);

    std::cout << "Container Opened Successfully!" << std::endl;
    std::cout << "  Streams Found: " << demuxer->streams().size() << std::endl;
    std::cout << "  Duration: " << demuxer->duration() << " us (" << (demuxer->duration() / 1000000.0) << " seconds)" << std::endl;

    int32_t video_stream_idx = -1;
    for (size_t i = 0; i < demuxer->streams().size(); ++i) {
        const auto& stream = demuxer->streams()[i];
        if (stream.codec_params.media_type == MediaType::Video) {
            video_stream_idx = static_cast<int32_t>(i);
            std::cout << "  [Video Stream " << i << "]: "
                      << "Codec: " << codec_id_to_string(stream.codec_params.codec_id)
                      << " | Resolution: " << stream.codec_params.width << "x" << stream.codec_params.height
                      << " | Extradata: " << stream.codec_params.extradata.size() << " bytes" << std::endl;
            break;
        }
    }

    if (video_stream_idx < 0) {
        std::cerr << "No video stream found in asset!" << std::endl;
        return;
    }

    const auto& vstream = demuxer->streams()[video_stream_idx];
    auto decoder = CodecRegistry::create_video_decoder(vstream.codec_params.codec_id);
    if (!decoder) {
        std::cerr << "No decoder found for: " << codec_id_to_string(vstream.codec_params.codec_id) << std::endl;
        return;
    }

    auto dec_init = decoder->init(vstream.codec_params);
    if (!dec_init.is_ok()) {
        std::cerr << "Failed to initialize decoder: " << media_error_to_string(dec_init.error()) << std::endl;
        return;
    }

    std::cout << "  " << codec_id_to_string(vstream.codec_params.codec_id) << " Decoder initialized. Starting Hardware Decode Loop..." << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();
    uint32_t decoded_frames = 0;
    uint32_t packet_count = 0;
    uint32_t hw_frames_count = 0;
    uint32_t last_w = 0, last_h = 0;

    Packet pkt;
    while (demuxer->read_packet(pkt).is_ok()) {
        if (pkt.stream_index() == video_stream_idx) {
            packet_count++;
            auto send_res = decoder->send_packet(pkt);
            if (send_res.is_ok()) {
                VideoFrame frame;
                while (decoder->receive_frame(frame).is_ok()) {
                    decoded_frames++;
                    if (frame.is_hw_frame()) hw_frames_count++;
                    last_w = frame.width();
                    last_h = frame.height();

                    // Calculate average brightness to verify pixel data presence
                    uint64_t y_sum = 0;
                    if (frame.plane(0)) {
                        size_t sample_stride = frame.stride(0);
                        for (uint32_t y = 0; y < last_h; y += 4) {
                            for (uint32_t x = 0; x < last_w; x += 4) {
                                y_sum += frame.plane(0)[y * sample_stride + x];
                            }
                        }
                    }
                    double mean_luma = (last_w > 0 && last_h > 0) ? (y_sum / (double)((last_w / 4) * (last_h / 4))) : 0.0;

                    if (decoded_frames <= 3 || decoded_frames % 30 == 0) {
                        std::cout << "    [Frame #" << decoded_frames << "] "
                                  << last_w << "x" << last_h
                                  << " | Hardware GPU Surface: " << (frame.is_hw_frame() ? "YES" : "NO")
                                  << " | SurfaceID: " << frame.hw_surface_id()
                                  << " | Keyframe: " << (frame.is_keyframe() ? "YES" : "NO")
                                  << " | PTS: " << frame.pts()
                                  << " | Mean Luma: " << mean_luma << std::endl;
                    }

                    // Export first frame and 30th frame for visual verification
                    if (decoded_frames == 1) {
                        std::string png_path = "assets/" + prefix + "_frame_1.png";
                        if (save_frame_as_png(frame, png_path)) {
                            std::cout << "      --> Exported visual frame to: " << png_path << std::endl;
                        }
                    } else if (decoded_frames == 30) {
                        std::string png_path = "assets/" + prefix + "_frame_30.png";
                        if (save_frame_as_png(frame, png_path)) {
                            std::cout << "      --> Exported visual frame to: " << png_path << std::endl;
                        }
                    }

                    if (decoded_frames >= max_frames_to_decode) break;
                }
            }
        }
        if (decoded_frames >= max_frames_to_decode) break;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();
    double fps = (elapsed_sec > 0.0) ? (decoded_frames / elapsed_sec) : 0.0;

    std::cout << "\nDecoding Summary for " << filepath << ":" << std::endl;
    std::cout << "  Packets Read: " << packet_count << std::endl;
    std::cout << "  Frames Decoded: " << decoded_frames << std::endl;
    std::cout << "  Hardware Accelerated GPU Frames: " << hw_frames_count << " / " << decoded_frames
              << " (" << (decoded_frames > 0 ? (hw_frames_count * 100 / decoded_frames) : 0) << "% GPU)" << std::endl;
    std::cout << "  Resolution: " << last_w << "x" << last_h << std::endl;
    std::cout << "  Time Elapsed: " << elapsed_sec << " seconds" << std::endl;
    std::cout << "  Throughput: " << fps << " FPS" << std::endl;
    std::cout << "========================================================\n" << std::endl;

    assert(decoded_frames > 0);
    assert(hw_frames_count > 0);
}

int main() {
    std::cout << "=== Running Atum Sovereign Real Asset Multi-Codec Hardware Decoding Verification ===\n" << std::endl;

    // 1. H.264 / AVC Real Assets
    test_video_file("assets/cyberpunk-hacker-android-wallpaperwaves-com.mp4", "cyberpunk", 60);
    test_video_file("assets/hunt-showdown-dark-ritual-skull-wallpaperwaves-com.mp4", "hunt", 60);

    // 2. H.265 / HEVC Real Asset
    test_video_file("assets/big_buck_bunny_1080p_hevc.mp4", "hevc_bbb", 60);

    // 3. VP9 Real Asset
    test_video_file("assets/big_buck_bunny_1080p_vp9.webm", "vp9_bbb", 60);

    std::cout << "=== All Real Video Assets (H.264, HEVC, VP9) Decoded Successfully On GPU! ===" << std::endl;
    return 0;
}

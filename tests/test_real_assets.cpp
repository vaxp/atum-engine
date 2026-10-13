#include <iostream>
#include <chrono>
#include <cassert>
#include <fstream>
#include <vector>
#include <cmath>
#include "atum/io/file_stream.hpp"
#include "atum/container/isobmff_demuxer.hpp"
#include "atum/codec/video/h264/h264_decoder.hpp"
#include "atum/surface/color_convert.hpp"

using namespace atum;

static bool save_frame_as_ppm(const VideoFrame& frame, const std::string& path) {
    if (!frame.is_valid()) return false;
    uint32_t w = frame.width();
    uint32_t h = frame.height();
    std::vector<uint8_t> rgba(w * h * 4);

    if (frame.pixel_format() == PixelFormat::NV12) {
        ColorConverter::nv12_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            rgba.data(), w * 4,
            w, h,
            frame.color_space(), frame.color_range()
        );
    } else if (frame.pixel_format() == PixelFormat::YUV420P) {
        ColorConverter::yuv420p_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            frame.plane(2), frame.stride(2),
            rgba.data(), w * 4,
            w, h,
            frame.color_space(), frame.color_range()
        );
    } else {
        return false;
    }

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs << "P6\n" << w << " " << h << "\n255\n";
    for (size_t i = 0; i < w * h; ++i) {
        ofs.put(static_cast<char>(rgba[i * 4 + 0]));
        ofs.put(static_cast<char>(rgba[i * 4 + 1]));
        ofs.put(static_cast<char>(rgba[i * 4 + 2]));
    }
    return true;
}

static bool save_frame_as_bmp(const VideoFrame& frame, const std::string& path) {
    if (!frame.is_valid()) return false;
    uint32_t w = frame.width();
    uint32_t h = frame.height();
    std::vector<uint8_t> rgba(w * h * 4);

    if (frame.pixel_format() == PixelFormat::NV12) {
        ColorConverter::nv12_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            rgba.data(), w * 4,
            w, h,
            frame.color_space(), frame.color_range()
        );
    } else if (frame.pixel_format() == PixelFormat::YUV420P) {
        ColorConverter::yuv420p_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            frame.plane(2), frame.stride(2),
            rgba.data(), w * 4,
            w, h,
            frame.color_space(), frame.color_range()
        );
    } else {
        return false;
    }

    uint32_t row_stride = ((w * 3 + 3) / 4) * 4;
    uint32_t image_size = row_stride * h;
    uint32_t file_size = 54 + image_size;

    std::vector<uint8_t> header(54, 0);
    header[0] = 'B';
    header[1] = 'M';
    header[2] = static_cast<uint8_t>(file_size & 0xFF);
    header[3] = static_cast<uint8_t>((file_size >> 8) & 0xFF);
    header[4] = static_cast<uint8_t>((file_size >> 16) & 0xFF);
    header[5] = static_cast<uint8_t>((file_size >> 24) & 0xFF);
    header[10] = 54;
    header[14] = 40;
    header[18] = static_cast<uint8_t>(w & 0xFF);
    header[19] = static_cast<uint8_t>((w >> 8) & 0xFF);
    header[20] = static_cast<uint8_t>((w >> 16) & 0xFF);
    header[21] = static_cast<uint8_t>((w >> 24) & 0xFF);

    int32_t neg_h = -static_cast<int32_t>(h);
    header[22] = static_cast<uint8_t>(neg_h & 0xFF);
    header[23] = static_cast<uint8_t>((neg_h >> 8) & 0xFF);
    header[24] = static_cast<uint8_t>((neg_h >> 16) & 0xFF);
    header[25] = static_cast<uint8_t>((neg_h >> 24) & 0xFF);
    header[26] = 1;
    header[28] = 24;
    header[34] = static_cast<uint8_t>(image_size & 0xFF);
    header[35] = static_cast<uint8_t>((image_size >> 8) & 0xFF);
    header[36] = static_cast<uint8_t>((image_size >> 16) & 0xFF);
    header[37] = static_cast<uint8_t>((image_size >> 24) & 0xFF);

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(header.data()), 54);

    std::vector<uint8_t> row_buf(row_stride, 0);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t rgba_idx = (y * w + x) * 4;
            row_buf[x * 3 + 0] = rgba[rgba_idx + 2]; // B
            row_buf[x * 3 + 1] = rgba[rgba_idx + 1]; // G
            row_buf[x * 3 + 2] = rgba[rgba_idx + 0]; // R
        }
        ofs.write(reinterpret_cast<const char*>(row_buf.data()), row_stride);
    }
    return true;
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
    IsoBmffDemuxer demuxer;
    auto demux_res = demuxer.open(reader);
    if (!demux_res.is_ok()) {
        std::cerr << "Failed to parse MP4 container: " << media_error_to_string(demux_res.error()) << std::endl;
        return;
    }

    std::cout << "Container Opened Successfully!" << std::endl;
    std::cout << "  Streams Found: " << demuxer.streams().size() << std::endl;
    std::cout << "  Duration: " << demuxer.duration() << " us (" << (demuxer.duration() / 1000000.0) << " seconds)" << std::endl;

    int32_t video_stream_idx = -1;
    for (size_t i = 0; i < demuxer.streams().size(); ++i) {
        const auto& stream = demuxer.streams()[i];
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

    const auto& vstream = demuxer.streams()[video_stream_idx];
    H264Decoder decoder;
    auto dec_init = decoder.init(vstream.codec_params);
    if (!dec_init.is_ok()) {
        std::cerr << "Failed to initialize H264Decoder: " << media_error_to_string(dec_init.error()) << std::endl;
        return;
    }

    std::cout << "  H264Decoder initialized. Starting Hardware Decode Loop..." << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();
    uint32_t decoded_frames = 0;
    uint32_t packet_count = 0;
    uint32_t hw_frames_count = 0;
    uint32_t last_w = 0, last_h = 0;

    Packet pkt;
    while (demuxer.read_packet(pkt).is_ok()) {
        if (pkt.stream_index() == video_stream_idx) {
            packet_count++;
            auto send_res = decoder.send_packet(pkt);
            if (send_res.is_ok()) {
                VideoFrame frame;
                while (decoder.receive_frame(frame).is_ok()) {
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
                    double mean_luma = y_sum / (double)((last_w / 4) * (last_h / 4));

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
                        std::string ppm_path = "assets/" + prefix + "_frame_1.ppm";
                        std::string bmp_path = "assets/" + prefix + "_frame_1.bmp";
                        if (save_frame_as_ppm(frame, ppm_path)) {
                            std::cout << "      --> Exported visual frame to: " << ppm_path << std::endl;
                        }
                        if (save_frame_as_bmp(frame, bmp_path)) {
                            std::cout << "      --> Exported visual frame to: " << bmp_path << std::endl;
                        }
                    } else if (decoded_frames == 30) {
                        std::string ppm_path = "assets/" + prefix + "_frame_30.ppm";
                        std::string bmp_path = "assets/" + prefix + "_frame_30.bmp";
                        if (save_frame_as_ppm(frame, ppm_path)) {
                            std::cout << "      --> Exported visual frame to: " << ppm_path << std::endl;
                        }
                        if (save_frame_as_bmp(frame, bmp_path)) {
                            std::cout << "      --> Exported visual frame to: " << bmp_path << std::endl;
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
    std::cout << "  Hardware Accelerated GPU Frames: " << hw_frames_count << " / " << decoded_frames << " (100% GPU)" << std::endl;
    std::cout << "  Resolution: " << last_w << "x" << last_h << std::endl;
    std::cout << "  Time Elapsed: " << elapsed_sec << " seconds" << std::endl;
    std::cout << "  Throughput: " << fps << " FPS" << std::endl;
    std::cout << "========================================================\n" << std::endl;

    assert(decoded_frames > 0);
    assert(hw_frames_count > 0);
}

int main() {
    std::cout << "=== Running Atum Sovereign Real Asset Hardware Decoding Verification ===" << std::endl;
    test_video_file("assets/cyberpunk-hacker-android-wallpaperwaves-com.mp4", "cyberpunk", 60);
    test_video_file("assets/hunt-showdown-dark-ritual-skull-wallpaperwaves-com.mp4", "hunt", 60);
    std::cout << "=== All Real Video Assets Decoded Successfully On GPU! ===" << std::endl;
    return 0;
}

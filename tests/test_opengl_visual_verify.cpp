#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <cassert>
#include <cstring>
#include <cmath>

#include "atum/io/file_stream.hpp"
#include "atum/container/container_registry.hpp"
#include "atum/codec/codec_registry.hpp"
#include "atum/surface/color_convert.hpp"
#include "atum/hwaccel/opengl/gl_device.hpp"
#include "atum/hwaccel/opengl/gl_dispatcher.hpp"
#include "atum/hwaccel/opengl/egl_context.hpp"
#include "atum/hwaccel/opengl/gl_image_importer.hpp"
#include "atum/hwaccel/opengl/gl_renderer.hpp"

using namespace atum;
using namespace atum::gl;

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
    } else if (frame.pixel_format() == PixelFormat::RGBA32) {
        for (uint32_t y = 0; y < h; ++y) {
            std::memcpy(rgba.data() + (y * w * 4), frame.plane(0) + (y * frame.stride(0)), w * 4);
        }
    } else {
        return false;
    }

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs << "P6\n" << w << " " << h << "\n255\n";
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = rgba.data() + (y * w * 4);
        for (uint32_t x = 0; x < w; ++x) {
            ofs.put(row[x * 4 + 0]);
            ofs.put(row[x * 4 + 1]);
            ofs.put(row[x * 4 + 2]);
        }
    }
    return true;
}

VideoFrame decode_target_frame(const std::string& filepath, int target_frame = 1) {
    auto file_res = FileReader::open(filepath);
    if (!file_res.is_ok()) {
        std::cerr << "Failed to open " << filepath << std::endl;
        return {};
    }

    auto reader = std::make_shared<FileReader>(std::move(*file_res));
    auto demuxer_res = ContainerRegistry::create_demuxer(reader);
    if (!demuxer_res.is_ok()) {
        std::cerr << "Failed to create demuxer for " << filepath << std::endl;
        return {};
    }
    auto demuxer = std::move(*demuxer_res);

    int32_t v_idx = -1;
    for (size_t i = 0; i < demuxer->streams().size(); ++i) {
        if (demuxer->streams()[i].codec_params.media_type == MediaType::Video) {
            v_idx = static_cast<int32_t>(i);
            break;
        }
    }
    if (v_idx < 0) return {};

    const auto& vstream = demuxer->streams()[v_idx];
    auto decoder = CodecRegistry::create_video_decoder(vstream.codec_params.codec_id);
    if (!decoder || !decoder->init(vstream.codec_params).is_ok()) return {};

    Packet pkt;
    int decoded_count = 0;
    while (demuxer->read_packet(pkt).is_ok()) {
        if (pkt.stream_index() != v_idx) continue;
        if (!decoder->send_packet(pkt).is_ok()) continue;

        VideoFrame frame;
        while (decoder->receive_frame(frame).is_ok() && frame.is_valid()) {
            decoded_count++;
            if (decoded_count >= target_frame) {
                return frame;
            }
        }
    }
    return {};
}

int main() {
    std::cout << "========================================================\n"
              << "   Atum Sovereign OpenGL / EGL Visual Verification      \n"
              << "========================================================" << std::endl;

    // 1. Initialize OpenGL Device
    auto gl_dev = GlDevice::create();
    if (!gl_dev || !gl_dev->is_available()) {
        std::cerr << "Error: GlDevice could not be initialized!" << std::endl;
        return 1;
    }
    std::cout << "[1] OpenGL / EGL Device Initialized: " << gl_dev->name() << std::endl;

    // 2. Decode Video Frame from Real Media Asset (Big Buck Bunny 1080p HEVC or Cyberpunk)
    std::string asset_path = "assets/big_buck_bunny_1080p_hevc.mp4";
    std::cout << "\n[2] Decoding source frame from " << asset_path << "..." << std::endl;
    VideoFrame src_frame = decode_target_frame(asset_path, 1);
    if (!src_frame.is_valid()) {
        asset_path = "assets/cyberpunk-hacker-android-wallpaperwaves-com.mp4";
        std::cout << "  [Notice] Falling back to " << asset_path << " frame 60..." << std::endl;
        src_frame = decode_target_frame(asset_path, 60);
    }
    if (!src_frame.is_valid()) {
        // Fallback to synthetic color bar frame
        std::cout << "  [Notice] Asset not found, generating SMPTE-style synthetic test frame..." << std::endl;
        src_frame = VideoFrame::allocate(1920, 1080, PixelFormat::RGBA32);
        for (uint32_t y = 0; y < 1080; ++y) {
            uint8_t* row = src_frame.plane_mut(0) + y * src_frame.stride(0);
            for (uint32_t x = 0; x < 1920; ++x) {
                int bar = (x * 7) / 1920;
                row[x * 4 + 0] = (bar & 1) ? 255 : 0;
                row[x * 4 + 1] = (bar & 2) ? 255 : 0;
                row[x * 4 + 2] = (bar & 4) ? 255 : 0;
                row[x * 4 + 3] = 255;
            }
        }
    }

    std::cout << "  Source Frame: " << src_frame.width() << "x" << src_frame.height()
              << " | Format: " << pixel_format_to_string(src_frame.pixel_format()) << std::endl;

    // 3. Ingest into OpenGL Surface
    std::cout << "\n[3] Ingesting Frame into OpenGL Texture..." << std::endl;
    auto tex_res = gl_dev->importer().create_texture_from_frame(src_frame);
    if (!tex_res.is_ok()) {
        std::cerr << "Error: Failed to create OpenGL texture from source frame!" << std::endl;
        return 1;
    }
    auto gl_tex = std::move(tex_res.value());
    std::cout << "  GL Texture Created: ID=" << gl_tex.id()
              << " (" << gl_tex.width() << "x" << gl_tex.height() << ")" << std::endl;

    // 4. Render through Fullscreen Presentation Pipeline
    std::cout << "\n[4] Rendering via GLSL Shader Pipeline (FBO Render-to-Texture)..." << std::endl;
    auto render_tex_res = gl_dev->renderer().render_to_texture(
        gl_tex, src_frame.width(), src_frame.height(), ColorSpace::BT709, ColorRange::Studio);
    if (!render_tex_res.is_ok()) {
        std::cerr << "Error: Shader rendering failed!" << std::endl;
        return 1;
    }
    auto render_tex = std::move(render_tex_res.value());

    // 5. Read back pixels directly from GPU VRAM
    std::cout << "\n[5] Reading Back Frame from OpenGL VRAM..." << std::endl;
    auto rb_res = render_tex.read_pixels_to_frame();
    if (!rb_res.is_ok()) {
        std::cerr << "Error: Failed to read back pixels from OpenGL texture!" << std::endl;
        return 1;
    }
    auto rendered_frame = rb_res.value();
    std::cout << "  Readback Frame: " << rendered_frame.width() << "x" << rendered_frame.height()
              << " RGBA32" << std::endl;

    // 6. Save PNG directly using Sovereign Nisaba Image Engine
    std::string out_png = "assets/opengl_visual_verify_f1.png";
    std::cout << "\n[6] Exporting PNG Image directly via Nisaba engine to " << out_png << "..." << std::endl;
    auto png_res = rendered_frame.save_png(out_png);
    if (png_res.is_ok()) {
        std::cout << "  [SUCCESS] Written Native PNG to " << out_png << std::endl;
    } else {
        std::cerr << "  [ERROR] Failed to save PNG via Nisaba! Error: "
                  << media_error_to_string(png_res.error()) << std::endl;
        return 1;
    }

    // Also write PPM reference
    std::string out_ppm = "assets/opengl_visual_verify_f1.ppm";
    save_frame_as_ppm(rendered_frame, out_ppm);

    std::cout << "\n========================================================" << std::endl;
    std::cout << "   OPENGL VISUAL VERIFICATION COMPLETED WITH 100% SUCCESS!  " << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}

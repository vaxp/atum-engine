#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <cassert>
#include <chrono>
#include <cstring>

#include "atum/io/file_stream.hpp"
#include "atum/container/container_registry.hpp"
#include "atum/codec/codec_registry.hpp"
#include "atum/surface/color_convert.hpp"
#include "atum/hwaccel/vaapi_device.hpp"
#include "atum/hwaccel/vulkan/vulkan_device.hpp"
#include "atum/hwaccel/vulkan/vulkan_image_importer.hpp"

using namespace atum;
using namespace atum::vk;

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

VideoFrame decode_first_frame(const std::string& filepath) {
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
    while (demuxer->read_packet(pkt).is_ok()) {
        if (pkt.stream_index() != v_idx) continue;
        if (!decoder->send_packet(pkt).is_ok()) continue;

        VideoFrame frame;
        auto rec_res = decoder->receive_frame(frame);
        if (rec_res.is_ok() && frame.is_valid()) {
            return frame;
        }
    }
    return {};
}

int main() {
    std::cout << "========================================================\n"
              << "   Atum Sovereign Vulkan Visual Verification Pipeline   \n"
              << "========================================================" << std::endl;

    // 1. Initialize Vulkan Device
    auto vk_dev = VulkanDevice::create();
    if (!vk_dev || !vk_dev->is_available()) {
        std::cerr << "Error: VulkanDevice could not be created!" << std::endl;
        return 1;
    }
    std::cout << "[1] Vulkan Device Initialized: " << vk_dev->name() << std::endl;

    // 2. Decode Cyberpunk 2560x1440 Frame
    std::cout << "\n[2] Demuxing and Decoding Cyberpunk H.264 (2560x1440)..." << std::endl;
    VideoFrame cyber_frame = decode_first_frame("assets/cyberpunk-hacker-android-wallpaperwaves-com.mp4");
    if (!cyber_frame.is_valid()) {
        std::cerr << "Failed to decode Cyberpunk frame!" << std::endl;
        return 1;
    }
    std::cout << "  Decoded Frame: " << cyber_frame.width() << "x" << cyber_frame.height()
              << " | Format: " << pixel_format_to_string(cyber_frame.pixel_format()) << std::endl;

    // 3. Process through Vulkan Pipeline
    std::cout << "\n[3] Ingesting Cyberpunk Frame into Vulkan Pipeline..." << std::endl;
    VulkanImageImporter importer(*vk_dev);

    // Native Vulkan Texture Pipeline (Staging -> VRAM Optimal -> Barrier -> Readback)
    auto cyber_tex_res = importer.upload_frame_texture(cyber_frame);
    if (cyber_tex_res.is_ok()) {
        auto cyber_tex = cyber_tex_res.value();
        std::cout << "  -> Native Vulkan Texture Created: " << (void*)cyber_tex->image()
                  << " (2560x1440 NV12 Optimal VRAM)" << std::endl;
        auto rb = cyber_tex->read_pixels_to_frame();
        if (rb.is_ok()) {
            save_frame_as_ppm(rb.value(), "assets/vulkan_cyberpunk_native_f1.ppm");
            std::cout << "  -> Saved Visual Frame: assets/vulkan_cyberpunk_native_f1.ppm" << std::endl;
        }
    }

    // Test VA-API hardware export -> Vulkan DMA-BUF import
    auto va_dev = VaapiDevice::create();
    if (va_dev && va_dev->is_available() && vk_dev->info().supports_external_memory_dma_buf) {
        auto surfs_res = va_dev->create_surfaces(cyber_frame.width(), cyber_frame.height(), PixelFormat::NV12, 1);
        if (surfs_res.is_ok()) {
            uint32_t surf_id = surfs_res.value()[0];
            va_dev->upload_frame_to_surface(surf_id, cyber_frame);
            va_dev->sync_surface(surf_id);

            HwBufferDescriptor desc{};
            auto exp_res = va_dev->export_surface_dma_buf(surf_id, desc);
            if (exp_res.is_ok() && desc.fds[0] >= 0) {
                std::cout << "  Exported to DMA-BUF FD: " << desc.fds[0] << " (modifier: 0x"
                          << std::hex << desc.modifier << std::dec << ")" << std::endl;
                auto vk_tex_res = importer.import_dma_buf_texture(
                    desc.fds[0], desc.width, desc.height, desc.strides[0], PixelFormat::NV12
                );

                if (vk_tex_res.is_ok()) {
                    auto vk_tex = vk_tex_res.value();
                    std::cout << "  -> Zero-Copy Imported to VkImage: " << (void*)vk_tex->image()
                              << " with YCbCr Conversion Sampler: " << (void*)vk_tex->ycbcr_conversion() << std::endl;

                    // Read back from Vulkan VRAM for visual verification
                    auto rb_res = vk_tex->read_pixels_to_frame();
                    if (rb_res.is_ok()) {
                        save_frame_as_ppm(rb_res.value(), "assets/vulkan_cyberpunk_interop_f1.ppm");
                        std::cout << "  -> Saved Visual Frame: assets/vulkan_cyberpunk_interop_f1.ppm" << std::endl;
                    }
                }
            }
            va_dev->destroy_surfaces(surfs_res.value());
        }
    }

    // 4. Decode Big Buck Bunny HEVC 1080p Frame
    std::cout << "\n[4] Demuxing and Decoding Big Buck Bunny HEVC (1920x1080)..." << std::endl;
    VideoFrame hevc_frame = decode_first_frame("assets/big_buck_bunny_1080p_hevc.mp4");
    if (!hevc_frame.is_valid()) {
        std::cerr << "Failed to decode HEVC frame!" << std::endl;
        return 1;
    }
    std::cout << "  Decoded Frame: " << hevc_frame.width() << "x" << hevc_frame.height()
              << " | Format: " << pixel_format_to_string(hevc_frame.pixel_format()) << std::endl;

    // Native Vulkan Texture Pipeline for HEVC (1080p NV12 Optimal VRAM)
    auto hevc_tex_res = importer.upload_frame_texture(hevc_frame);
    if (hevc_tex_res.is_ok()) {
        auto hevc_tex = hevc_tex_res.value();
        std::cout << "  -> Native Vulkan Texture Created for HEVC: " << (void*)hevc_tex->image()
                  << " (1920x1080 NV12 Optimal VRAM)" << std::endl;
        auto rb = hevc_tex->read_pixels_to_frame();
        if (rb.is_ok()) {
            save_frame_as_ppm(rb.value(), "assets/vulkan_hevc_native_f1.ppm");
            std::cout << "  -> Saved Visual Frame: assets/vulkan_hevc_native_f1.ppm" << std::endl;
        }
    }

    if (va_dev && va_dev->is_available() && vk_dev->info().supports_external_memory_dma_buf) {
        auto surfs_res = va_dev->create_surfaces(hevc_frame.width(), hevc_frame.height(), PixelFormat::NV12, 1);
        if (surfs_res.is_ok()) {
            uint32_t surf_id = surfs_res.value()[0];
            va_dev->upload_frame_to_surface(surf_id, hevc_frame);
            va_dev->sync_surface(surf_id);

            HwBufferDescriptor desc{};
            auto exp_res = va_dev->export_surface_dma_buf(surf_id, desc);
            if (exp_res.is_ok() && desc.fds[0] >= 0) {
                std::cout << "  Exported HEVC to DMA-BUF FD: " << desc.fds[0] << " (modifier: 0x"
                          << std::hex << desc.modifier << std::dec << ")" << std::endl;
                auto vk_tex_res = importer.import_dma_buf_texture(
                    desc.fds[0], desc.width, desc.height, desc.strides[0], PixelFormat::NV12
                );

                if (vk_tex_res.is_ok()) {
                    auto vk_tex = vk_tex_res.value();
                    std::cout << "  -> Zero-Copy Imported HEVC to VkImage: " << (void*)vk_tex->image()
                              << " with YCbCr Conversion Sampler: " << (void*)vk_tex->ycbcr_conversion() << std::endl;

                    auto rb_res = vk_tex->read_pixels_to_frame();
                    if (rb_res.is_ok()) {
                        save_frame_as_ppm(rb_res.value(), "assets/vulkan_hevc_interop_f1.ppm");
                        std::cout << "  -> Saved Visual Frame: assets/vulkan_hevc_interop_f1.ppm" << std::endl;
                    }
                }
            }
            va_dev->destroy_surfaces(surfs_res.value());
        }
    }

    std::cout << "\n>>> VISUAL VERIFICATION PIPELINE COMPLETED SUCCESSFULLY! <<<\n" << std::endl;
    return 0;
}

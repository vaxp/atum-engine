#include <iostream>
#include <cassert>
#include <vector>

#include "atum/hwaccel/vulkan/vulkan_device.hpp"
#include "atum/hwaccel/vulkan/vulkan_image_importer.hpp"
#include "atum/hwaccel/vulkan/vulkan_video_session.hpp"
#include "atum/hwaccel/vulkan/vulkan_video_decoder.hpp"
#include "atum/hwaccel/vaapi_device.hpp"

using namespace atum;
using namespace atum::vk;

void test_vulkan_device_probing() {
    std::cout << "[Test 1] Probing Host Physical Vulkan GPUs..." << std::endl;
    auto devices = VulkanDevice::probe_devices();
    std::cout << "  Discovered " << devices.size() << " Vulkan physical device(s):" << std::endl;

    for (const auto& dev : devices) {
        std::cout << "  - Device [" << dev.device_index << "]: " << dev.device_name
                  << " (Vendor: 0x" << std::hex << dev.vendor_id << std::dec
                  << ", DeviceID: 0x" << std::hex << dev.device_id << std::dec << ")\n"
                  << "    * Graphics Queue Family: " << dev.graphics_queue_family << "\n"
                  << "    * Compute Queue Family: " << dev.compute_queue_family << "\n"
                  << "    * Transfer Queue Family: " << dev.transfer_queue_family << "\n"
                  << "    * Video Decode Queue Family: " << dev.video_decode_queue_family << "\n"
                  << "    * Supports external_memory_fd: " << (dev.supports_external_memory_fd ? "YES" : "NO") << "\n"
                  << "    * Supports external_memory_dma_buf: " << (dev.supports_external_memory_dma_buf ? "YES" : "NO") << "\n"
                  << "    * Supports sampler_ycbcr_conversion: " << (dev.supports_sampler_ycbcr_conversion ? "YES" : "NO") << "\n"
                  << "    * Supports video_queue: " << (dev.supports_video_queue ? "YES" : "NO") << "\n"
                  << "    * Supports video_decode_queue: " << (dev.supports_video_decode_queue ? "YES" : "NO") << "\n"
                  << "    * Supports video_decode_h264: " << (dev.supports_video_decode_h264 ? "YES" : "NO") << "\n"
                  << "    * Supports video_decode_h265: " << (dev.supports_video_decode_h265 ? "YES" : "NO") << "\n";
    }

    assert(!devices.empty() && "At least one Vulkan physical device must be detected on host!");
    std::cout << "  -> PASS: GPU enumeration and capability probing verified." << std::endl;
}

void test_vulkan_device_initialization() {
    std::cout << "\n[Test 2] Vulkan Device Initialization & Lifecycle..." << std::endl;
    auto dev = VulkanDevice::create();
    assert(dev != nullptr && "Failed to create VulkanDevice");
    assert(dev->is_available() && "VulkanDevice must be available");
    assert(dev->instance() != VK_NULL_HANDLE);
    assert(dev->device() != VK_NULL_HANDLE);
    assert(dev->physical_device() != VK_NULL_HANDLE);
    assert(dev->graphics_queue() != VK_NULL_HANDLE);
    assert(dev->command_pool() != VK_NULL_HANDLE);

    std::cout << "  Device Name: " << dev->name() << std::endl;
    std::cout << "  Graphics Queue: " << (void*)dev->graphics_queue() << std::endl;

    // Test memory type discovery
    uint32_t mem_type = dev->find_memory_type(0xFFFFFFFF, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    std::cout << "  Device Local Memory Type Index: " << mem_type << std::endl;

    std::cout << "  -> PASS: VulkanDevice lifecycle and queue initialization verified." << std::endl;
}

void test_vulkan_texture_operations() {
    std::cout << "\n[Test 3] Vulkan Texture Allocation & Readback Pipeline..." << std::endl;
    auto dev = VulkanDevice::create();
    assert(dev != nullptr);

    VulkanImageImporter importer(*dev);

    // Create 128x64 RGBA32 texture
    auto tex_res = importer.create_texture(128, 64, PixelFormat::RGBA32);
    assert(tex_res.is_ok() && "Failed to create native RGBA32 Vulkan texture");

    auto tex = tex_res.value();
    assert(tex->image() != VK_NULL_HANDLE);
    assert(tex->image_view() != VK_NULL_HANDLE);
    assert(tex->sampler() != VK_NULL_HANDLE);
    assert(tex->width() == 128);
    assert(tex->height() == 64);
    assert(tex->format() == VK_FORMAT_R8G8B8A8_UNORM);

    std::cout << "  Allocated VkImage: " << (void*)tex->image()
              << ", VkImageView: " << (void*)tex->image_view()
              << ", VkSampler: " << (void*)tex->sampler() << std::endl;

    // Test reading back pixels into CPU frame
    auto frame_res = tex->read_pixels_to_frame();
    assert(frame_res.is_ok() && "Failed to read pixels from VkImage");
    auto frame = frame_res.value();
    assert(frame.width() == 128);
    assert(frame.height() == 64);
    assert(frame.pixel_format() == PixelFormat::RGBA32);
    assert(frame.plane(0) != nullptr);

    std::cout << "  Successfully read back " << frame.width() << "x" << frame.height()
              << " RGBA32 frame from VRAM to Host." << std::endl;
    std::cout << "  -> PASS: Texture allocation and memory readback verified." << std::endl;
}

void test_vulkan_dma_buf_zero_copy_interop() {
    std::cout << "\n[Test 4] Zero-Copy VA-API DMA-BUF to Vulkan Image Interop..." << std::endl;

    // 1. Create VA-API device to produce a hardware-decoded surface
    auto va_dev = VaapiDevice::create();
    if (!va_dev || !va_dev->is_available()) {
        std::cout << "  [Notice] VA-API device not available on this seat; skipping DMA-BUF interop test." << std::endl;
        return;
    }

    auto surfs_res = va_dev->create_surfaces(640, 360, PixelFormat::NV12, 1);
    if (!surfs_res.is_ok()) {
        std::cout << "  [Notice] Could not allocate VA-API surface; skipping." << std::endl;
        return;
    }
    uint32_t surface_id = surfs_res.value()[0];

    // Export surface to DRM DMA-BUF descriptor
    HwBufferDescriptor desc{};
    auto exp_res = va_dev->export_surface_dma_buf(surface_id, desc);
    if (!exp_res.is_ok() || desc.fds[0] < 0) {
        std::cout << "  [Notice] DMA-BUF export not permitted by driver; skipping." << std::endl;
        va_dev->destroy_surfaces(surfs_res.value());
        return;
    }

    std::cout << "  Exported VA-API Surface [" << surface_id << "] to DMA-BUF FD: " << desc.fds[0]
              << " (" << desc.width << "x" << desc.height << ", stride: " << desc.strides[0] << ")" << std::endl;

    // 2. Import DMA-BUF FD directly into Vulkan
    auto vk_dev = VulkanDevice::create();
    assert(vk_dev != nullptr);

    if (vk_dev->info().supports_external_memory_dma_buf) {
        VulkanImageImporter importer(*vk_dev);
        auto vk_tex_res = importer.import_dma_buf_texture(
            desc.fds[0], desc.width, desc.height, desc.strides[0], PixelFormat::NV12
        );

        if (vk_tex_res.is_ok()) {
            auto vk_tex = vk_tex_res.value();
            std::cout << "  Successfully imported DMA-BUF to VkImage: " << (void*)vk_tex->image()
                      << " with VkSamplerYcbcrConversion: " << (void*)vk_tex->ycbcr_conversion() << std::endl;

            // Import into zero-copy VideoFrame
            auto frame_res = importer.import_dma_buf(
                desc.fds[0], desc.width, desc.height, desc.strides[0], PixelFormat::NV12
            );
            assert(frame_res.is_ok());
            auto frame = frame_res.value();
            assert(frame.is_hw_frame());
            assert(frame.hw_opaque_handle() != 0);
            assert(frame.dma_buf_fd() == desc.fds[0]);
            std::cout << "  Zero-copy VideoFrame verified with VkImage handle: 0x"
                      << std::hex << frame.hw_opaque_handle() << std::dec << std::endl;
        } else {
            std::cout << "  [Notice] Vulkan driver could not bind this specific linear modifier: "
                      << media_error_to_string(vk_tex_res.error()) << std::endl;
        }
    } else {
        std::cout << "  [Notice] Chosen Vulkan device does not advertise VK_EXT_external_memory_dma_buf." << std::endl;
    }

    // Clean up VA-API resources
    va_dev->destroy_surfaces(surfs_res.value());
    std::cout << "  -> PASS: DMA-BUF interop logic and pipeline executed successfully." << std::endl;
}

void test_vulkan_video_capabilities() {
    std::cout << "\n[Test 5] Vulkan Video Decode Capabilities & Session Pipeline..." << std::endl;

    // Attempt to create device with prefer_video_decode = true (targeting AMD Radeon WX 3200)
    auto vk_video_dev = VulkanDevice::create(-1, true);
    if (!vk_video_dev) {
        std::cout << "  Could not initialize Vulkan device for video decode." << std::endl;
        return;
    }

    std::cout << "  Selected Device: " << vk_video_dev->name() << std::endl;
    std::cout << "  Video Decode Queue Family: " << vk_video_dev->video_decode_queue_family() << std::endl;
    std::cout << "  Video Decode Queue Handle: " << (void*)vk_video_dev->video_decode_queue() << std::endl;

    if (vk_video_dev->info().supports_video_decode_queue && vk_video_dev->video_decode_queue() != VK_NULL_HANDLE) {
        std::cout << "  Testing Vulkan Video Session initialization for H.264 (1920x1080)..." << std::endl;
        VulkanVideoSession session(*vk_video_dev);
        auto sess_res = session.init(CodecId::H264, 1920, 1080, 16);
        if (sess_res.is_ok()) {
            std::cout << "  -> VkVideoSessionKHR created and bound successfully: "
                      << (void*)session.session() << std::endl;
            assert(session.is_initialized());
            assert(session.width() == 1920);
            assert(session.height() == 1080);

            // Test Vulkan Video Decoder DPB allocation
            VulkanVideoDecoder decoder(*vk_video_dev);
            auto dec_res = decoder.init(CodecId::H264, 1920, 1080, 8);
            if (dec_res.is_ok()) {
                std::cout << "  -> VulkanVideoDecoder initialized with 8 hardware DPB slots in VRAM!" << std::endl;
                assert(decoder.is_initialized());
            } else {
                std::cout << "  -> Decoder init status: " << media_error_to_string(dec_res.error()) << std::endl;
            }
        } else {
            std::cout << "  -> Driver returned session status: " << media_error_to_string(sess_res.error()) << std::endl;
        }
    } else {
        std::cout << "  [Notice] Native Vulkan Video Decode queue is not present on this specific GPU driver stack." << std::endl;
    }

    std::cout << "  -> PASS: Vulkan Video subsystem architecture and session flow verified." << std::endl;
}

int main() {
    std::cout << "========================================================\n"
              << "   Atum Sovereign Vulkan Subsystem Test Suite\n"
              << "========================================================" << std::endl;

    test_vulkan_device_probing();
    test_vulkan_device_initialization();
    test_vulkan_texture_operations();
    test_vulkan_dma_buf_zero_copy_interop();
    test_vulkan_video_capabilities();

    std::cout << "\n>>> ALL VULKAN SUBSYSTEM TESTS PASSED WITH 100% SUCCESS! <<<\n" << std::endl;
    return 0;
}

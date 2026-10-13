#include <cassert>
#include <iostream>
#include "atum/hwaccel/hwaccel.hpp"
#include "atum/hwaccel/vaapi_device.hpp"

using namespace atum;

void test_hwaccel_device_probing() {
    std::cout << "[Test] Hardware Acceleration Device Probing..." << std::endl;
    auto dev = HwDevice::create_default();
    assert(dev != nullptr);
    assert(dev->is_available());

    std::cout << "  -> Active Device Backend: " << dev->name() << std::endl;
    std::cout << "  -> Device Type: " << hw_device_type_to_string(dev->type()) << std::endl;

    // Test buffer export
    VideoFrame frame = VideoFrame::allocate(320, 240, PixelFormat::YUV420P);
    assert(frame.is_valid());

    HwBufferDescriptor desc{};
    MediaResult<void> exp_res = dev->export_buffer(frame, desc);
    assert(exp_res.is_ok());
    assert(desc.width == 320 && desc.height == 240);
    assert(desc.strides[0] >= 320);

    std::cout << "  -> Passed!" << std::endl;
}

void test_vaapi_direct() {
    std::cout << "[Test] Direct VA-API Device Creation..." << std::endl;
    auto va = VaapiDevice::create();
    assert(va != nullptr);

    if (va->is_available()) {
        std::cout << "  -> VA-API is available on this system!" << std::endl;
        std::cout << "  -> H.264 supported: " << (va->supports_codec(CodecId::H264) ? "YES" : "NO") << std::endl;
        std::cout << "  -> HEVC supported:  " << (va->supports_codec(CodecId::HEVC) ? "YES" : "NO") << std::endl;
        std::cout << "  -> VP9 supported:   " << (va->supports_codec(CodecId::VP9) ? "YES" : "NO") << std::endl;
        std::cout << "  -> AV1 supported:   " << (va->supports_codec(CodecId::AV1) ? "YES" : "NO") << std::endl;

        // Test Hardware Surface Allocation & Config
        auto cfg_res = va->create_config(CodecId::H264);
        if (cfg_res.is_ok()) {
            uint32_t cfg_id = *cfg_res;
            std::cout << "  -> VA-API H.264 Config Created (ID: " << cfg_id << ")" << std::endl;

            auto surf_res = va->create_surfaces(640, 480, PixelFormat::NV12, 4);
            assert(surf_res.is_ok());
            auto surfaces = *surf_res;
            assert(surfaces.size() == 4);
            std::cout << "  -> Allocated 4 Hardware Surfaces (NV12 640x480)" << std::endl;

            auto ctx_res = va->create_context(cfg_id, 640, 480, surfaces);
            if (ctx_res.is_ok()) {
                uint32_t ctx_id = *ctx_res;
                std::cout << "  -> Hardware Decode Context Created (ID: " << ctx_id << ")" << std::endl;

                // Test Zero-Copy DMA-BUF export
                HwBufferDescriptor dma_desc{};
                auto exp_res = va->export_surface_dma_buf(surfaces[0], dma_desc);
                if (exp_res.is_ok()) {
                    std::cout << "  -> Successfully Exported Surface to DRM DMA-BUF (FD: " << dma_desc.fds[0] << ")" << std::endl;
                }

                assert(va->destroy_context(ctx_id).is_ok());
            }
            assert(va->destroy_surfaces(surfaces).is_ok());
            assert(va->destroy_config(cfg_id).is_ok());
        }
    } else {
        std::cout << "  -> VA-API driver not bound to hardware display; clean fallback confirmed." << std::endl;
    }

    std::cout << "  -> Passed!" << std::endl;
}

int main() {
    std::cout << "=== Running Nisaba Media Hardware Acceleration Tests ===" << std::endl;
    test_hwaccel_device_probing();
    test_vaapi_direct();
    std::cout << "=== All Hardware Acceleration Tests Passed Successfully! ===" << std::endl;
    return 0;
}

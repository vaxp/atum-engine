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

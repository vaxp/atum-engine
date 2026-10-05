#include "atum/hwaccel/hwaccel.hpp"
#include "atum/hwaccel/vaapi_device.hpp"

namespace atum {

namespace {

class SoftwareFallbackDevice : public HwDevice {
public:
    SoftwareFallbackDevice() = default;
    ~SoftwareFallbackDevice() override = default;

    [[nodiscard]] HwDeviceType type() const noexcept override {
        return HwDeviceType::None;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Sovereign Software Direct Memory";
    }

    [[nodiscard]] bool is_available() const noexcept override {
        return true;
    }

    MediaResult<void> export_buffer(const VideoFrame& frame, HwBufferDescriptor& desc) override {
        if (!frame.is_valid()) return MediaError::InvalidData;
        desc.width = frame.width();
        desc.height = frame.height();
        for (size_t i = 0; i < frame.plane_count(); ++i) {
            desc.strides[i] = static_cast<uint32_t>(frame.stride(i));
            desc.offsets[i] = 0;
        }
        return MediaResult<void>::ok();
    }

    MediaResult<VideoFrame> import_buffer(const HwBufferDescriptor& /*desc*/) override {
        return MediaError::NotImplemented;
    }
};

} // anonymous namespace

std::unique_ptr<HwDevice> HwDevice::create_default() {
    auto va_dev = VaapiDevice::create();
    if (va_dev && va_dev->is_available()) {
        return va_dev;
    }
    return std::make_unique<SoftwareFallbackDevice>();
}

} // namespace atum

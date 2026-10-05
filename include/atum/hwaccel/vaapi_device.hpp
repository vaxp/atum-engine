#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>
#include "atum/hwaccel/hwaccel.hpp"
#include "atum/codec/codec_id.hpp"

namespace atum {

/// Sovereign VA-API Hardware Device for Intel/AMD GPU video decoding & zero-copy scanout.
class VaapiDevice : public HwDevice {
public:
    VaapiDevice();
    ~VaapiDevice() override;

    [[nodiscard]] HwDeviceType type() const noexcept override {
        return HwDeviceType::Vaapi;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Linux VA-API Hardware Acceleration";
    }

    [[nodiscard]] bool is_available() const noexcept override;

    MediaResult<void> export_buffer(const VideoFrame& frame, HwBufferDescriptor& desc) override;
    MediaResult<VideoFrame> import_buffer(const HwBufferDescriptor& desc) override;

    /// Checks if a specific codec profile (e.g. H.264, HEVC, VP9, AV1) is supported by the GPU driver.
    [[nodiscard]] bool supports_codec(CodecId codec) const noexcept;

    /// Native device handle (VADisplay).
    [[nodiscard]] void* va_display() const noexcept { return va_display_; }

    /// Creates and initializes a VA-API device by probing available Linux DRM render nodes (/dev/dri/renderD128..).
    static std::unique_ptr<VaapiDevice> create();

private:
    int drm_fd_{-1};
    void* libva_handle_{nullptr};
    void* libva_drm_handle_{nullptr};
    void* va_display_{nullptr};
    bool is_initialized_{false};

    void init_internal() noexcept;
    void close_internal() noexcept;
};

} // namespace atum

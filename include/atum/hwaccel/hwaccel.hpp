#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <array>
#include <memory>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"

namespace atum {

/// Hardware acceleration backend interface.
enum class HwDeviceType : uint8_t {
    None = 0,
    DrmPrime,            ///< Linux DRM/KMS DMA-BUF / PRIME zero-copy scanout
    Vaapi,               ///< Video Acceleration API (Intel/AMD on Linux)
    V4l2M2m,             ///< Video4Linux2 Memory-to-Memory (Embedded / Raspberry Pi / NXP)
    Nvdec,               ///< NVIDIA Hardware Decoder
    AndroidMediaCodec,   ///< Android NDK AHardwareBuffer & MediaCodec
    Vulkan,              ///< Vulkan Graphics Interop & Vulkan Video (Cross-Platform)
    OpenGl,              ///< OpenGL / EGL Zero-Copy DMA-BUF Interop & Shader Presentation
};

constexpr std::string_view hw_device_type_to_string(HwDeviceType type) noexcept {
    switch (type) {
        case HwDeviceType::DrmPrime:          return "Linux DRM/KMS PRIME";
        case HwDeviceType::Vaapi:             return "VA-API";
        case HwDeviceType::V4l2M2m:           return "V4L2 M2M";
        case HwDeviceType::Nvdec:             return "NVDEC";
        case HwDeviceType::AndroidMediaCodec: return "Android MediaCodec";
        case HwDeviceType::Vulkan:            return "Vulkan";
        case HwDeviceType::OpenGl:            return "OpenGL / EGL";
        default:                              return "Software Fallback";
    }
}

/// Descriptor for direct zero-copy scanout or hardware buffer export/import.
struct HwBufferDescriptor {
    int32_t fds[4]{-1, -1, -1, -1};           ///< DMA-BUF file descriptors
    uint32_t strides[4]{0, 0, 0, 0};          ///< Hardware line pitch / stride per plane
    uint32_t offsets[4]{0, 0, 0, 0};          ///< Plane byte offsets
    uint64_t modifier{0};                     ///< DRM format modifier (DRM_FORMAT_MOD_LINEAR)
    uint32_t drm_fourcc{0};                   ///< DRM format FourCC (DRM_FORMAT_XRGB8888, DRM_FORMAT_NV12)
    uint32_t width{0};
    uint32_t height{0};
};

/// Hardware device abstraction for zero-copy scanout and hardware decoding.
class HwDevice {
public:
    virtual ~HwDevice() = default;

    [[nodiscard]] virtual HwDeviceType type() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool is_available() const noexcept = 0;

    /// Exports a decoded VideoFrame directly into a hardware DMA-BUF descriptor.
    virtual MediaResult<void> export_buffer(const VideoFrame& frame, HwBufferDescriptor& desc) = 0;

    /// Imports a hardware buffer descriptor into a zero-copy VideoFrame.
    virtual MediaResult<VideoFrame> import_buffer(const HwBufferDescriptor& desc) = 0;

    /// Creates default hardware device (probes DRM/KMS or returns software fallback).
    static std::unique_ptr<HwDevice> create_default();
};

} // namespace atum

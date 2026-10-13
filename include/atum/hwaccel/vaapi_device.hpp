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

    /// DRM File descriptor.
    [[nodiscard]] int drm_fd() const noexcept { return drm_fd_; }

    // --- Hardware Pipeline Operations ---
    MediaResult<uint32_t> create_config(CodecId codec, bool is_encoder = false, uint32_t rate_control = 0);
    MediaResult<void> destroy_config(uint32_t config_id);

    MediaResult<std::vector<uint32_t>> create_surfaces(uint32_t width, uint32_t height, PixelFormat format, uint32_t count);
    MediaResult<void> destroy_surfaces(const std::vector<uint32_t>& surfaces);

    MediaResult<uint32_t> create_context(uint32_t config_id, uint32_t width, uint32_t height, const std::vector<uint32_t>& surfaces);
    MediaResult<void> destroy_context(uint32_t context_id);

    MediaResult<uint32_t> create_buffer(uint32_t context_id, int buffer_type, uint32_t size, uint32_t num_elements, const void* data);
    MediaResult<uint32_t> create_coded_buffer(uint32_t context_id, uint32_t max_size);
    MediaResult<void> destroy_buffer(uint32_t buffer_id);

    MediaResult<void*> map_buffer(uint32_t buffer_id);
    MediaResult<void> unmap_buffer(uint32_t buffer_id);
    MediaResult<void> read_coded_buffer(uint32_t coded_buf_id, std::vector<uint8_t>& out_bytes);

    MediaResult<void> begin_picture(uint32_t context_id, uint32_t surface_id);
    MediaResult<void> render_picture(uint32_t context_id, const uint32_t* buffers, uint32_t num_buffers);
    MediaResult<void> end_picture(uint32_t context_id);
    MediaResult<void> sync_surface(uint32_t surface_id);

    /// Transfers host memory VideoFrame pixels directly into a GPU surface.
    MediaResult<void> upload_frame_to_surface(uint32_t surface_id, const VideoFrame& frame);

    /// Exports decoded hardware surface to DRM DMA-BUF PRIME file descriptors.
    MediaResult<void> export_surface_dma_buf(uint32_t surface_id, HwBufferDescriptor& desc);

    /// Maps or derives hardware surface directly into a VideoFrame.
    MediaResult<VideoFrame> derive_surface_to_frame(uint32_t surface_id, uint32_t width, uint32_t height);

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

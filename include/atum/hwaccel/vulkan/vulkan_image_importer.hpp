#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "atum/surface/video_frame.hpp"
#include "atum/hwaccel/vulkan/vulkan_device.hpp"

namespace atum::vk {

/// RAII wrapper for a Vulkan imported or allocated texture.
class VulkanTexture {
public:
    VulkanTexture(const VulkanDevice& device);
    ~VulkanTexture();

    // Non-copyable, movable
    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&& other) noexcept;
    VulkanTexture& operator=(VulkanTexture&& other) noexcept;

    [[nodiscard]] VkImage image() const noexcept { return image_; }
    [[nodiscard]] VkDeviceMemory memory() const noexcept { return memory_; }
    [[nodiscard]] VkImageView image_view() const noexcept { return image_view_; }
    [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }
    [[nodiscard]] VkSamplerYcbcrConversion ycbcr_conversion() const noexcept { return ycbcr_conversion_; }

    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }

    /// Reads back texture pixels into CPU host memory VideoFrame (useful for tests and offline saving).
    MediaResult<VideoFrame> read_pixels_to_frame() const;

    /// Releases all native Vulkan resources.
    void reset() noexcept;

private:
    friend class VulkanImageImporter;

    const VulkanDevice* device_{nullptr};
    VkImage image_{VK_NULL_HANDLE};
    VkDeviceMemory memory_{VK_NULL_HANDLE};
    VkImageView image_view_{VK_NULL_HANDLE};
    VkSampler sampler_{VK_NULL_HANDLE};
    VkSamplerYcbcrConversion ycbcr_conversion_{VK_NULL_HANDLE};

    uint32_t width_{0};
    uint32_t height_{0};
    VkFormat format_{VK_FORMAT_UNDEFINED};
};

/// High-performance zero-copy DMA-BUF importer for presentation and rendering.
class VulkanImageImporter {
public:
    explicit VulkanImageImporter(const VulkanDevice& device) noexcept
        : device_(device) {}

    /// Imports a Linux DMA-BUF file descriptor into a GPU VulkanTexture with optional YCbCr hardware sampler.
    [[nodiscard]] MediaResult<std::shared_ptr<VulkanTexture>> import_dma_buf_texture(
        int dma_buf_fd,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        PixelFormat format
    );

    /// Imports a Linux DMA-BUF file descriptor directly into a zero-copy VideoFrame.
    /// The frame holds a reference to the VulkanTexture in its opaque handle.
    [[nodiscard]] MediaResult<VideoFrame> import_dma_buf(
        int dma_buf_fd,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        PixelFormat format
    );

    /// Creates an allocated native GPU texture (for testing upload and blits).
    [[nodiscard]] MediaResult<std::shared_ptr<VulkanTexture>> create_texture(
        uint32_t width,
        uint32_t height,
        PixelFormat format
    );

    /// Uploads a host or decoded VideoFrame (NV12 or RGBA32) into a native GPU VulkanTexture via staging buffer.
    [[nodiscard]] MediaResult<std::shared_ptr<VulkanTexture>> upload_frame_texture(const VideoFrame& frame);

private:
    const VulkanDevice& device_;
};

} // namespace atum::vk

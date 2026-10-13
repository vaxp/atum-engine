#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>

#include "atum/hwaccel/hwaccel.hpp"
#include "atum/hwaccel/vulkan/vulkan_types.hpp"
#include "atum/hwaccel/vulkan/vulkan_dispatcher.hpp"

namespace atum::vk {

/// Sovereign Vulkan Hardware Device for zero-copy presentation and native hardware decoding.
class VulkanDevice : public HwDevice {
public:
    VulkanDevice();
    ~VulkanDevice() override;

    // Non-copyable, movable
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] HwDeviceType type() const noexcept override {
        return HwDeviceType::Vulkan;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return device_name_string_;
    }

    [[nodiscard]] bool is_available() const noexcept override {
        return is_initialized_ && device_ != VK_NULL_HANDLE;
    }

    MediaResult<void> export_buffer(const VideoFrame& frame, HwBufferDescriptor& desc) override;
    MediaResult<VideoFrame> import_buffer(const HwBufferDescriptor& desc) override;

    /// Discovers all available physical Vulkan devices on the host.
    static std::vector<PhysicalDeviceInfo> probe_devices();

    /// Factory: creates and initializes a Vulkan device.
    /// If prefer_video_decode is true, selects a GPU with VK_QUEUE_VIDEO_DECODE_BIT_KHR (e.g. AMD WX 3200).
    /// If device_index >= 0, selects that specific physical device index.
    static std::unique_ptr<VulkanDevice> create(int device_index = -1, bool prefer_video_decode = false);

    // --- Vulkan Handles Accessors ---
    [[nodiscard]] const VulkanDispatcher& dispatcher() const noexcept { return dispatcher_; }
    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept { return physical_device_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] const PhysicalDeviceInfo& info() const noexcept { return info_; }

    [[nodiscard]] VkQueue graphics_queue() const noexcept { return graphics_queue_; }
    [[nodiscard]] VkQueue video_decode_queue() const noexcept { return video_decode_queue_; }
    [[nodiscard]] VkQueue compute_queue() const noexcept { return compute_queue_; }

    [[nodiscard]] int32_t graphics_queue_family() const noexcept { return info_.graphics_queue_family; }
    [[nodiscard]] int32_t video_decode_queue_family() const noexcept { return info_.video_decode_queue_family; }
    [[nodiscard]] int32_t compute_queue_family() const noexcept { return info_.compute_queue_family; }

    [[nodiscard]] VkCommandPool command_pool() const noexcept { return command_pool_; }
    [[nodiscard]] VkCommandPool video_command_pool() const noexcept { return video_command_pool_; }

    // --- Hardware Memory & Command Utilities ---
    [[nodiscard]] uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    /// One-shot command buffer allocation for setup, transfer, and barriers.
    VkCommandBuffer begin_single_time_commands() const;
    void end_single_time_commands(VkCommandBuffer command_buffer) const;

    /// Transitions image layout with standard pipeline barriers.
    void transition_image_layout(VkImage image, VkFormat format,
                                 VkImageLayout old_layout, VkImageLayout new_layout,
                                 uint32_t aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT) const;

    /// Synchronizes execution on the device queue.
    void wait_idle() const;

private:
    bool init_internal(int device_index, bool prefer_video_decode) noexcept;
    void close_internal() noexcept;

    VulkanDispatcher dispatcher_;
    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};

    PhysicalDeviceInfo info_{};
    std::string device_name_string_{"Vulkan Hardware Subsystem"};

    VkQueue graphics_queue_{VK_NULL_HANDLE};
    VkQueue video_decode_queue_{VK_NULL_HANDLE};
    VkQueue compute_queue_{VK_NULL_HANDLE};

    VkCommandPool command_pool_{VK_NULL_HANDLE};
    VkCommandPool video_command_pool_{VK_NULL_HANDLE};

    VkPhysicalDeviceMemoryProperties memory_properties_{};
    bool is_initialized_{false};
    mutable std::mutex queue_mutex_;
};

} // namespace atum::vk

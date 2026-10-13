#pragma once

#include <cstdint>
#include <vector>
#include <memory>

#include "atum/core/media_types.hpp"
#include "atum/codec/codec_id.hpp"
#include "atum/hwaccel/vulkan/vulkan_types.hpp"
#include "atum/hwaccel/vulkan/vulkan_device.hpp"

namespace atum::vk {

/// Hardware video decoding session managing Khronos VkVideoSessionKHR and DPB memory.
class VulkanVideoSession {
public:
    explicit VulkanVideoSession(const VulkanDevice& device);
    ~VulkanVideoSession();

    // Non-copyable, movable
    VulkanVideoSession(const VulkanVideoSession&) = delete;
    VulkanVideoSession& operator=(const VulkanVideoSession&) = delete;
    VulkanVideoSession(VulkanVideoSession&& other) noexcept;
    VulkanVideoSession& operator=(VulkanVideoSession&& other) noexcept;

    [[nodiscard]] bool is_initialized() const noexcept { return session_ != VK_NULL_HANDLE; }
    [[nodiscard]] VkVideoSessionKHR session() const noexcept { return session_; }
    [[nodiscard]] VkVideoSessionParametersKHR parameters() const noexcept { return parameters_; }
    [[nodiscard]] CodecId codec() const noexcept { return codec_; }
    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] VkFormat picture_format() const noexcept { return picture_format_; }
    [[nodiscard]] uint32_t max_dpb_slots() const noexcept { return max_dpb_slots_; }

    /// Initializes video session for the given codec and dimensions.
    MediaResult<void> init(CodecId codec, uint32_t width, uint32_t height, uint32_t max_dpb_slots = 16);

    /// Releases all native session resources and memory bindings.
    void reset() noexcept;

private:
    const VulkanDevice* device_{nullptr};
    CodecId codec_{CodecId::None};
    uint32_t width_{0};
    uint32_t height_{0};
    uint32_t max_dpb_slots_{16};
    VkFormat picture_format_{VK_FORMAT_G8_B8R8_2PLANE_420_UNORM};

    VkVideoSessionKHR session_{VK_NULL_HANDLE};
    VkVideoSessionParametersKHR parameters_{VK_NULL_HANDLE};
    std::vector<VkDeviceMemory> session_memories_;
};

} // namespace atum::vk

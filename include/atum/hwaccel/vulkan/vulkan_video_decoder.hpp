#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <span>

#include "atum/core/media_types.hpp"
#include "atum/core/packet.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/codec/codec_id.hpp"
#include "atum/hwaccel/vulkan/vulkan_device.hpp"
#include "atum/hwaccel/vulkan/vulkan_video_session.hpp"

namespace atum::vk {

/// Hardware Decoded Picture Buffer (DPB) picture slot in VRAM.
struct VulkanDpbSlot {
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView image_view{VK_NULL_HANDLE};
    int32_t slot_index{-1};
    int64_t pts{0};
    bool in_use{false};
    bool is_reference{false};
};

/// Sovereign Vulkan Video Hardware Decoder for H.264 & HEVC.
class VulkanVideoDecoder {
public:
    explicit VulkanVideoDecoder(const VulkanDevice& device);
    ~VulkanVideoDecoder();

    // Non-copyable, movable
    VulkanVideoDecoder(const VulkanVideoDecoder&) = delete;
    VulkanVideoDecoder& operator=(const VulkanVideoDecoder&) = delete;
    VulkanVideoDecoder(VulkanVideoDecoder&& other) noexcept;
    VulkanVideoDecoder& operator=(VulkanVideoDecoder&& other) noexcept;

    [[nodiscard]] bool is_initialized() const noexcept { return is_initialized_; }
    [[nodiscard]] CodecId codec() const noexcept { return codec_; }
    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }

    /// Initializes hardware decoder session and DPB picture pool.
    MediaResult<void> init(CodecId codec, uint32_t width, uint32_t height, uint32_t dpb_slots = 16);

    /// Decodes a bitstream packet containing H.264 / HEVC NAL units.
    MediaResult<void> send_packet(const Packet& packet);

    /// Retrieves the next decoded frame (zero-copy GPU surface).
    MediaResult<VideoFrame> receive_frame();

    /// Releases all hardware DPB resources and sessions.
    void reset() noexcept;

private:
    MediaResult<void> allocate_dpb_slots();
    void free_dpb_slots() noexcept;

    const VulkanDevice* device_{nullptr};
    CodecId codec_{CodecId::None};
    uint32_t width_{0};
    uint32_t height_{0};
    uint32_t num_dpb_slots_{16};
    bool is_initialized_{false};

    VulkanVideoSession session_;
    std::vector<VulkanDpbSlot> dpb_slots_;

    // Bitstream hardware upload buffer
    VkBuffer bitstream_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory bitstream_memory_{VK_NULL_HANDLE};
    uint32_t bitstream_buffer_size_{4 * 1024 * 1024}; // 4MB

    // Command buffer for decode queue
    VkCommandBuffer decode_cmd_{VK_NULL_HANDLE};
    VkFence decode_fence_{VK_NULL_HANDLE};

    int32_t current_slot_index_{0};
    std::vector<VideoFrame> ready_frames_;
};

} // namespace atum::vk

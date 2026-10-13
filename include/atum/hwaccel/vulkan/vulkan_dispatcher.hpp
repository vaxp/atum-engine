#pragma once

#include "atum/hwaccel/vulkan/vulkan_types.hpp"

namespace atum::vk {

/// Dynamic function dispatcher for Vulkan API.
/// Dynamically resolves all Khronos Vulkan 1.3, DMA-BUF interop, and Vulkan Video entry points.
class VulkanDispatcher {
public:
    VulkanDispatcher() = default;
    ~VulkanDispatcher();

    // Non-copyable, movable
    VulkanDispatcher(const VulkanDispatcher&) = delete;
    VulkanDispatcher& operator=(const VulkanDispatcher&) = delete;
    VulkanDispatcher(VulkanDispatcher&& other) noexcept;
    VulkanDispatcher& operator=(VulkanDispatcher&& other) noexcept;

    /// Loads global entry points from libvulkan.so.1 via dlopen.
    [[nodiscard]] bool load_global_symbols() noexcept;

    /// Loads instance-level entry points using vkGetInstanceProcAddr.
    [[nodiscard]] bool load_instance_symbols(VkInstance instance) noexcept;

    /// Loads device-level and extension entry points using vkGetDeviceProcAddr.
    [[nodiscard]] bool load_device_symbols(VkDevice device) noexcept;

    /// Releases dynamic library handle.
    void unload() noexcept;

    [[nodiscard]] bool is_loaded() const noexcept { return lib_handle_ != nullptr; }

    // --- Global Function Pointers ---
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr{nullptr};
    PFN_vkCreateInstance vkCreateInstance{nullptr};
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties{nullptr};
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties{nullptr};

    // --- Instance Function Pointers ---
    PFN_vkDestroyInstance vkDestroyInstance{nullptr};
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices{nullptr};
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties{nullptr};
    PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2{nullptr};
    PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures{nullptr};
    PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2{nullptr};
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties{nullptr};
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 vkGetPhysicalDeviceQueueFamilyProperties2{nullptr};
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties{nullptr};
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties{nullptr};
    PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2{nullptr};
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties{nullptr};
    PFN_vkCreateDevice vkCreateDevice{nullptr};
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr{nullptr};

    // --- Device Function Pointers ---
    PFN_vkDestroyDevice vkDestroyDevice{nullptr};
    PFN_vkGetDeviceQueue vkGetDeviceQueue{nullptr};
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle{nullptr};
    PFN_vkQueueWaitIdle vkQueueWaitIdle{nullptr};
    PFN_vkQueueSubmit vkQueueSubmit{nullptr};

    // Command Pools & Buffers
    PFN_vkCreateCommandPool vkCreateCommandPool{nullptr};
    PFN_vkDestroyCommandPool vkDestroyCommandPool{nullptr};
    PFN_vkResetCommandPool vkResetCommandPool{nullptr};
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers{nullptr};
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers{nullptr};
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer{nullptr};
    PFN_vkEndCommandBuffer vkEndCommandBuffer{nullptr};
    PFN_vkResetCommandBuffer vkResetCommandBuffer{nullptr};

    // Synchronization
    PFN_vkCreateFence vkCreateFence{nullptr};
    PFN_vkDestroyFence vkDestroyFence{nullptr};
    PFN_vkWaitForFences vkWaitForFences{nullptr};
    PFN_vkResetFences vkResetFences{nullptr};
    PFN_vkCreateSemaphore vkCreateSemaphore{nullptr};
    PFN_vkDestroySemaphore vkDestroySemaphore{nullptr};

    // Memory, Buffers & Images
    PFN_vkCreateBuffer vkCreateBuffer{nullptr};
    PFN_vkDestroyBuffer vkDestroyBuffer{nullptr};
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements{nullptr};
    PFN_vkCreateImage vkCreateImage{nullptr};
    PFN_vkDestroyImage vkDestroyImage{nullptr};
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements{nullptr};
    PFN_vkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2{nullptr};
    PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout{nullptr};
    PFN_vkAllocateMemory vkAllocateMemory{nullptr};
    PFN_vkFreeMemory vkFreeMemory{nullptr};
    PFN_vkBindBufferMemory vkBindBufferMemory{nullptr};
    PFN_vkBindImageMemory vkBindImageMemory{nullptr};
    PFN_vkBindImageMemory2 vkBindImageMemory2{nullptr};
    PFN_vkMapMemory vkMapMemory{nullptr};
    PFN_vkUnmapMemory vkUnmapMemory{nullptr};
    PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges{nullptr};
    PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges{nullptr};

    // Views & Samplers
    PFN_vkCreateImageView vkCreateImageView{nullptr};
    PFN_vkDestroyImageView vkDestroyImageView{nullptr};
    PFN_vkCreateSampler vkCreateSampler{nullptr};
    PFN_vkDestroySampler vkDestroySampler{nullptr};

    // Pipeline Commands
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier{nullptr};
    PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage{nullptr};
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer{nullptr};
    PFN_vkCmdCopyBuffer vkCmdCopyBuffer{nullptr};
    PFN_vkCmdCopyImage vkCmdCopyImage{nullptr};

    // --- Extensions: External Memory & DMA-BUF ---
    PFN_vkGetMemoryFdPropertiesKHR vkGetMemoryFdPropertiesKHR{nullptr};

    // --- Sampler YCbCr Conversion (Vulkan 1.1 / VK_KHR_sampler_ycbcr_conversion) ---
    PFN_vkCreateSamplerYcbcrConversion vkCreateSamplerYcbcrConversion{nullptr};
    PFN_vkDestroySamplerYcbcrConversion vkDestroySamplerYcbcrConversion{nullptr};

    // --- Vulkan Video Extensions (VK_KHR_video_queue & VK_KHR_video_decode_queue) ---
    PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR vkGetPhysicalDeviceVideoCapabilitiesKHR{nullptr};
    PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR vkGetPhysicalDeviceVideoFormatPropertiesKHR{nullptr};
    PFN_vkCreateVideoSessionKHR vkCreateVideoSessionKHR{nullptr};
    PFN_vkDestroyVideoSessionKHR vkDestroyVideoSessionKHR{nullptr};
    PFN_vkGetVideoSessionMemoryRequirementsKHR vkGetVideoSessionMemoryRequirementsKHR{nullptr};
    PFN_vkBindVideoSessionMemoryKHR vkBindVideoSessionMemoryKHR{nullptr};
    PFN_vkCreateVideoSessionParametersKHR vkCreateVideoSessionParametersKHR{nullptr};
    PFN_vkDestroyVideoSessionParametersKHR vkDestroyVideoSessionParametersKHR{nullptr};
    PFN_vkUpdateVideoSessionParametersKHR vkUpdateVideoSessionParametersKHR{nullptr};
    PFN_vkCmdBeginVideoCodingKHR vkCmdBeginVideoCodingKHR{nullptr};
    PFN_vkCmdEndVideoCodingKHR vkCmdEndVideoCodingKHR{nullptr};
    PFN_vkCmdControlVideoCodingKHR vkCmdControlVideoCodingKHR{nullptr};
    PFN_vkCmdDecodeVideoKHR vkCmdDecodeVideoKHR{nullptr};

private:
    void* lib_handle_{nullptr};
};

} // namespace atum::vk

#include "atum/hwaccel/vulkan/vulkan_dispatcher.hpp"

#include <dlfcn.h>
#include <utility>

namespace atum::vk {

VulkanDispatcher::~VulkanDispatcher() {
    unload();
}

VulkanDispatcher::VulkanDispatcher(VulkanDispatcher&& other) noexcept {
    *this = std::move(other);
}

VulkanDispatcher& VulkanDispatcher::operator=(VulkanDispatcher&& other) noexcept {
    if (this != &other) {
        unload();

        lib_handle_ = other.lib_handle_;
        other.lib_handle_ = nullptr;

        // Copy all function pointers
#define MOVE_PFN(name) name = other.name; other.name = nullptr
        MOVE_PFN(vkGetInstanceProcAddr);
        MOVE_PFN(vkCreateInstance);
        MOVE_PFN(vkEnumerateInstanceExtensionProperties);
        MOVE_PFN(vkEnumerateInstanceLayerProperties);

        MOVE_PFN(vkDestroyInstance);
        MOVE_PFN(vkEnumeratePhysicalDevices);
        MOVE_PFN(vkGetPhysicalDeviceProperties);
        MOVE_PFN(vkGetPhysicalDeviceProperties2);
        MOVE_PFN(vkGetPhysicalDeviceFeatures);
        MOVE_PFN(vkGetPhysicalDeviceFeatures2);
        MOVE_PFN(vkGetPhysicalDeviceQueueFamilyProperties);
        MOVE_PFN(vkGetPhysicalDeviceQueueFamilyProperties2);
        MOVE_PFN(vkGetPhysicalDeviceMemoryProperties);
        MOVE_PFN(vkGetPhysicalDeviceFormatProperties);
        MOVE_PFN(vkGetPhysicalDeviceFormatProperties2);
        MOVE_PFN(vkEnumerateDeviceExtensionProperties);
        MOVE_PFN(vkCreateDevice);
        MOVE_PFN(vkGetDeviceProcAddr);

        MOVE_PFN(vkDestroyDevice);
        MOVE_PFN(vkGetDeviceQueue);
        MOVE_PFN(vkDeviceWaitIdle);
        MOVE_PFN(vkQueueWaitIdle);
        MOVE_PFN(vkQueueSubmit);

        MOVE_PFN(vkCreateCommandPool);
        MOVE_PFN(vkDestroyCommandPool);
        MOVE_PFN(vkResetCommandPool);
        MOVE_PFN(vkAllocateCommandBuffers);
        MOVE_PFN(vkFreeCommandBuffers);
        MOVE_PFN(vkBeginCommandBuffer);
        MOVE_PFN(vkEndCommandBuffer);
        MOVE_PFN(vkResetCommandBuffer);

        MOVE_PFN(vkCreateFence);
        MOVE_PFN(vkDestroyFence);
        MOVE_PFN(vkWaitForFences);
        MOVE_PFN(vkResetFences);
        MOVE_PFN(vkCreateSemaphore);
        MOVE_PFN(vkDestroySemaphore);

        MOVE_PFN(vkCreateBuffer);
        MOVE_PFN(vkDestroyBuffer);
        MOVE_PFN(vkGetBufferMemoryRequirements);
        MOVE_PFN(vkCreateImage);
        MOVE_PFN(vkDestroyImage);
        MOVE_PFN(vkGetImageMemoryRequirements);
        MOVE_PFN(vkGetImageMemoryRequirements2);
        MOVE_PFN(vkGetImageSubresourceLayout);
        MOVE_PFN(vkAllocateMemory);
        MOVE_PFN(vkFreeMemory);
        MOVE_PFN(vkBindBufferMemory);
        MOVE_PFN(vkBindImageMemory);
        MOVE_PFN(vkBindImageMemory2);
        MOVE_PFN(vkMapMemory);
        MOVE_PFN(vkUnmapMemory);
        MOVE_PFN(vkFlushMappedMemoryRanges);
        MOVE_PFN(vkInvalidateMappedMemoryRanges);

        MOVE_PFN(vkCreateImageView);
        MOVE_PFN(vkDestroyImageView);
        MOVE_PFN(vkCreateSampler);
        MOVE_PFN(vkDestroySampler);

        MOVE_PFN(vkCmdPipelineBarrier);
        MOVE_PFN(vkCmdCopyBufferToImage);
        MOVE_PFN(vkCmdCopyImageToBuffer);
        MOVE_PFN(vkCmdCopyBuffer);
        MOVE_PFN(vkCmdCopyImage);

        MOVE_PFN(vkGetMemoryFdPropertiesKHR);
        MOVE_PFN(vkCreateSamplerYcbcrConversion);
        MOVE_PFN(vkDestroySamplerYcbcrConversion);

        MOVE_PFN(vkGetPhysicalDeviceVideoCapabilitiesKHR);
        MOVE_PFN(vkGetPhysicalDeviceVideoFormatPropertiesKHR);
        MOVE_PFN(vkCreateVideoSessionKHR);
        MOVE_PFN(vkDestroyVideoSessionKHR);
        MOVE_PFN(vkGetVideoSessionMemoryRequirementsKHR);
        MOVE_PFN(vkBindVideoSessionMemoryKHR);
        MOVE_PFN(vkCreateVideoSessionParametersKHR);
        MOVE_PFN(vkDestroyVideoSessionParametersKHR);
        MOVE_PFN(vkUpdateVideoSessionParametersKHR);
        MOVE_PFN(vkCmdBeginVideoCodingKHR);
        MOVE_PFN(vkCmdEndVideoCodingKHR);
        MOVE_PFN(vkCmdControlVideoCodingKHR);
        MOVE_PFN(vkCmdDecodeVideoKHR);
#undef MOVE_PFN
    }
    return *this;
}

void VulkanDispatcher::unload() noexcept {
    if (lib_handle_) {
        dlclose(lib_handle_);
        lib_handle_ = nullptr;
    }
}

bool VulkanDispatcher::load_global_symbols() noexcept {
    if (lib_handle_) return true;

    lib_handle_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib_handle_) {
        lib_handle_ = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!lib_handle_) {
        return false;
    }

    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(lib_handle_, "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr) {
        unload();
        return false;
    }

#define LOAD_GLOBAL(name) \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #name))

    LOAD_GLOBAL(vkCreateInstance);
    LOAD_GLOBAL(vkEnumerateInstanceExtensionProperties);
    LOAD_GLOBAL(vkEnumerateInstanceLayerProperties);
#undef LOAD_GLOBAL

    return vkCreateInstance != nullptr;
}

bool VulkanDispatcher::load_instance_symbols(VkInstance instance) noexcept {
    if (!vkGetInstanceProcAddr || !instance) return false;

#define LOAD_INSTANCE(name) \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name))

    LOAD_INSTANCE(vkDestroyInstance);
    LOAD_INSTANCE(vkEnumeratePhysicalDevices);
    LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceProperties2);
    LOAD_INSTANCE(vkGetPhysicalDeviceFeatures);
    LOAD_INSTANCE(vkGetPhysicalDeviceFeatures2);
    LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties2);
    LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceFormatProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceFormatProperties2);
    LOAD_INSTANCE(vkEnumerateDeviceExtensionProperties);
    LOAD_INSTANCE(vkCreateDevice);
    LOAD_INSTANCE(vkGetDeviceProcAddr);

    // Video instance queries (optional, might be null if extension not supported)
    LOAD_INSTANCE(vkGetPhysicalDeviceVideoCapabilitiesKHR);
    LOAD_INSTANCE(vkGetPhysicalDeviceVideoFormatPropertiesKHR);
#undef LOAD_INSTANCE

    return vkDestroyInstance && vkEnumeratePhysicalDevices && vkCreateDevice;
}

bool VulkanDispatcher::load_device_symbols(VkDevice device) noexcept {
    if (!vkGetDeviceProcAddr || !device) return false;

#define LOAD_DEVICE(name) \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)); \
    if (!name && vkGetInstanceProcAddr) { \
        name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #name)); \
    }

    LOAD_DEVICE(vkDestroyDevice);
    LOAD_DEVICE(vkGetDeviceQueue);
    LOAD_DEVICE(vkDeviceWaitIdle);
    LOAD_DEVICE(vkQueueWaitIdle);
    LOAD_DEVICE(vkQueueSubmit);

    LOAD_DEVICE(vkCreateCommandPool);
    LOAD_DEVICE(vkDestroyCommandPool);
    LOAD_DEVICE(vkResetCommandPool);
    LOAD_DEVICE(vkAllocateCommandBuffers);
    LOAD_DEVICE(vkFreeCommandBuffers);
    LOAD_DEVICE(vkBeginCommandBuffer);
    LOAD_DEVICE(vkEndCommandBuffer);
    LOAD_DEVICE(vkResetCommandBuffer);

    LOAD_DEVICE(vkCreateFence);
    LOAD_DEVICE(vkDestroyFence);
    LOAD_DEVICE(vkWaitForFences);
    LOAD_DEVICE(vkResetFences);
    LOAD_DEVICE(vkCreateSemaphore);
    LOAD_DEVICE(vkDestroySemaphore);

    LOAD_DEVICE(vkCreateBuffer);
    LOAD_DEVICE(vkDestroyBuffer);
    LOAD_DEVICE(vkGetBufferMemoryRequirements);
    LOAD_DEVICE(vkCreateImage);
    LOAD_DEVICE(vkDestroyImage);
    LOAD_DEVICE(vkGetImageMemoryRequirements);
    LOAD_DEVICE(vkGetImageMemoryRequirements2);
    LOAD_DEVICE(vkGetImageSubresourceLayout);
    LOAD_DEVICE(vkAllocateMemory);
    LOAD_DEVICE(vkFreeMemory);
    LOAD_DEVICE(vkBindBufferMemory);
    LOAD_DEVICE(vkBindImageMemory);
    LOAD_DEVICE(vkBindImageMemory2);
    LOAD_DEVICE(vkMapMemory);
    LOAD_DEVICE(vkUnmapMemory);
    LOAD_DEVICE(vkFlushMappedMemoryRanges);
    LOAD_DEVICE(vkInvalidateMappedMemoryRanges);

    LOAD_DEVICE(vkCreateImageView);
    LOAD_DEVICE(vkDestroyImageView);
    LOAD_DEVICE(vkCreateSampler);
    LOAD_DEVICE(vkDestroySampler);

    LOAD_DEVICE(vkCmdPipelineBarrier);
    LOAD_DEVICE(vkCmdCopyBufferToImage);
    LOAD_DEVICE(vkCmdCopyImageToBuffer);
    LOAD_DEVICE(vkCmdCopyBuffer);
    LOAD_DEVICE(vkCmdCopyImage);

    // External Memory & DMA-BUF Extensions
    LOAD_DEVICE(vkGetMemoryFdPropertiesKHR);

    // YCbCr Conversion
    LOAD_DEVICE(vkCreateSamplerYcbcrConversion);
    LOAD_DEVICE(vkDestroySamplerYcbcrConversion);

    // Vulkan Video Core & Decode
    LOAD_DEVICE(vkCreateVideoSessionKHR);
    LOAD_DEVICE(vkDestroyVideoSessionKHR);
    LOAD_DEVICE(vkGetVideoSessionMemoryRequirementsKHR);
    LOAD_DEVICE(vkBindVideoSessionMemoryKHR);
    LOAD_DEVICE(vkCreateVideoSessionParametersKHR);
    LOAD_DEVICE(vkDestroyVideoSessionParametersKHR);
    LOAD_DEVICE(vkUpdateVideoSessionParametersKHR);
    LOAD_DEVICE(vkCmdBeginVideoCodingKHR);
    LOAD_DEVICE(vkCmdEndVideoCodingKHR);
    LOAD_DEVICE(vkCmdControlVideoCodingKHR);
    LOAD_DEVICE(vkCmdDecodeVideoKHR);

#undef LOAD_DEVICE

    return vkDestroyDevice && vkCreateCommandPool && vkCreateImage;
}

} // namespace atum::vk

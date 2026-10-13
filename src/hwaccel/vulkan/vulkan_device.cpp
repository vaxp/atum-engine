#include "atum/hwaccel/vulkan/vulkan_device.hpp"
#include "atum/hwaccel/vulkan/vulkan_image_importer.hpp"

#include <cstring>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace atum::vk {

namespace {

bool check_extension_supported(const std::vector<VkExtensionProperties>& available, const char* name) noexcept {
    for (const auto& ext : available) {
        if (std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

VulkanDevice::VulkanDevice() = default;

VulkanDevice::~VulkanDevice() {
    close_internal();
}

std::vector<PhysicalDeviceInfo> VulkanDevice::probe_devices() {
    std::vector<PhysicalDeviceInfo> results;

    VulkanDispatcher disp;
    if (!disp.load_global_symbols()) {
        return results;
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Atum Multimedia Engine";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Atum";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo inst_create_info{};
    inst_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_create_info.pApplicationInfo = &app_info;

    VkInstance inst = VK_NULL_HANDLE;
    VkResult res = disp.vkCreateInstance(&inst_create_info, nullptr, &inst);
    if (res != VK_SUCCESS || !inst) {
        return results;
    }

    if (!disp.load_instance_symbols(inst)) {
        disp.vkDestroyInstance(inst, nullptr);
        return results;
    }

    uint32_t device_count = 0;
    disp.vkEnumeratePhysicalDevices(inst, &device_count, nullptr);
    if (device_count == 0) {
        disp.vkDestroyInstance(inst, nullptr);
        return results;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    disp.vkEnumeratePhysicalDevices(inst, &device_count, devices.data());

    for (uint32_t i = 0; i < device_count; ++i) {
        VkPhysicalDevice dev = devices[i];
        VkPhysicalDeviceProperties props{};
        disp.vkGetPhysicalDeviceProperties(dev, &props);

        PhysicalDeviceInfo info{};
        info.device_index = i;
        info.device_name = props.deviceName;
        info.vendor_id = props.vendorID;
        info.device_id = props.deviceID;
        info.device_type = props.deviceType;
        info.api_version = props.apiVersion;
        info.driver_version = props.driverVersion;

        // Query queue families
        uint32_t q_family_count = 0;
        disp.vkGetPhysicalDeviceQueueFamilyProperties(dev, &q_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> q_props(q_family_count);
        disp.vkGetPhysicalDeviceQueueFamilyProperties(dev, &q_family_count, q_props.data());

        for (uint32_t q = 0; q < q_family_count; ++q) {
            if ((q_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && info.graphics_queue_family < 0) {
                info.graphics_queue_family = static_cast<int32_t>(q);
            }
            if ((q_props[q].queueFlags & VK_QUEUE_COMPUTE_BIT) && info.compute_queue_family < 0) {
                info.compute_queue_family = static_cast<int32_t>(q);
            }
            if ((q_props[q].queueFlags & VK_QUEUE_TRANSFER_BIT) && info.transfer_queue_family < 0) {
                info.transfer_queue_family = static_cast<int32_t>(q);
            }
            // Check for VK_QUEUE_VIDEO_DECODE_BIT_KHR (0x00000020)
            if (q_props[q].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {
                info.video_decode_queue_family = static_cast<int32_t>(q);
            }
        }

        // Query device extensions
        uint32_t ext_count = 0;
        disp.vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> extensions(ext_count);
        disp.vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, extensions.data());

        info.supports_external_memory_fd = check_extension_supported(extensions, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        info.supports_external_memory_dma_buf = check_extension_supported(extensions, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        info.supports_image_drm_format_modifier = check_extension_supported(extensions, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
        info.supports_sampler_ycbcr_conversion = check_extension_supported(extensions, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
        info.supports_video_queue = check_extension_supported(extensions, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        info.supports_video_decode_queue = check_extension_supported(extensions, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        info.supports_video_decode_h264 = check_extension_supported(extensions, VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
        info.supports_video_decode_h265 = check_extension_supported(extensions, VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME);

        results.push_back(std::move(info));
    }

    disp.vkDestroyInstance(inst, nullptr);
    return results;
}

std::unique_ptr<VulkanDevice> VulkanDevice::create(int device_index, bool prefer_video_decode) {
    auto dev = std::make_unique<VulkanDevice>();
    if (!dev->init_internal(device_index, prefer_video_decode)) {
        return nullptr;
    }
    return dev;
}

bool VulkanDevice::init_internal(int device_index, bool prefer_video_decode) noexcept {
    close_internal();

    if (!dispatcher_.load_global_symbols()) {
        return false;
    }

    // 1. Create Instance
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Atum Multimedia Engine";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Atum";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    // Check available instance extensions
    uint32_t inst_ext_count = 0;
    dispatcher_.vkEnumerateInstanceExtensionProperties(nullptr, &inst_ext_count, nullptr);
    std::vector<VkExtensionProperties> inst_exts(inst_ext_count);
    dispatcher_.vkEnumerateInstanceExtensionProperties(nullptr, &inst_ext_count, inst_exts.data());

    std::vector<const char*> enabled_inst_extensions;
    if (check_extension_supported(inst_exts, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
        enabled_inst_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }

    VkInstanceCreateInfo inst_create_info{};
    inst_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_create_info.pApplicationInfo = &app_info;
    inst_create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_inst_extensions.size());
    inst_create_info.ppEnabledExtensionNames = enabled_inst_extensions.data();

    VkResult res = dispatcher_.vkCreateInstance(&inst_create_info, nullptr, &instance_);
    if (res != VK_SUCCESS || !instance_) {
        return false;
    }

    if (!dispatcher_.load_instance_symbols(instance_)) {
        close_internal();
        return false;
    }

    // 2. Enumerate Physical Devices
    uint32_t dev_count = 0;
    dispatcher_.vkEnumeratePhysicalDevices(instance_, &dev_count, nullptr);
    if (dev_count == 0) {
        close_internal();
        return false;
    }

    std::vector<VkPhysicalDevice> physical_devices(dev_count);
    dispatcher_.vkEnumeratePhysicalDevices(instance_, &dev_count, physical_devices.data());

    // Probe physical devices to inspect info
    std::vector<PhysicalDeviceInfo> discovered;
    for (uint32_t i = 0; i < dev_count; ++i) {
        VkPhysicalDevice p_dev = physical_devices[i];
        VkPhysicalDeviceProperties props{};
        dispatcher_.vkGetPhysicalDeviceProperties(p_dev, &props);

        PhysicalDeviceInfo info{};
        info.device_index = i;
        info.device_name = props.deviceName;
        info.vendor_id = props.vendorID;
        info.device_id = props.deviceID;
        info.device_type = props.deviceType;
        info.api_version = props.apiVersion;
        info.driver_version = props.driverVersion;

        uint32_t q_count = 0;
        dispatcher_.vkGetPhysicalDeviceQueueFamilyProperties(p_dev, &q_count, nullptr);
        std::vector<VkQueueFamilyProperties> q_props(q_count);
        dispatcher_.vkGetPhysicalDeviceQueueFamilyProperties(p_dev, &q_count, q_props.data());

        for (uint32_t q = 0; q < q_count; ++q) {
            if ((q_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && info.graphics_queue_family < 0) {
                info.graphics_queue_family = static_cast<int32_t>(q);
            }
            if ((q_props[q].queueFlags & VK_QUEUE_COMPUTE_BIT) && info.compute_queue_family < 0) {
                info.compute_queue_family = static_cast<int32_t>(q);
            }
            if ((q_props[q].queueFlags & VK_QUEUE_TRANSFER_BIT) && info.transfer_queue_family < 0) {
                info.transfer_queue_family = static_cast<int32_t>(q);
            }
            if (q_props[q].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {
                info.video_decode_queue_family = static_cast<int32_t>(q);
            }
        }

        uint32_t ext_c = 0;
        dispatcher_.vkEnumerateDeviceExtensionProperties(p_dev, nullptr, &ext_c, nullptr);
        std::vector<VkExtensionProperties> extensions(ext_c);
        dispatcher_.vkEnumerateDeviceExtensionProperties(p_dev, nullptr, &ext_c, extensions.data());

        info.supports_external_memory_fd = check_extension_supported(extensions, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        info.supports_external_memory_dma_buf = check_extension_supported(extensions, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        info.supports_image_drm_format_modifier = check_extension_supported(extensions, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
        info.supports_sampler_ycbcr_conversion = check_extension_supported(extensions, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
        info.supports_video_queue = check_extension_supported(extensions, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        info.supports_video_decode_queue = check_extension_supported(extensions, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        info.supports_video_decode_h264 = check_extension_supported(extensions, VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
        info.supports_video_decode_h265 = check_extension_supported(extensions, VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME);

        discovered.push_back(std::move(info));
    }

    // 3. Select Target Physical Device
    uint32_t chosen_index = 0;
    if (device_index >= 0 && static_cast<size_t>(device_index) < discovered.size()) {
        chosen_index = static_cast<uint32_t>(device_index);
    } else if (prefer_video_decode) {
        bool found = false;
        for (const auto& d : discovered) {
            if (d.video_decode_queue_family >= 0 && d.supports_video_decode_queue) {
                chosen_index = d.device_index;
                found = true;
                break;
            }
        }
        if (!found) {
            // Pick discrete GPU if available, else first GPU with dma_buf
            for (const auto& d : discovered) {
                if (d.device_type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    chosen_index = d.device_index;
                    break;
                }
            }
        }
    } else {
        // Prefer device supporting DMA-BUF import (e.g. Intel UHD / AMD Radeon)
        for (const auto& d : discovered) {
            if (d.supports_external_memory_dma_buf && d.device_type != VK_PHYSICAL_DEVICE_TYPE_CPU) {
                chosen_index = d.device_index;
                break;
            }
        }
    }

    physical_device_ = physical_devices[chosen_index];
    info_ = discovered[chosen_index];
    device_name_string_ = "Vulkan [" + info_.device_name + "]";

    // Query device extensions supported by the chosen physical device
    uint32_t dev_ext_count = 0;
    dispatcher_.vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &dev_ext_count, nullptr);
    std::vector<VkExtensionProperties> dev_exts(dev_ext_count);
    dispatcher_.vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &dev_ext_count, dev_exts.data());

    // 4. Configure Queues
    std::set<uint32_t> unique_queue_families;
    if (info_.graphics_queue_family >= 0) {
        unique_queue_families.insert(static_cast<uint32_t>(info_.graphics_queue_family));
    }
    if (info_.video_decode_queue_family >= 0) {
        unique_queue_families.insert(static_cast<uint32_t>(info_.video_decode_queue_family));
    }
    if (info_.compute_queue_family >= 0) {
        unique_queue_families.insert(static_cast<uint32_t>(info_.compute_queue_family));
    }

    float queue_priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    for (uint32_t family : unique_queue_families) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &queue_priority;
        queue_create_infos.push_back(qci);
    }

    // 5. Enable Device Extensions
    std::vector<const char*> enabled_device_extensions;
    auto try_enable = [&](const char* ext_name) {
        if (check_extension_supported(dev_exts, ext_name)) {
            enabled_device_extensions.push_back(ext_name);
            return true;
        }
        return false;
    };

    // External memory & DMA-BUF
    try_enable(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    try_enable(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    try_enable(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    try_enable(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
    try_enable(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
    try_enable(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
    try_enable(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
    try_enable(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    try_enable(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);

    // Vulkan Video Decode extensions if supported
    if (info_.video_decode_queue_family >= 0 && info_.supports_video_decode_queue) {
        try_enable(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        try_enable(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        try_enable(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
        try_enable(VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME);
        try_enable("VK_KHR_video_maintenance1");
    }

    // Enable features (Sampler YCbCr conversion)
    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr_features{};
    ycbcr_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
    ycbcr_features.samplerYcbcrConversion = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &ycbcr_features;

    VkDeviceCreateInfo dev_create_info{};
    dev_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_create_info.pNext = &features2;
    dev_create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
    dev_create_info.pQueueCreateInfos = queue_create_infos.data();
    dev_create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_device_extensions.size());
    dev_create_info.ppEnabledExtensionNames = enabled_device_extensions.data();

    res = dispatcher_.vkCreateDevice(physical_device_, &dev_create_info, nullptr, &device_);
    if (res != VK_SUCCESS || !device_) {
        // Fallback without pNext features2 if driver strictly requires simple VkPhysicalDeviceFeatures
        VkDeviceCreateInfo fallback_ci = dev_create_info;
        fallback_ci.pNext = nullptr;
        res = dispatcher_.vkCreateDevice(physical_device_, &fallback_ci, nullptr, &device_);
        if (res != VK_SUCCESS || !device_) {
            close_internal();
            return false;
        }
    }

    if (!dispatcher_.load_device_symbols(device_)) {
        close_internal();
        return false;
    }

    // Retrieve Queue Handles
    if (info_.graphics_queue_family >= 0) {
        dispatcher_.vkGetDeviceQueue(device_, static_cast<uint32_t>(info_.graphics_queue_family), 0, &graphics_queue_);
    }
    if (info_.video_decode_queue_family >= 0) {
        dispatcher_.vkGetDeviceQueue(device_, static_cast<uint32_t>(info_.video_decode_queue_family), 0, &video_decode_queue_);
    }
    if (info_.compute_queue_family >= 0) {
        dispatcher_.vkGetDeviceQueue(device_, static_cast<uint32_t>(info_.compute_queue_family), 0, &compute_queue_);
    }

    // Query physical device memory properties
    dispatcher_.vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties_);

    // 6. Create Command Pools
    if (info_.graphics_queue_family >= 0) {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = static_cast<uint32_t>(info_.graphics_queue_family);
        dispatcher_.vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_);
    }

    if (info_.video_decode_queue_family >= 0 && info_.video_decode_queue_family != info_.graphics_queue_family) {
        VkCommandPoolCreateInfo vpool_info{};
        vpool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        vpool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vpool_info.queueFamilyIndex = static_cast<uint32_t>(info_.video_decode_queue_family);
        dispatcher_.vkCreateCommandPool(device_, &vpool_info, nullptr, &video_command_pool_);
    } else {
        video_command_pool_ = command_pool_;
    }

    is_initialized_ = true;
    return true;
}

void VulkanDevice::close_internal() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        dispatcher_.vkDeviceWaitIdle(device_);

        if (video_command_pool_ != VK_NULL_HANDLE && video_command_pool_ != command_pool_) {
            dispatcher_.vkDestroyCommandPool(device_, video_command_pool_, nullptr);
            video_command_pool_ = VK_NULL_HANDLE;
        }

        if (command_pool_ != VK_NULL_HANDLE) {
            dispatcher_.vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }

        dispatcher_.vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        dispatcher_.vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    graphics_queue_ = VK_NULL_HANDLE;
    video_decode_queue_ = VK_NULL_HANDLE;
    compute_queue_ = VK_NULL_HANDLE;
    physical_device_ = VK_NULL_HANDLE;
    is_initialized_ = false;
}

uint32_t VulkanDevice::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    for (uint32_t i = 0; i < memory_properties_.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) &&
            (memory_properties_.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    // Fallback: return any matching type bit
    for (uint32_t i = 0; i < memory_properties_.memoryTypeCount; ++i) {
        if (type_filter & (1 << i)) {
            return i;
        }
    }
    return 0;
}

VkCommandBuffer VulkanDevice::begin_single_time_commands() const {
    if (!command_pool_) return VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool_;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    dispatcher_.vkAllocateCommandBuffers(device_, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    dispatcher_.vkBeginCommandBuffer(cmd, &begin_info);

    return cmd;
}

void VulkanDevice::end_single_time_commands(VkCommandBuffer command_buffer) const {
    if (!command_buffer || !command_pool_ || !graphics_queue_) return;

    dispatcher_.vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    std::lock_guard<std::mutex> lock(queue_mutex_);
    dispatcher_.vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
    dispatcher_.vkQueueWaitIdle(graphics_queue_);

    dispatcher_.vkFreeCommandBuffers(device_, command_pool_, 1, &command_buffer);
}

void VulkanDevice::transition_image_layout(VkImage image, VkFormat /*format*/,
                                          VkImageLayout old_layout, VkImageLayout new_layout,
                                          uint32_t aspect_mask) const {
    VkCommandBuffer cmd = begin_single_time_commands();
    if (!cmd) return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags source_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags destination_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    }

    dispatcher_.vkCmdPipelineBarrier(
        cmd,
        source_stage, destination_stage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    end_single_time_commands(cmd);
}

void VulkanDevice::wait_idle() const {
    if (device_) {
        dispatcher_.vkDeviceWaitIdle(device_);
    }
}

MediaResult<void> VulkanDevice::export_buffer(const VideoFrame& /*frame*/, HwBufferDescriptor& /*desc*/) {
    // Vulkan frame buffer export can be performed via VulkanImageImporter
    return MediaResult<void>::ok();
}

MediaResult<VideoFrame> VulkanDevice::import_buffer(const HwBufferDescriptor& desc) {
    if (desc.fds[0] < 0) {
        return MediaError::InvalidData;
    }
    // Zero-copy import via VulkanImageImporter
    VulkanImageImporter importer(*this);
    PixelFormat fmt = (desc.drm_fourcc == 0x3231564e /* DRM_FORMAT_NV12 */) ? PixelFormat::NV12 : PixelFormat::RGBA32;
    return importer.import_dma_buf(desc.fds[0], desc.width, desc.height, desc.strides[0], fmt);
}

} // namespace atum::vk

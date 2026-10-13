#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <string>
#include <vector>
#include <optional>
#include <memory>

#include "atum/core/media_types.hpp"
#include "atum/hwaccel/hwaccel.hpp"
#include "atum/surface/pixel_format.hpp"

// Khronos Vulkan Core & Video Headers
#include <vulkan/vulkan.h>
#include <vk_video/vulkan_video_codecs_common.h>
#include <vk_video/vulkan_video_codec_h264std.h>
#include <vk_video/vulkan_video_codec_h264std_decode.h>
#include <vk_video/vulkan_video_codec_h265std.h>
#include <vk_video/vulkan_video_codec_h265std_decode.h>

namespace atum::vk {

/// Translates Vulkan VkResult codes to human-readable string view.
constexpr std::string_view result_to_string(VkResult result) noexcept {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        default: return "VK_UNKNOWN_ERROR";
    }
}

/// Hardware properties and capabilities discovered for a physical GPU.
struct PhysicalDeviceInfo {
    uint32_t device_index{0};
    std::string device_name;
    uint32_t vendor_id{0};
    uint32_t device_id{0};
    VkPhysicalDeviceType device_type{VK_PHYSICAL_DEVICE_TYPE_OTHER};
    uint32_t api_version{0};
    uint32_t driver_version{0};

    // Queue family indices (-1 if not present)
    int32_t graphics_queue_family{-1};
    int32_t compute_queue_family{-1};
    int32_t transfer_queue_family{-1};
    int32_t video_decode_queue_family{-1};

    // Extension support flags
    bool supports_external_memory_fd{false};
    bool supports_external_memory_dma_buf{false};
    bool supports_image_drm_format_modifier{false};
    bool supports_sampler_ycbcr_conversion{false};
    bool supports_video_queue{false};
    bool supports_video_decode_queue{false};
    bool supports_video_decode_h264{false};
    bool supports_video_decode_h265{false};
};

/// Maps Atum engine PixelFormat to standard Vulkan VkFormat.
constexpr VkFormat pixel_format_to_vk_format(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::RGBA32:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::RGB24:
            return VK_FORMAT_R8G8B8_UNORM;
        case PixelFormat::NV12:
            return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        case PixelFormat::YUV420P:
            return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
        case PixelFormat::P010:
            return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
        case PixelFormat::YUV420P10:
            return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

/// Maps Vulkan VkFormat back to Atum engine PixelFormat.
constexpr PixelFormat vk_format_to_pixel_format(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
            return PixelFormat::RGBA32;
        case VK_FORMAT_R8G8B8_UNORM:
            return PixelFormat::RGB24;
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
            return PixelFormat::NV12;
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
            return PixelFormat::YUV420P;
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
            return PixelFormat::P010;
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
            return PixelFormat::YUV420P10;
        default:
            return PixelFormat::Unknown;
    }
}

} // namespace atum::vk

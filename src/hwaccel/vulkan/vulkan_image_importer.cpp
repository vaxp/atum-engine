#include "atum/hwaccel/vulkan/vulkan_image_importer.hpp"

#include <unistd.h>
#include <cstring>

namespace atum::vk {

VulkanTexture::VulkanTexture(const VulkanDevice& device)
    : device_(&device) {}

VulkanTexture::~VulkanTexture() {
    reset();
}

VulkanTexture::VulkanTexture(VulkanTexture&& other) noexcept {
    *this = std::move(other);
}

VulkanTexture& VulkanTexture::operator=(VulkanTexture&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = other.device_;
        image_ = other.image_;
        memory_ = other.memory_;
        image_view_ = other.image_view_;
        sampler_ = other.sampler_;
        ycbcr_conversion_ = other.ycbcr_conversion_;
        width_ = other.width_;
        height_ = other.height_;
        format_ = other.format_;

        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.image_view_ = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
        other.ycbcr_conversion_ = VK_NULL_HANDLE;
        other.width_ = 0;
        other.height_ = 0;
        other.format_ = VK_FORMAT_UNDEFINED;
    }
    return *this;
}

void VulkanTexture::reset() noexcept {
    if (!device_ || !device_->device()) return;

    VkDevice dev = device_->device();
    const auto& disp = device_->dispatcher();

    if (image_view_ != VK_NULL_HANDLE) {
        disp.vkDestroyImageView(dev, image_view_, nullptr);
        image_view_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        disp.vkDestroySampler(dev, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (ycbcr_conversion_ != VK_NULL_HANDLE && disp.vkDestroySamplerYcbcrConversion) {
        disp.vkDestroySamplerYcbcrConversion(dev, ycbcr_conversion_, nullptr);
        ycbcr_conversion_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        disp.vkDestroyImage(dev, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        disp.vkFreeMemory(dev, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

MediaResult<VideoFrame> VulkanTexture::read_pixels_to_frame() const {
    if (!device_ || !image_ || width_ == 0 || height_ == 0) {
        return MediaError::InvalidState;
    }

    VkDevice dev = device_->device();
    const auto& disp = device_->dispatcher();

    PixelFormat px_fmt = vk_format_to_pixel_format(format_);
    if (px_fmt == PixelFormat::Unknown) {
        px_fmt = PixelFormat::RGBA32;
    }

    uint32_t bpp = (px_fmt == PixelFormat::RGBA32) ? 4 : 1;
    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width_) * height_ * bpp;
    if (px_fmt == PixelFormat::NV12) {
        buffer_size = static_cast<VkDeviceSize>(width_) * height_ * 3 / 2;
    }

    // 1. Create Staging Buffer
    VkBufferCreateInfo buf_ci{};
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size = buffer_size;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer staging_buf = VK_NULL_HANDLE;
    if (disp.vkCreateBuffer(dev, &buf_ci, nullptr, &staging_buf) != VK_SUCCESS) {
        return MediaError::HardwareError;
    }

    VkMemoryRequirements mem_reqs{};
    disp.vkGetBufferMemoryRequirements(dev, staging_buf, &mem_reqs);

    uint32_t mem_type = device_->find_memory_type(
        mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type;

    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (disp.vkAllocateMemory(dev, &alloc_info, nullptr, &staging_mem) != VK_SUCCESS) {
        disp.vkDestroyBuffer(dev, staging_buf, nullptr);
        return MediaError::AllocationFailed;
    }

    disp.vkBindBufferMemory(dev, staging_buf, staging_mem, 0);

    // 2. Transition Image layout to TRANSFER_SRC_OPTIMAL
    device_->transition_image_layout(
        image_, format_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );

    // 3. Copy Image to Buffer
    VkCommandBuffer cmd = device_->begin_single_time_commands();
    if (cmd) {
        if (px_fmt == PixelFormat::NV12) {
            VkBufferImageCopy copy_regions[2]{};

            // Plane 0: Y plane
            copy_regions[0].bufferOffset = 0;
            copy_regions[0].bufferRowLength = width_;
            copy_regions[0].bufferImageHeight = height_;
            copy_regions[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
            copy_regions[0].imageSubresource.mipLevel = 0;
            copy_regions[0].imageSubresource.baseArrayLayer = 0;
            copy_regions[0].imageSubresource.layerCount = 1;
            copy_regions[0].imageOffset = {0, 0, 0};
            copy_regions[0].imageExtent = {width_, height_, 1};

            // Plane 1: UV plane (half width, half height)
            copy_regions[1].bufferOffset = static_cast<VkDeviceSize>(width_) * height_;
            copy_regions[1].bufferRowLength = width_ / 2;
            copy_regions[1].bufferImageHeight = height_ / 2;
            copy_regions[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
            copy_regions[1].imageSubresource.mipLevel = 0;
            copy_regions[1].imageSubresource.baseArrayLayer = 0;
            copy_regions[1].imageSubresource.layerCount = 1;
            copy_regions[1].imageOffset = {0, 0, 0};
            copy_regions[1].imageExtent = {width_ / 2, height_ / 2, 1};

            disp.vkCmdCopyImageToBuffer(
                cmd,
                image_,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                staging_buf,
                2, copy_regions
            );
        } else {
            VkBufferImageCopy copy_region{};
            copy_region.bufferOffset = 0;
            copy_region.bufferRowLength = 0;
            copy_region.bufferImageHeight = 0;
            copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.mipLevel = 0;
            copy_region.imageSubresource.baseArrayLayer = 0;
            copy_region.imageSubresource.layerCount = 1;
            copy_region.imageOffset = {0, 0, 0};
            copy_region.imageExtent = {width_, height_, 1};

            disp.vkCmdCopyImageToBuffer(
                cmd,
                image_,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                staging_buf,
                1, &copy_region
            );
        }

        device_->end_single_time_commands(cmd);
    }

    // 4. Map and populate VideoFrame
    VideoFrame frame = VideoFrame::allocate(width_, height_, px_fmt);
    void* mapped = nullptr;
    if (disp.vkMapMemory(dev, staging_mem, 0, buffer_size, 0, &mapped) == VK_SUCCESS) {
        if (px_fmt == PixelFormat::NV12) {
            const auto* src_y = static_cast<const uint8_t*>(mapped);
            const auto* src_uv = src_y + (width_ * height_);
            auto* dst_y = frame.plane_mut(0);
            auto* dst_uv = frame.plane_mut(1);

            for (uint32_t y = 0; y < height_; ++y) {
                std::memcpy(dst_y + (y * frame.stride(0)), src_y + (y * width_), width_);
            }
            for (uint32_t y = 0; y < height_ / 2; ++y) {
                std::memcpy(dst_uv + (y * frame.stride(1)), src_uv + (y * width_), width_);
            }
        } else {
            auto* dst = frame.plane_mut(0);
            const auto* src = static_cast<const uint8_t*>(mapped);
            for (uint32_t y = 0; y < height_; ++y) {
                std::memcpy(dst + (y * frame.stride(0)), src + (y * width_ * bpp), width_ * bpp);
            }
        }
        disp.vkUnmapMemory(dev, staging_mem);
    }

    // Clean up staging resources
    disp.vkDestroyBuffer(dev, staging_buf, nullptr);
    disp.vkFreeMemory(dev, staging_mem, nullptr);

    return frame;
}

MediaResult<std::shared_ptr<VulkanTexture>> VulkanImageImporter::import_dma_buf_texture(
    int dma_buf_fd,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    PixelFormat format
) {
    if (dma_buf_fd < 0 || width == 0 || height == 0) {
        return MediaError::InvalidData;
    }

    VkDevice dev = device_.device();
    const auto& disp = device_.dispatcher();
    VkFormat vk_fmt = pixel_format_to_vk_format(format);

    auto tex = std::make_shared<VulkanTexture>(device_);
    tex->width_ = width;
    tex->height_ = height;
    tex->format_ = vk_fmt;

    // 1. External Memory Image Create Info
    VkExternalMemoryImageCreateInfo ext_image_ci{};
    ext_image_ci.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    ext_image_ci.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo image_ci{};
    image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_ci.pNext = &ext_image_ci;
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = vk_fmt;
    image_ci.extent = {width, height, 1};
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling = VK_IMAGE_TILING_LINEAR;
    image_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = disp.vkCreateImage(dev, &image_ci, nullptr, &tex->image_);
    if (res != VK_SUCCESS || !tex->image_) {
        // Fallback to VK_IMAGE_TILING_OPTIMAL if linear tiling is not supported
        image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        res = disp.vkCreateImage(dev, &image_ci, nullptr, &tex->image_);
        if (res != VK_SUCCESS || !tex->image_) {
            return MediaError::HardwareError;
        }
    }

    // 2. Query Memory Requirements
    VkMemoryRequirements mem_reqs{};
    disp.vkGetImageMemoryRequirements(dev, tex->image_, &mem_reqs);

    uint32_t mem_type_bits = mem_reqs.memoryTypeBits;
    if (disp.vkGetMemoryFdPropertiesKHR) {
        VkMemoryFdPropertiesKHR fd_props{};
        fd_props.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
        if (disp.vkGetMemoryFdPropertiesKHR(
                dev,
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                dma_buf_fd,
                &fd_props) == VK_SUCCESS) {
            if ((mem_type_bits & fd_props.memoryTypeBits) != 0) {
                mem_type_bits &= fd_props.memoryTypeBits;
            } else if (fd_props.memoryTypeBits != 0) {
                mem_type_bits = fd_props.memoryTypeBits;
            }
        }
    }

    int dup_fd = dup(dma_buf_fd);
    if (dup_fd < 0) {
        return MediaError::IoError;
    }

    VkImportMemoryFdInfoKHR import_info{};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import_info.fd = dup_fd;

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = &import_info;
    alloc_info.allocationSize = (mem_reqs.size > 0) ? mem_reqs.size : (static_cast<VkDeviceSize>(stride) * height * 2);
    alloc_info.memoryTypeIndex = device_.find_memory_type(mem_type_bits, 0);

    res = disp.vkAllocateMemory(dev, &alloc_info, nullptr, &tex->memory_);
    if (res != VK_SUCCESS || !tex->memory_) {
        close(dup_fd);
        return MediaError::AllocationFailed;
    }

    res = disp.vkBindImageMemory(dev, tex->image_, tex->memory_, 0);
    if (res != VK_SUCCESS) {
        return MediaError::HardwareError;
    }

    // 3. YCbCr Conversion (if format is YUV / NV12 / P010)
    void* sampler_pnext = nullptr;
    void* view_pnext = nullptr;
    VkSamplerYcbcrConversionInfo conv_info{};

    if ((format == PixelFormat::NV12 || format == PixelFormat::YUV420P || format == PixelFormat::P010)
        && disp.vkCreateSamplerYcbcrConversion) {
        VkSamplerYcbcrConversionCreateInfo conv_ci{};
        conv_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
        conv_ci.format = vk_fmt;
        conv_ci.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
        conv_ci.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
        conv_ci.xChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
        conv_ci.yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
        conv_ci.chromaFilter = VK_FILTER_LINEAR;
        conv_ci.forceExplicitReconstruction = VK_FALSE;

        if (disp.vkCreateSamplerYcbcrConversion(dev, &conv_ci, nullptr, &tex->ycbcr_conversion_) == VK_SUCCESS) {
            conv_info.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
            conv_info.conversion = tex->ycbcr_conversion_;
            sampler_pnext = &conv_info;
            view_pnext = &conv_info;
        }
    }

    // 4. Create Sampler
    VkSamplerCreateInfo sampler_ci{};
    sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.pNext = sampler_pnext;
    sampler_ci.magFilter = VK_FILTER_LINEAR;
    sampler_ci.minFilter = VK_FILTER_LINEAR;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.maxLod = 1.0f;

    disp.vkCreateSampler(dev, &sampler_ci, nullptr, &tex->sampler_);

    // 5. Create ImageView
    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.pNext = view_pnext;
    view_ci.image = tex->image_;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = vk_fmt;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.baseMipLevel = 0;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.baseArrayLayer = 0;
    view_ci.subresourceRange.layerCount = 1;

    disp.vkCreateImageView(dev, &view_ci, nullptr, &tex->image_view_);

    return tex;
}

MediaResult<VideoFrame> VulkanImageImporter::import_dma_buf(
    int dma_buf_fd,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    PixelFormat format
) {
    auto tex_res = import_dma_buf_texture(dma_buf_fd, width, height, stride, format);
    if (!tex_res.is_ok()) {
        return tex_res.error();
    }

    auto tex = tex_res.value();
    VideoFrame frame = VideoFrame::allocate(width, height, format);
    frame.set_hw_opaque_handle(reinterpret_cast<uint64_t>(tex->image()));
    frame.set_dma_buf_fd(dma_buf_fd);

    return frame;
}

MediaResult<std::shared_ptr<VulkanTexture>> VulkanImageImporter::create_texture(
    uint32_t width,
    uint32_t height,
    PixelFormat format
) {
    if (width == 0 || height == 0) {
        return MediaError::InvalidData;
    }

    VkDevice dev = device_.device();
    const auto& disp = device_.dispatcher();
    VkFormat vk_fmt = pixel_format_to_vk_format(format);

    auto tex = std::make_shared<VulkanTexture>(device_);
    tex->width_ = width;
    tex->height_ = height;
    tex->format_ = vk_fmt;

    VkImageCreateInfo image_ci{};
    image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = vk_fmt;
    image_ci.extent = {width, height, 1};
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = disp.vkCreateImage(dev, &image_ci, nullptr, &tex->image_);
    if (res != VK_SUCCESS || !tex->image_) {
        return MediaError::HardwareError;
    }

    VkMemoryRequirements mem_reqs{};
    disp.vkGetImageMemoryRequirements(dev, tex->image_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device_.find_memory_type(
        mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    res = disp.vkAllocateMemory(dev, &alloc_info, nullptr, &tex->memory_);
    if (res != VK_SUCCESS || !tex->memory_) {
        return MediaError::AllocationFailed;
    }

    disp.vkBindImageMemory(dev, tex->image_, tex->memory_, 0);

    // Transition to general/shader read optimal
    device_.transition_image_layout(
        tex->image_, vk_fmt,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    // Create default sampler
    VkSamplerCreateInfo sampler_ci{};
    sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.magFilter = VK_FILTER_LINEAR;
    sampler_ci.minFilter = VK_FILTER_LINEAR;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    disp.vkCreateSampler(dev, &sampler_ci, nullptr, &tex->sampler_);

    // Create ImageView
    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = tex->image_;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = vk_fmt;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;
    disp.vkCreateImageView(dev, &view_ci, nullptr, &tex->image_view_);

    return tex;
}

MediaResult<std::shared_ptr<VulkanTexture>> VulkanImageImporter::upload_frame_texture(const VideoFrame& frame) {
    if (!frame.is_valid()) {
        return MediaError::InvalidData;
    }

    uint32_t width = frame.width();
    uint32_t height = frame.height();
    PixelFormat px_fmt = frame.pixel_format();
    VkFormat vk_fmt = pixel_format_to_vk_format(px_fmt);
    if (vk_fmt == VK_FORMAT_UNDEFINED) {
        return MediaError::UnsupportedFormat;
    }

    VkDevice dev = device_.device();
    const auto& disp = device_.dispatcher();

    uint32_t bpp = (px_fmt == PixelFormat::RGBA32) ? 4 : 1;
    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width) * height * bpp;
    if (px_fmt == PixelFormat::NV12) {
        buffer_size = static_cast<VkDeviceSize>(width) * height * 3 / 2;
    }

    // 1. Create Staging Buffer
    VkBufferCreateInfo buf_ci{};
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size = buffer_size;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer staging_buf = VK_NULL_HANDLE;
    if (disp.vkCreateBuffer(dev, &buf_ci, nullptr, &staging_buf) != VK_SUCCESS) {
        return MediaError::HardwareError;
    }

    VkMemoryRequirements mem_reqs{};
    disp.vkGetBufferMemoryRequirements(dev, staging_buf, &mem_reqs);

    uint32_t mem_type = device_.find_memory_type(
        mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type;

    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (disp.vkAllocateMemory(dev, &alloc_info, nullptr, &staging_mem) != VK_SUCCESS) {
        disp.vkDestroyBuffer(dev, staging_buf, nullptr);
        return MediaError::AllocationFailed;
    }

    disp.vkBindBufferMemory(dev, staging_buf, staging_mem, 0);

    // 2. Copy Frame Pixels to Staging Buffer
    void* mapped = nullptr;
    if (disp.vkMapMemory(dev, staging_mem, 0, buffer_size, 0, &mapped) == VK_SUCCESS) {
        if (px_fmt == PixelFormat::NV12) {
            auto* dst_y = static_cast<uint8_t*>(mapped);
            auto* dst_uv = dst_y + (width * height);
            const auto* src_y = frame.plane(0);
            const auto* src_uv = frame.plane(1);

            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(dst_y + (y * width), src_y + (y * frame.stride(0)), width);
            }
            for (uint32_t y = 0; y < height / 2; ++y) {
                std::memcpy(dst_uv + (y * width), src_uv + (y * frame.stride(1)), width);
            }
        } else if (px_fmt == PixelFormat::RGBA32) {
            auto* dst = static_cast<uint8_t*>(mapped);
            const auto* src = frame.plane(0);
            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(dst + (y * width * 4), src + (y * frame.stride(0)), width * 4);
            }
        }
        disp.vkUnmapMemory(dev, staging_mem);
    }

    // 3. Create GPU Texture with VK_IMAGE_TILING_OPTIMAL
    auto tex = std::make_shared<VulkanTexture>(device_);
    tex->width_ = width;
    tex->height_ = height;
    tex->format_ = vk_fmt;

    VkImageCreateInfo image_ci{};
    image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = vk_fmt;
    image_ci.extent = {width, height, 1};
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = disp.vkCreateImage(dev, &image_ci, nullptr, &tex->image_);
    if (res != VK_SUCCESS || !tex->image_) {
        disp.vkDestroyBuffer(dev, staging_buf, nullptr);
        disp.vkFreeMemory(dev, staging_mem, nullptr);
        return MediaError::HardwareError;
    }

    VkMemoryRequirements img_mem_reqs{};
    disp.vkGetImageMemoryRequirements(dev, tex->image_, &img_mem_reqs);

    VkMemoryAllocateInfo img_alloc_info{};
    img_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    img_alloc_info.allocationSize = img_mem_reqs.size;
    img_alloc_info.memoryTypeIndex = device_.find_memory_type(
        img_mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    res = disp.vkAllocateMemory(dev, &img_alloc_info, nullptr, &tex->memory_);
    if (res != VK_SUCCESS || !tex->memory_) {
        disp.vkDestroyBuffer(dev, staging_buf, nullptr);
        disp.vkFreeMemory(dev, staging_mem, nullptr);
        return MediaError::AllocationFailed;
    }

    disp.vkBindImageMemory(dev, tex->image_, tex->memory_, 0);

    // 4. Transition image to TRANSFER_DST_OPTIMAL
    device_.transition_image_layout(
        tex->image_, vk_fmt,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    // 5. Copy buffer to image
    VkCommandBuffer cmd = device_.begin_single_time_commands();
    if (cmd) {
        if (px_fmt == PixelFormat::NV12) {
            VkBufferImageCopy copy_regions[2]{};
            copy_regions[0].bufferOffset = 0;
            copy_regions[0].bufferRowLength = width;
            copy_regions[0].bufferImageHeight = height;
            copy_regions[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
            copy_regions[0].imageSubresource.mipLevel = 0;
            copy_regions[0].imageSubresource.baseArrayLayer = 0;
            copy_regions[0].imageSubresource.layerCount = 1;
            copy_regions[0].imageOffset = {0, 0, 0};
            copy_regions[0].imageExtent = {width, height, 1};

            copy_regions[1].bufferOffset = static_cast<VkDeviceSize>(width) * height;
            copy_regions[1].bufferRowLength = width / 2;
            copy_regions[1].bufferImageHeight = height / 2;
            copy_regions[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
            copy_regions[1].imageSubresource.mipLevel = 0;
            copy_regions[1].imageSubresource.baseArrayLayer = 0;
            copy_regions[1].imageSubresource.layerCount = 1;
            copy_regions[1].imageOffset = {0, 0, 0};
            copy_regions[1].imageExtent = {width / 2, height / 2, 1};

            disp.vkCmdCopyBufferToImage(
                cmd,
                staging_buf,
                tex->image_,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                2, copy_regions
            );
        } else {
            VkBufferImageCopy copy_region{};
            copy_region.bufferOffset = 0;
            copy_region.bufferRowLength = 0;
            copy_region.bufferImageHeight = 0;
            copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.mipLevel = 0;
            copy_region.imageSubresource.baseArrayLayer = 0;
            copy_region.imageSubresource.layerCount = 1;
            copy_region.imageOffset = {0, 0, 0};
            copy_region.imageExtent = {width, height, 1};

            disp.vkCmdCopyBufferToImage(
                cmd,
                staging_buf,
                tex->image_,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &copy_region
            );
        }
        device_.end_single_time_commands(cmd);
    }

    // Free staging resources
    disp.vkDestroyBuffer(dev, staging_buf, nullptr);
    disp.vkFreeMemory(dev, staging_mem, nullptr);

    // 6. Transition image to SHADER_READ_ONLY_OPTIMAL
    device_.transition_image_layout(
        tex->image_, vk_fmt,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    // 7. YCbCr conversion if NV12
    void* sampler_pnext = nullptr;
    void* view_pnext = nullptr;
    VkSamplerYcbcrConversionInfo conv_info{};

    if (px_fmt == PixelFormat::NV12 && disp.vkCreateSamplerYcbcrConversion) {
        VkSamplerYcbcrConversionCreateInfo conv_ci{};
        conv_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
        conv_ci.format = vk_fmt;
        conv_ci.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
        conv_ci.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
        conv_ci.xChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
        conv_ci.yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
        conv_ci.chromaFilter = VK_FILTER_LINEAR;
        conv_ci.forceExplicitReconstruction = VK_FALSE;

        if (disp.vkCreateSamplerYcbcrConversion(dev, &conv_ci, nullptr, &tex->ycbcr_conversion_) == VK_SUCCESS) {
            conv_info.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
            conv_info.conversion = tex->ycbcr_conversion_;
            sampler_pnext = &conv_info;
            view_pnext = &conv_info;
        }
    }

    // 8. Create Sampler
    VkSamplerCreateInfo sampler_ci{};
    sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.pNext = sampler_pnext;
    sampler_ci.magFilter = VK_FILTER_LINEAR;
    sampler_ci.minFilter = VK_FILTER_LINEAR;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    disp.vkCreateSampler(dev, &sampler_ci, nullptr, &tex->sampler_);

    // 9. Create ImageView
    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.pNext = view_pnext;
    view_ci.image = tex->image_;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = vk_fmt;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;
    disp.vkCreateImageView(dev, &view_ci, nullptr, &tex->image_view_);

    return tex;
}

} // namespace atum::vk

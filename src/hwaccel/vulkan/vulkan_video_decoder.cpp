#include "atum/hwaccel/vulkan/vulkan_video_decoder.hpp"

#include <cstring>
#include <algorithm>

namespace atum::vk {

VulkanVideoDecoder::VulkanVideoDecoder(const VulkanDevice& device)
    : device_(&device), session_(device) {}

VulkanVideoDecoder::~VulkanVideoDecoder() {
    reset();
}

VulkanVideoDecoder::VulkanVideoDecoder(VulkanVideoDecoder&& other) noexcept
    : session_(*other.device_) {
    *this = std::move(other);
}

VulkanVideoDecoder& VulkanVideoDecoder::operator=(VulkanVideoDecoder&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = other.device_;
        codec_ = other.codec_;
        width_ = other.width_;
        height_ = other.height_;
        num_dpb_slots_ = other.num_dpb_slots_;
        is_initialized_ = other.is_initialized_;
        session_ = std::move(other.session_);
        dpb_slots_ = std::move(other.dpb_slots_);
        bitstream_buffer_ = other.bitstream_buffer_;
        bitstream_memory_ = other.bitstream_memory_;
        bitstream_buffer_size_ = other.bitstream_buffer_size_;
        decode_cmd_ = other.decode_cmd_;
        decode_fence_ = other.decode_fence_;
        current_slot_index_ = other.current_slot_index_;
        ready_frames_ = std::move(other.ready_frames_);

        other.bitstream_buffer_ = VK_NULL_HANDLE;
        other.bitstream_memory_ = VK_NULL_HANDLE;
        other.decode_cmd_ = VK_NULL_HANDLE;
        other.decode_fence_ = VK_NULL_HANDLE;
        other.is_initialized_ = false;
    }
    return *this;
}

void VulkanVideoDecoder::reset() noexcept {
    if (!device_ || !device_->device()) return;

    VkDevice dev = device_->device();
    const auto& disp = device_->dispatcher();

    if (device_->video_decode_queue()) {
        disp.vkQueueWaitIdle(device_->video_decode_queue());
    }

    if (decode_fence_ != VK_NULL_HANDLE) {
        disp.vkDestroyFence(dev, decode_fence_, nullptr);
        decode_fence_ = VK_NULL_HANDLE;
    }

    if (decode_cmd_ != VK_NULL_HANDLE && device_->video_command_pool()) {
        disp.vkFreeCommandBuffers(dev, device_->video_command_pool(), 1, &decode_cmd_);
        decode_cmd_ = VK_NULL_HANDLE;
    }

    if (bitstream_buffer_ != VK_NULL_HANDLE) {
        disp.vkDestroyBuffer(dev, bitstream_buffer_, nullptr);
        bitstream_buffer_ = VK_NULL_HANDLE;
    }

    if (bitstream_memory_ != VK_NULL_HANDLE) {
        disp.vkFreeMemory(dev, bitstream_memory_, nullptr);
        bitstream_memory_ = VK_NULL_HANDLE;
    }

    free_dpb_slots();
    session_.reset();

    ready_frames_.clear();
    is_initialized_ = false;
}

void VulkanVideoDecoder::free_dpb_slots() noexcept {
    if (!device_ || !device_->device()) return;

    VkDevice dev = device_->device();
    const auto& disp = device_->dispatcher();

    for (auto& slot : dpb_slots_) {
        if (slot.image_view != VK_NULL_HANDLE) {
            disp.vkDestroyImageView(dev, slot.image_view, nullptr);
            slot.image_view = VK_NULL_HANDLE;
        }
        if (slot.image != VK_NULL_HANDLE) {
            disp.vkDestroyImage(dev, slot.image, nullptr);
            slot.image = VK_NULL_HANDLE;
        }
        if (slot.memory != VK_NULL_HANDLE) {
            disp.vkFreeMemory(dev, slot.memory, nullptr);
            slot.memory = VK_NULL_HANDLE;
        }
    }
    dpb_slots_.clear();
}

MediaResult<void> VulkanVideoDecoder::allocate_dpb_slots() {
    free_dpb_slots();

    VkDevice dev = device_->device();
    const auto& disp = device_->dispatcher();

    dpb_slots_.resize(num_dpb_slots_);
    for (uint32_t i = 0; i < num_dpb_slots_; ++i) {
        auto& slot = dpb_slots_[i];
        slot.slot_index = static_cast<int32_t>(i);

        VkImageCreateInfo img_ci{};
        img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_ci.imageType = VK_IMAGE_TYPE_2D;
        img_ci.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        img_ci.extent = {width_, height_, 1};
        img_ci.mipLevels = 1;
        img_ci.arrayLayers = 1;
        img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_ci.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
                       VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
                       VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkResult res = disp.vkCreateImage(dev, &img_ci, nullptr, &slot.image);
        if (res != VK_SUCCESS || !slot.image) {
            return MediaError::HardwareError;
        }

        VkMemoryRequirements mem_reqs{};
        disp.vkGetImageMemoryRequirements(dev, slot.image, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = device_->find_memory_type(
            mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        res = disp.vkAllocateMemory(dev, &alloc_info, nullptr, &slot.memory);
        if (res != VK_SUCCESS || !slot.memory) {
            return MediaError::AllocationFailed;
        }

        disp.vkBindImageMemory(dev, slot.image, slot.memory, 0);

        VkImageViewCreateInfo view_ci{};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = slot.image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;

        res = disp.vkCreateImageView(dev, &view_ci, nullptr, &slot.image_view);
        if (res != VK_SUCCESS) {
            return MediaError::HardwareError;
        }
    }

    return MediaResult<void>::ok();
}

MediaResult<void> VulkanVideoDecoder::init(CodecId codec, uint32_t width, uint32_t height, uint32_t dpb_slots) {
    reset();

    if (!device_ || !device_->device()) {
        return MediaError::InvalidState;
    }

    codec_ = codec;
    width_ = width;
    height_ = height;
    num_dpb_slots_ = std::max(dpb_slots, 4u);

    // 1. Initialize Video Session
    auto sess_res = session_.init(codec, width, height, num_dpb_slots_);
    if (!sess_res.is_ok()) {
        return sess_res;
    }

    // 2. Allocate DPB Slots
    auto dpb_res = allocate_dpb_slots();
    if (!dpb_res.is_ok()) {
        reset();
        return dpb_res;
    }

    VkDevice dev = device_->device();
    const auto& disp = device_->dispatcher();

    // 3. Allocate Bitstream Buffer
    VkBufferCreateInfo buf_ci{};
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size = bitstream_buffer_size_;
    buf_ci.usage = VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = disp.vkCreateBuffer(dev, &buf_ci, nullptr, &bitstream_buffer_);
    if (res != VK_SUCCESS || !bitstream_buffer_) {
        reset();
        return MediaError::HardwareError;
    }

    VkMemoryRequirements mem_reqs{};
    disp.vkGetBufferMemoryRequirements(dev, bitstream_buffer_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device_->find_memory_type(
        mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    res = disp.vkAllocateMemory(dev, &alloc_info, nullptr, &bitstream_memory_);
    if (res != VK_SUCCESS || !bitstream_memory_) {
        reset();
        return MediaError::AllocationFailed;
    }

    disp.vkBindBufferMemory(dev, bitstream_buffer_, bitstream_memory_, 0);

    // 4. Allocate Decode Command Buffer
    VkCommandBufferAllocateInfo cmd_ai{};
    cmd_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_ai.commandPool = device_->video_command_pool();
    cmd_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_ai.commandBufferCount = 1;

    res = disp.vkAllocateCommandBuffers(dev, &cmd_ai, &decode_cmd_);
    if (res != VK_SUCCESS || !decode_cmd_) {
        reset();
        return MediaError::HardwareError;
    }

    // 5. Create Fence
    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    res = disp.vkCreateFence(dev, &fence_ci, nullptr, &decode_fence_);
    if (res != VK_SUCCESS) {
        reset();
        return MediaError::HardwareError;
    }

    is_initialized_ = true;
    return MediaResult<void>::ok();
}

MediaResult<void> VulkanVideoDecoder::send_packet(const Packet& packet) {
    if (!is_initialized_) {
        return MediaError::InvalidState;
    }

    if (packet.empty()) {
        return MediaResult<void>::ok();
    }

    if (packet.size() > bitstream_buffer_size_) {
        return MediaError::AllocationFailed;
    }

    VkDevice dev = device_->device();
    const auto& disp = device_->dispatcher();

    // 1. Upload bitstream data into hardware buffer
    void* mapped = nullptr;
    VkResult res = disp.vkMapMemory(dev, bitstream_memory_, 0, packet.size(), 0, &mapped);
    if (res != VK_SUCCESS || !mapped) {
        return MediaError::HardwareError;
    }
    std::memcpy(mapped, packet.data(), packet.size());
    disp.vkUnmapMemory(dev, bitstream_memory_);

    // 2. Select target DPB slot
    auto& target_slot = dpb_slots_[current_slot_index_];
    target_slot.pts = packet.pts();
    target_slot.in_use = true;

    // 3. Record video decode commands
    disp.vkResetCommandBuffer(decode_cmd_, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    disp.vkBeginCommandBuffer(decode_cmd_, &begin_info);

    VkVideoBeginCodingInfoKHR coding_begin_info{};
    coding_begin_info.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
    coding_begin_info.videoSession = session_.session();
    coding_begin_info.videoSessionParameters = session_.parameters();

    disp.vkCmdBeginVideoCodingKHR(decode_cmd_, &coding_begin_info);

    VkVideoPictureResourceInfoKHR dst_pic_resource{};
    dst_pic_resource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
    dst_pic_resource.codedOffset = {0, 0};
    dst_pic_resource.codedExtent = {width_, height_};
    dst_pic_resource.baseArrayLayer = 0;
    dst_pic_resource.imageViewBinding = target_slot.image_view;

    VkVideoReferenceSlotInfoKHR setup_reference_slot{};
    setup_reference_slot.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
    setup_reference_slot.slotIndex = target_slot.slot_index;
    setup_reference_slot.pPictureResource = &dst_pic_resource;

    VkVideoDecodeInfoKHR decode_info{};
    decode_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR;
    decode_info.srcBuffer = bitstream_buffer_;
    decode_info.srcBufferOffset = 0;
    decode_info.srcBufferRange = packet.size();
    decode_info.dstPictureResource = dst_pic_resource;
    decode_info.pSetupReferenceSlot = &setup_reference_slot;

    disp.vkCmdDecodeVideoKHR(decode_cmd_, &decode_info);

    VkVideoEndCodingInfoKHR coding_end_info{};
    coding_end_info.sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR;
    disp.vkCmdEndVideoCodingKHR(decode_cmd_, &coding_end_info);

    disp.vkEndCommandBuffer(decode_cmd_);

    // 4. Submit to Video Decode Queue
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &decode_cmd_;

    disp.vkResetFences(dev, 1, &decode_fence_);
    disp.vkQueueSubmit(device_->video_decode_queue(), 1, &submit_info, decode_fence_);
    disp.vkWaitForFences(dev, 1, &decode_fence_, VK_TRUE, 1000000000ULL); // 1 second timeout

    // 5. Emit VideoFrame
    VideoFrame frame = VideoFrame::allocate(width_, height_, PixelFormat::NV12);
    frame.set_pts(packet.pts());
    frame.set_hw_opaque_handle(reinterpret_cast<uint64_t>(target_slot.image));
    ready_frames_.push_back(std::move(frame));

    current_slot_index_ = (current_slot_index_ + 1) % num_dpb_slots_;
    return MediaResult<void>::ok();
}

MediaResult<VideoFrame> VulkanVideoDecoder::receive_frame() {
    if (ready_frames_.empty()) {
        return MediaError::NeedMoreData;
    }

    VideoFrame frame = std::move(ready_frames_.front());
    ready_frames_.erase(ready_frames_.begin());
    return frame;
}

} // namespace atum::vk

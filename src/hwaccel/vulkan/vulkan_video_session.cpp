#include "atum/hwaccel/vulkan/vulkan_video_session.hpp"

#include <algorithm>

namespace atum::vk {

VulkanVideoSession::VulkanVideoSession(const VulkanDevice& device)
    : device_(&device) {}

VulkanVideoSession::~VulkanVideoSession() {
    reset();
}

VulkanVideoSession::VulkanVideoSession(VulkanVideoSession&& other) noexcept {
    *this = std::move(other);
}

VulkanVideoSession& VulkanVideoSession::operator=(VulkanVideoSession&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = other.device_;
        codec_ = other.codec_;
        width_ = other.width_;
        height_ = other.height_;
        max_dpb_slots_ = other.max_dpb_slots_;
        picture_format_ = other.picture_format_;
        session_ = other.session_;
        parameters_ = other.parameters_;
        session_memories_ = std::move(other.session_memories_);

        other.session_ = VK_NULL_HANDLE;
        other.parameters_ = VK_NULL_HANDLE;
    }
    return *this;
}

void VulkanVideoSession::reset() noexcept {
    if (!device_ || !device_->device()) return;

    VkDevice dev = device_->device();
    const auto& disp = device_->dispatcher();

    if (parameters_ != VK_NULL_HANDLE && disp.vkDestroyVideoSessionParametersKHR) {
        disp.vkDestroyVideoSessionParametersKHR(dev, parameters_, nullptr);
        parameters_ = VK_NULL_HANDLE;
    }

    if (session_ != VK_NULL_HANDLE && disp.vkDestroyVideoSessionKHR) {
        disp.vkDestroyVideoSessionKHR(dev, session_, nullptr);
        session_ = VK_NULL_HANDLE;
    }

    for (VkDeviceMemory mem : session_memories_) {
        if (mem != VK_NULL_HANDLE) {
            disp.vkFreeMemory(dev, mem, nullptr);
        }
    }
    session_memories_.clear();
}

MediaResult<void> VulkanVideoSession::init(CodecId codec, uint32_t width, uint32_t height, uint32_t max_dpb_slots) {
    reset();

    if (!device_ || !device_->device()) {
        return MediaError::InvalidState;
    }

    const auto& disp = device_->dispatcher();
    if (!disp.vkCreateVideoSessionKHR || device_->video_decode_queue_family() < 0) {
        return MediaError::NotImplemented;
    }

    codec_ = codec;
    width_ = width;
    height_ = height;
    max_dpb_slots_ = max_dpb_slots;

    VkVideoDecodeH264ProfileInfoKHR h264_profile{};
    VkVideoDecodeH265ProfileInfoKHR h265_profile{};

    VkVideoProfileInfoKHR profile_info{};
    profile_info.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    profile_info.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    profile_info.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    profile_info.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

    if (codec == CodecId::H264) {
        profile_info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
        h264_profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
        h264_profile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_MAIN;
        h264_profile.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;
        profile_info.pNext = &h264_profile;
    } else if (codec == CodecId::H265 || codec == CodecId::HEVC) {
        profile_info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
        h265_profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR;
        h265_profile.stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN;
        profile_info.pNext = &h265_profile;
    } else {
        return MediaError::UnsupportedCodec;
    }

    // 1. Create Video Session
    VkVideoSessionCreateInfoKHR session_ci{};
    session_ci.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
    session_ci.queueFamilyIndex = static_cast<uint32_t>(device_->video_decode_queue_family());
    session_ci.pVideoProfile = &profile_info;
    session_ci.pictureFormat = picture_format_;
    session_ci.maxCodedExtent = {width, height};
    session_ci.referencePictureFormat = picture_format_;
    session_ci.maxDpbSlots = max_dpb_slots_;
    session_ci.maxActiveReferencePictures = std::min(max_dpb_slots_, 8u);

    VkResult res = disp.vkCreateVideoSessionKHR(device_->device(), &session_ci, nullptr, &session_);
    if (res != VK_SUCCESS || !session_) {
        return MediaError::HardwareError;
    }

    // 2. Query and Bind Session Memory Requirements
    uint32_t mem_req_count = 0;
    disp.vkGetVideoSessionMemoryRequirementsKHR(device_->device(), session_, &mem_req_count, nullptr);
    if (mem_req_count > 0) {
        std::vector<VkVideoSessionMemoryRequirementsKHR> mem_reqs(mem_req_count);
        for (auto& req : mem_reqs) {
            req.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
        }
        disp.vkGetVideoSessionMemoryRequirementsKHR(device_->device(), session_, &mem_req_count, mem_reqs.data());

        std::vector<VkBindVideoSessionMemoryInfoKHR> bind_infos(mem_req_count);
        session_memories_.resize(mem_req_count, VK_NULL_HANDLE);

        for (uint32_t i = 0; i < mem_req_count; ++i) {
            const auto& req = mem_reqs[i];
            uint32_t mem_type = device_->find_memory_type(
                req.memoryRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = req.memoryRequirements.size;
            alloc_info.memoryTypeIndex = mem_type;

            res = disp.vkAllocateMemory(device_->device(), &alloc_info, nullptr, &session_memories_[i]);
            if (res != VK_SUCCESS) {
                reset();
                return MediaError::AllocationFailed;
            }

            bind_infos[i].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
            bind_infos[i].memoryBindIndex = req.memoryBindIndex;
            bind_infos[i].memory = session_memories_[i];
            bind_infos[i].memoryOffset = 0;
            bind_infos[i].memorySize = req.memoryRequirements.size;
        }

        res = disp.vkBindVideoSessionMemoryKHR(
            device_->device(), session_,
            static_cast<uint32_t>(bind_infos.size()), bind_infos.data()
        );
        if (res != VK_SUCCESS) {
            reset();
            return MediaError::HardwareError;
        }
    }

    return MediaResult<void>::ok();
}

} // namespace atum::vk

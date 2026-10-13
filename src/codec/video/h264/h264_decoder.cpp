#include "atum/codec/video/h264/h264_decoder.hpp"
#include <algorithm>
#include <cstring>
#include "atum/hwaccel/vaapi_device.hpp"

namespace atum {

H264Decoder::H264Decoder() {
    params_.codec_id = CodecId::H264;
    params_.media_type = MediaType::Video;
}

H264Decoder::~H264Decoder() {
    release_hardware_pipeline();
}

void H264Decoder::init_hardware_pipeline(uint32_t width, uint32_t height) {
    if (hw_pipeline_initialized_ || !is_hw_supported_ || !hw_device_) return;
    if (hw_device_->type() == HwDeviceType::Vaapi) {
        auto* va = static_cast<VaapiDevice*>(hw_device_.get());
        auto cfg_res = va->create_config(CodecId::H264);
        if (cfg_res.is_ok()) {
            hw_config_id_ = *cfg_res;
            auto surf_res = va->create_surfaces(width, height, PixelFormat::NV12, 16);
            if (surf_res.is_ok()) {
                hw_surfaces_ = std::move(*surf_res);
                auto ctx_res = va->create_context(hw_config_id_, width, height, hw_surfaces_);
                if (ctx_res.is_ok()) {
                    hw_context_id_ = *ctx_res;
                    hw_pipeline_initialized_ = true;
                }
            }
        }
    }
}

void H264Decoder::release_hardware_pipeline() {
    if (!hw_pipeline_initialized_ || !hw_device_) return;
    if (hw_device_->type() == HwDeviceType::Vaapi) {
        auto* va = static_cast<VaapiDevice*>(hw_device_.get());
        if (hw_context_id_ != 0) {
            va->destroy_context(hw_context_id_);
            hw_context_id_ = 0;
        }
        if (!hw_surfaces_.empty()) {
            va->destroy_surfaces(hw_surfaces_);
            hw_surfaces_.clear();
        }
        if (hw_config_id_ != 0) {
            va->destroy_config(hw_config_id_);
            hw_config_id_ = 0;
        }
    }
    hw_pipeline_initialized_ = false;
}

MediaResult<void> H264Decoder::init(const CodecParameters& params) {
    release_hardware_pipeline();
    params_ = params;
    params_.codec_id = CodecId::H264;
    params_.media_type = MediaType::Video;
    frame_count_ = 0;
    current_surface_idx_ = 0;
    dpb_.clear();

    if (!params.extradata.empty()) {
        if (params.extradata[0] == 1 && params.extradata.size() >= 7) {
            is_avcc_ = true;
            nal_length_size_ = (params.extradata[4] & 0x03) + 1;
        }
        auto res = parser_.parse_extradata(params.extradata);
        if (res) {
            if (parser_.width() > 0) params_.width = parser_.width();
            if (parser_.height() > 0) params_.height = parser_.height();
        }
    }

    hw_device_ = HwDevice::create_default();
    if (hw_device_ && hw_device_->type() == HwDeviceType::Vaapi) {
        auto* va = static_cast<VaapiDevice*>(hw_device_.get());
        is_hw_supported_ = va->supports_codec(CodecId::H264);
    }

    if (params_.width > 0 && params_.height > 0) {
        init_hardware_pipeline(params_.width, params_.height);
    }

    return {};
}

MediaResult<void> H264Decoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    auto data = packet.as_span();

    if (is_avcc_ && nal_length_size_ > 0) {
        size_t offset = 0;
        while (offset + nal_length_size_ <= data.size()) {
            uint32_t nal_len = 0;
            for (size_t i = 0; i < nal_length_size_; ++i) {
                nal_len = (nal_len << 8) | data[offset + i];
            }
            offset += nal_length_size_;
            if (offset + nal_len > data.size()) break;

            auto nal_span = data.subspan(offset, nal_len);
            if (!nal_span.empty()) {
                NalUnit nal{};
                nal.forbidden_zero_bit = (nal_span[0] >> 7) & 1;
                nal.ref_idc = (nal_span[0] >> 5) & 3;
                nal.type = nal_span[0] & 0x1F;
                nal.payload = nal_span.subspan(1);
                (void)parser_.parse_nal(nal);
            }
            offset += nal_len;
        }
    } else {
        auto nals = resilient_parser_.parse_and_recover(data);
        for (const auto& nal : nals) {
            (void)parser_.parse_nal(nal);
        }
    }

    uint32_t w = parser_.width();
    uint32_t h = parser_.height();
    if (w == 0) w = params_.width;
    if (h == 0) h = params_.height;

    if (w > 0 && h > 0) {
        params_.width = w;
        params_.height = h;

        if (!hw_pipeline_initialized_) {
            init_hardware_pipeline(w, h);
        }

        VideoFrame frame;
        uint32_t target_surface_id = 0;

        if (hw_pipeline_initialized_ && !hw_surfaces_.empty() && hw_device_->type() == HwDeviceType::Vaapi) {
            auto* va = static_cast<VaapiDevice*>(hw_device_.get());
            target_surface_id = hw_surfaces_[current_surface_idx_];
            current_surface_idx_ = (current_surface_idx_ + 1) % hw_surfaces_.size();

            // Submit hardware decode task
            va->begin_picture(hw_context_id_, target_surface_id);
            uint32_t slice_buf_id = 0;
            if (va->create_buffer(hw_context_id_, 1 /* VASliceDataBufferType */, static_cast<uint32_t>(packet.size()), 1, packet.data()).is_ok()) {
                va->render_picture(hw_context_id_, &slice_buf_id, 1);
                va->destroy_buffer(slice_buf_id);
            }
            va->end_picture(hw_context_id_);
            va->sync_surface(target_surface_id);

            // Export or derive hardware frame
            auto derived = va->derive_surface_to_frame(target_surface_id, w, h);
            if (derived.is_ok()) {
                frame = std::move(*derived);
            } else {
                frame = VideoFrame::allocate(w, h, PixelFormat::NV12);
                frame.set_hw_frame(true);
                frame.set_hw_surface_id(target_surface_id);
                HwBufferDescriptor desc{};
                if (va->export_surface_dma_buf(target_surface_id, desc).is_ok()) {
                    frame.set_dma_buf_fd(desc.fds[0]);
                }
            }
        } else {
            // Hardware-backed direct frame
            frame = VideoFrame::allocate(w, h, PixelFormat::YUV420P);
            frame.set_hw_frame(true);
            HwBufferDescriptor desc{};
            if (hw_device_ && hw_device_->export_buffer(frame, desc).is_ok()) {
                frame.set_dma_buf_fd(desc.fds[0]);
            }
        }

        frame.set_pts(packet.pts());
        frame.set_dts(packet.dts());
        frame.set_duration(packet.duration());

        bool is_key = false;
        if (parser_.last_slice_header().has_value() && parser_.last_slice_header()->is_keyframe()) {
            is_key = true;
        } else if (packet.is_keyframe()) {
            is_key = true;
        }
        frame.set_keyframe(is_key);

        auto* sps = parser_.active_sps();
        if (sps && sps->vui_parameters_present_flag) {
            frame.set_color_range(sps->vui.video_full_range_flag ? ColorRange::Full : ColorRange::Studio);
            if (sps->vui.colour_description_present_flag) {
                if (sps->vui.matrix_coefficients == 1) {
                    frame.set_color_space(ColorSpace::BT709);
                } else if (sps->vui.matrix_coefficients == 6 || sps->vui.matrix_coefficients == 5) {
                    frame.set_color_space(ColorSpace::BT601);
                } else if (sps->vui.matrix_coefficients == 9) {
                    frame.set_color_space(ColorSpace::BT2020);
                }
            }
        }

        int32_t poc = parser_.last_slice_header().has_value() ?
                      static_cast<int32_t>(parser_.last_slice_header()->pic_order_cnt_lsb) : static_cast<int32_t>(frame_count_);
        uint32_t frame_num = parser_.last_slice_header().has_value() ?
                             parser_.last_slice_header()->frame_num : frame_count_;

        if (error_concealment_enabled_ && resilient_parser_.stats().corrupted_bytes_skipped > 0) {
            damage_mask_ = ErrorConcealmentMask(w, h, 16);
            damage_mask_.mark_damaged(0, 0);
            (void)error_concealer_.conceal(frame, damage_mask_, &dpb_);
        }

        if (is_key) {
            dpb_.clear();
        }
        dpb_.insert(frame.clone(), poc, frame_num, is_key);

        ready_frames_.push(std::move(frame));
        frame_count_++;
    }

    return {};
}

MediaResult<void> H264Decoder::receive_frame(VideoFrame& frame) {
    if (ready_frames_.empty()) return MediaError::NeedMoreData;
    frame = std::move(ready_frames_.front());
    ready_frames_.pop();
    return {};
}

MediaResult<void> H264Decoder::flush() {
    return {};
}

MediaResult<void> H264Decoder::reset() {
    while (!ready_frames_.empty()) ready_frames_.pop();
    dpb_.clear();
    frame_count_ = 0;
    current_surface_idx_ = 0;
    return {};
}

} // namespace atum

#include "atum/codec/video/hevc/hevc_decoder.hpp"
#include <algorithm>
#include <cstring>
#include "atum/hwaccel/vaapi_device.hpp"

namespace atum {

HevcDecoder::HevcDecoder() {
    params_.codec_id = CodecId::H265;
    params_.media_type = MediaType::Video;
}

HevcDecoder::~HevcDecoder() {
    release_hardware_pipeline();
}

void HevcDecoder::init_hardware_pipeline(uint32_t width, uint32_t height) {
    if (hw_pipeline_initialized_ || !is_hw_supported_ || !hw_device_) return;
    if (hw_device_->type() == HwDeviceType::Vaapi) {
        auto* va = static_cast<VaapiDevice*>(hw_device_.get());
        auto cfg_res = va->create_config(CodecId::H265);
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

void HevcDecoder::release_hardware_pipeline() {
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

MediaResult<void> HevcDecoder::init(const CodecParameters& params) {
    release_hardware_pipeline();
    params_ = params;
    params_.codec_id = CodecId::H265;
    params_.media_type = MediaType::Video;
    frame_count_ = 0;
    current_surface_idx_ = 0;
    dpb_.clear();

    if (!params.extradata.empty()) {
        if (params.extradata[0] == 1 && params.extradata.size() >= 23) {
            is_hvcc_ = true;
            nal_length_size_ = (params.extradata[21] & 0x03) + 1;
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
        is_hw_supported_ = va->supports_codec(CodecId::H265);
    }

    if (params_.width > 0 && params_.height > 0) {
        init_hardware_pipeline(params_.width, params_.height);
    }

    return {};
}

MediaResult<void> HevcDecoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    auto data = packet.as_span();

    if (is_hvcc_ && nal_length_size_ > 0) {
        size_t offset = 0;
        while (offset + nal_length_size_ <= data.size()) {
            uint32_t nal_len = 0;
            for (size_t i = 0; i < nal_length_size_; ++i) {
                nal_len = (nal_len << 8) | data[offset + i];
            }
            offset += nal_length_size_;
            if (offset + nal_len > data.size()) break;

            auto nal_span = data.subspan(offset, nal_len);
            (void)parser_.parse_nal(nal_span);
            offset += nal_len;
        }
    } else {
        (void)parser_.parse_annex_b(data);
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

            va->begin_picture(hw_context_id_, target_surface_id);
            uint32_t slice_buf_id = 0;
            if (va->create_buffer(hw_context_id_, 1 /* VASliceDataBufferType */, static_cast<uint32_t>(packet.size()), 1, packet.data()).is_ok()) {
                va->render_picture(hw_context_id_, &slice_buf_id, 1);
                va->destroy_buffer(slice_buf_id);
            }
            va->end_picture(hw_context_id_);
            va->sync_surface(target_surface_id);

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
                if (sps->vui.matrix_coeffs == 1) {
                    frame.set_color_space(ColorSpace::BT709);
                } else if (sps->vui.matrix_coeffs == 6 || sps->vui.matrix_coeffs == 5) {
                    frame.set_color_space(ColorSpace::BT601);
                } else if (sps->vui.matrix_coeffs == 9) {
                    frame.set_color_space(ColorSpace::BT2020);
                }
            }
        }

        int32_t poc = parser_.last_slice_header().has_value() ?
                      static_cast<int32_t>(parser_.last_slice_header()->slice_pic_order_cnt_lsb) : static_cast<int32_t>(frame_count_);

        if (is_key) {
            dpb_.clear();
        }
        dpb_.insert(frame.clone(), poc, frame_count_, is_key);

        ready_frames_.push(std::move(frame));
        frame_count_++;
    }

    return {};
}

MediaResult<void> HevcDecoder::receive_frame(VideoFrame& frame) {
    if (ready_frames_.empty()) return MediaError::NeedMoreData;
    frame = std::move(ready_frames_.front());
    ready_frames_.pop();
    return {};
}

MediaResult<void> HevcDecoder::flush() {
    return {};
}

MediaResult<void> HevcDecoder::reset() {
    while (!ready_frames_.empty()) ready_frames_.pop();
    dpb_.clear();
    frame_count_ = 0;
    current_surface_idx_ = 0;
    return {};
}

} // namespace atum

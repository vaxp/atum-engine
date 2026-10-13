#include "atum/codec/video/av1/av1_decoder.hpp"
#include <algorithm>
#include <cstring>
#include "atum/hwaccel/vaapi_device.hpp"
#include "atum/hwaccel/vaapi_param_builder.hpp"

namespace atum {

Av1Decoder::Av1Decoder() {
    params_.codec_id = CodecId::AV1;
    params_.media_type = MediaType::Video;
}

Av1Decoder::~Av1Decoder() {
    release_hardware_pipeline();
}

void Av1Decoder::init_hardware_pipeline(uint32_t width, uint32_t height) {
    if (hw_pipeline_initialized_ || !is_hw_supported_ || !hw_device_) return;
    if (hw_device_->type() == HwDeviceType::Vaapi) {
        auto* va = static_cast<VaapiDevice*>(hw_device_.get());
        auto cfg_res = va->create_config(CodecId::AV1);
        if (cfg_res.is_ok()) {
            hw_config_id_ = *cfg_res;
            auto surf_res = va->create_surfaces(width, height, PixelFormat::NV12, 8);
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

void Av1Decoder::release_hardware_pipeline() {
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

MediaResult<void> Av1Decoder::init(const CodecParameters& params) {
    release_hardware_pipeline();
    params_ = params;
    params_.codec_id = CodecId::AV1;
    params_.media_type = MediaType::Video;
    frame_count_ = 0;
    current_surface_idx_ = 0;
    dpb_.clear();

    if (!params.extradata.empty()) {
        (void)parser_.parse_extradata(params.extradata);
        if (parser_.width() > 0) params_.width = parser_.width();
        if (parser_.height() > 0) params_.height = parser_.height();
    }

    hw_device_ = HwDevice::create_default();
    if (hw_device_ && hw_device_->type() == HwDeviceType::Vaapi) {
        auto* va = static_cast<VaapiDevice*>(hw_device_.get());
        is_hw_supported_ = va->supports_codec(CodecId::AV1);
    }

    if (params_.width > 0 && params_.height > 0) {
        init_hardware_pipeline(params_.width, params_.height);
    }

    return {};
}

MediaResult<void> Av1Decoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    (void)parser_.parse_stream(packet.as_span());

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

            std::vector<uint32_t> buf_ids;

#if defined(ATUM_HAS_VAAPI_HEADERS)
            const auto& seq = parser_.sequence_header();
            if (seq.has_value()) {
                const auto* frame_hdr = parser_.last_frame_header().has_value() ? &parser_.last_frame_header().value() : nullptr;
                auto params = VaapiAv1DecodeParams::build(*seq, frame_hdr, target_surface_id);
                auto pic_buf = va->create_buffer(hw_context_id_, 0 /* VAPictureParameterBufferType */, sizeof(params.pic_param), 1, &params.pic_param);
                if (pic_buf.is_ok()) buf_ids.push_back(*pic_buf);
            }
#endif
            auto data_buf = va->create_buffer(hw_context_id_, 5 /* VASliceDataBufferType */, static_cast<uint32_t>(packet.size()), 1, packet.data());
            if (data_buf.is_ok()) buf_ids.push_back(*data_buf);

            // Hardware Command Submission
            va->begin_picture(hw_context_id_, target_surface_id);
            if (!buf_ids.empty()) {
                va->render_picture(hw_context_id_, buf_ids.data(), static_cast<uint32_t>(buf_ids.size()));
            }
            va->end_picture(hw_context_id_);
            va->sync_surface(target_surface_id);

            for (uint32_t b : buf_ids) {
                va->destroy_buffer(b);
            }

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
        if (parser_.last_frame_header().has_value()) {
            is_key = parser_.last_frame_header()->is_keyframe();
        } else if (packet.is_keyframe()) {
            is_key = true;
        }
        frame.set_keyframe(is_key);

        const auto& seq_opt = parser_.sequence_header();
        if (seq_opt.has_value() && seq_opt->color_config.color_description_present_flag) {
            if (seq_opt->color_config.matrix_coefficients == 1) {
                frame.set_color_space(ColorSpace::BT709);
            } else if (seq_opt->color_config.matrix_coefficients == 9) {
                frame.set_color_space(ColorSpace::BT2020);
            } else if (seq_opt->color_config.matrix_coefficients == 6 || seq_opt->color_config.matrix_coefficients == 5) {
                frame.set_color_space(ColorSpace::BT601);
            }
            frame.set_color_range(seq_opt->color_config.color_range_full ? ColorRange::Full : ColorRange::Studio);
        }

        if (is_key) {
            dpb_.clear();
        }
        dpb_.insert(frame.clone(), frame_count_, frame_count_, is_key);

        ready_frames_.push(std::move(frame));
        frame_count_++;
    }

    return {};
}

MediaResult<void> Av1Decoder::receive_frame(VideoFrame& frame) {
    if (ready_frames_.empty()) return MediaError::NeedMoreData;
    frame = std::move(ready_frames_.front());
    ready_frames_.pop();
    return {};
}

MediaResult<void> Av1Decoder::flush() {
    return {};
}

MediaResult<void> Av1Decoder::reset() {
    while (!ready_frames_.empty()) ready_frames_.pop();
    dpb_.clear();
    frame_count_ = 0;
    current_surface_idx_ = 0;
    return {};
}

} // namespace atum

#include "atum/codec/video/vp9/vp9_decoder.hpp"
#include <algorithm>
#include <cstring>
#include "atum/hwaccel/vaapi_device.hpp"
#include "atum/hwaccel/vaapi_param_builder.hpp"

namespace atum {

Vp9Decoder::Vp9Decoder() {
    params_.codec_id = CodecId::VP9;
    params_.media_type = MediaType::Video;
}

Vp9Decoder::~Vp9Decoder() {
    release_hardware_pipeline();
}

void Vp9Decoder::init_hardware_pipeline(uint32_t width, uint32_t height) {
    if (hw_pipeline_initialized_ || !is_hw_supported_ || !hw_device_) return;
    if (hw_device_->type() == HwDeviceType::Vaapi) {
        auto* va = static_cast<VaapiDevice*>(hw_device_.get());
        auto cfg_res = va->create_config(CodecId::VP9);
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

void Vp9Decoder::release_hardware_pipeline() {
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

MediaResult<void> Vp9Decoder::init(const CodecParameters& params) {
    release_hardware_pipeline();
    params_ = params;
    params_.codec_id = CodecId::VP9;
    params_.media_type = MediaType::Video;
    frame_count_ = 0;
    current_surface_idx_ = 0;
    ref_surfaces_.fill(0xFFFFFFFF);
    last_width_ = 0;
    last_height_ = 0;
    dpb_.clear();

    hw_device_ = HwDevice::create_default();
    if (hw_device_ && hw_device_->type() == HwDeviceType::Vaapi) {
        auto* va = static_cast<VaapiDevice*>(hw_device_.get());
        is_hw_supported_ = va->supports_codec(CodecId::VP9);
    }

    if (params_.width > 0 && params_.height > 0) {
        init_hardware_pipeline(params_.width, params_.height);
    }

    return {};
}

MediaResult<void> Vp9Decoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    auto subframes_res = parser_.parse_packet(packet.as_span());
    if (!subframes_res) return subframes_res.error();

    for (const auto& subframe : *subframes_res) {
        if (!subframe.header.has_value()) continue;

        const auto& hdr = *subframe.header;

        // If this frame just displays an existing reference frame, output it without decoding
        if (hdr.show_existing_frame) {
            uint8_t ref_idx = hdr.frame_to_show_map_idx;
            uint32_t surface_id = (ref_idx < 8) ? ref_surfaces_[ref_idx] : 0xFFFFFFFF;
            if (surface_id != 0xFFFFFFFF && hw_device_ && hw_device_->type() == HwDeviceType::Vaapi) {
                auto* va = static_cast<VaapiDevice*>(hw_device_.get());
                uint32_t w = (last_width_ > 0) ? last_width_ : params_.width;
                uint32_t h = (last_height_ > 0) ? last_height_ : params_.height;
                auto derived = va->derive_surface_to_frame(surface_id, w, h);
                if (derived.is_ok()) {
                    VideoFrame frame = std::move(*derived);
                    frame.set_pts(packet.pts());
                    frame.set_dts(packet.dts());
                    frame.set_duration(packet.duration());
                    frame.set_keyframe(false);
                    ready_frames_.push(std::move(frame));
                    frame_count_++;
                }
            }
            continue;
        }

        uint32_t w = hdr.width;
        uint32_t h = hdr.height;
        if (w == 0) w = (last_width_ > 0) ? last_width_ : params_.width;
        if (h == 0) h = (last_height_ > 0) ? last_height_ : params_.height;

        if (w > 0 && h > 0) {
            params_.width = w;
            params_.height = h;
            last_width_ = w;
            last_height_ = h;

            if (!hw_pipeline_initialized_) {
                init_hardware_pipeline(w, h);
            }

            VideoFrame frame;
            uint32_t target_surface_id = 0;

            if (hw_pipeline_initialized_ && !hw_surfaces_.empty() && hw_device_->type() == HwDeviceType::Vaapi) {
                auto* va = static_cast<VaapiDevice*>(hw_device_.get());

                // Pick a surface that does not conflict with active reference frames
                target_surface_id = hw_surfaces_[current_surface_idx_];
                for (size_t attempt = 0; attempt < hw_surfaces_.size(); ++attempt) {
                    uint32_t cand = hw_surfaces_[(current_surface_idx_ + attempt) % hw_surfaces_.size()];
                    bool in_refs = false;
                    for (size_t i = 0; i < 8; ++i) {
                        if (ref_surfaces_[i] != 0xFFFFFFFF && ref_surfaces_[i] == cand) {
                            in_refs = true;
                            break;
                        }
                    }
                    if (!in_refs) {
                        target_surface_id = cand;
                        current_surface_idx_ = (current_surface_idx_ + attempt + 1) % hw_surfaces_.size();
                        break;
                    }
                }

                std::vector<uint32_t> buf_ids;

#if defined(ATUM_HAS_VAAPI_HEADERS)
                auto params = VaapiVp9DecodeParams::build(hdr, target_surface_id, ref_surfaces_, subframe.data.size());
                auto pic_buf = va->create_buffer(hw_context_id_, 0 /* VAPictureParameterBufferType */, sizeof(params.pic_param), 1, &params.pic_param);
                if (pic_buf.is_ok()) buf_ids.push_back(*pic_buf);

                if (params.has_slice) {
                    auto sl_buf = va->create_buffer(hw_context_id_, 4 /* VASliceParameterBufferType */, sizeof(params.slice_param), 1, &params.slice_param);
                    if (sl_buf.is_ok()) buf_ids.push_back(*sl_buf);
                }
#endif
                auto data_buf = va->create_buffer(hw_context_id_, 5 /* VASliceDataBufferType */, static_cast<uint32_t>(subframe.data.size()), 1, subframe.data.data());
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

                // Update VP9 reference frame surfaces table
                if (hdr.is_keyframe()) {
                    ref_surfaces_.fill(target_surface_id);
                } else {
                    for (size_t i = 0; i < 8; ++i) {
                        if ((hdr.refresh_frame_flags & (1u << i)) != 0) {
                            ref_surfaces_[i] = target_surface_id;
                        }
                    }
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
            frame.set_keyframe(hdr.is_keyframe());

            if (hdr.color_space == Vp9ColorSpace::BT709) {
                frame.set_color_space(ColorSpace::BT709);
            } else if (hdr.color_space == Vp9ColorSpace::BT601 || hdr.color_space == Vp9ColorSpace::SMPTE170) {
                frame.set_color_space(ColorSpace::BT601);
            } else if (hdr.color_space == Vp9ColorSpace::BT2020) {
                frame.set_color_space(ColorSpace::BT2020);
            }
            frame.set_color_range(hdr.color_range_full ? ColorRange::Full : ColorRange::Studio);

            if (hdr.is_keyframe()) {
                dpb_.clear();
            }
            dpb_.insert(frame.clone(), frame_count_, frame_count_, hdr.is_keyframe());

            if (hdr.show_frame) {
                ready_frames_.push(std::move(frame));
                frame_count_++;
            }
        }
    }

    return {};
}

MediaResult<void> Vp9Decoder::receive_frame(VideoFrame& frame) {
    if (ready_frames_.empty()) return MediaError::NeedMoreData;
    frame = std::move(ready_frames_.front());
    ready_frames_.pop();
    return {};
}

MediaResult<void> Vp9Decoder::flush() {
    return {};
}

MediaResult<void> Vp9Decoder::reset() {
    while (!ready_frames_.empty()) ready_frames_.pop();
    dpb_.clear();
    frame_count_ = 0;
    current_surface_idx_ = 0;
    ref_surfaces_.fill(0xFFFFFFFF);
    last_width_ = 0;
    last_height_ = 0;
    return {};
}

} // namespace atum

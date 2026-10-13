#include "atum/hwaccel/vaapi_encoder.hpp"
#include <cstring>
#include <iostream>

#if __has_include(<va/va.h>)
#include <va/va.h>
#include <va/va_enc_h264.h>
#include <va/va_enc_hevc.h>
#define ATUM_HAS_VAAPI_ENC_HEADERS 1
#endif

namespace atum {

VaapiEncoder::VaapiEncoder() {
    params_.media_type = MediaType::Video;
    params_.codec_id = CodecId::H264;
}

VaapiEncoder::~VaapiEncoder() {
    release_pipeline();
}

void VaapiEncoder::release_pipeline() noexcept {
    if (device_ && hw_initialized_) {
        if (coded_buffer_id_ != 0) {
            (void)device_->destroy_buffer(coded_buffer_id_);
            coded_buffer_id_ = 0;
        }
        if (hw_context_id_ != 0) {
            (void)device_->destroy_context(hw_context_id_);
            hw_context_id_ = 0;
        }
        surface_pool_.release();
        if (hw_config_id_ != 0) {
            (void)device_->destroy_config(hw_config_id_);
            hw_config_id_ = 0;
        }
    }
    hw_initialized_ = false;
}

MediaResult<void> VaapiEncoder::init(const CodecParameters& params) {
    return init(params, HwEncoderConfig{});
}

MediaResult<void> VaapiEncoder::init(const CodecParameters& params, const HwEncoderConfig& config) {
    release_pipeline();

    params_ = params;
    config_ = config;
    frame_index_ = 0;

    if (params_.codec_id != CodecId::H264 && params_.codec_id != CodecId::H265) {
        return MediaError::UnsupportedCodec;
    }

    if (params_.width == 0 || params_.height == 0) {
        return MediaError::InvalidData;
    }

    device_ = VaapiDevice::create();
    if (!device_ || !device_->is_available()) {
        return MediaError::HardwareError;
    }

#if defined(ATUM_HAS_VAAPI_ENC_HEADERS)
    uint32_t rc_mode = config_.use_cqp ? VA_RC_CQP : VA_RC_CBR;
    auto cfg_res = device_->create_config(params_.codec_id, true /* is_encoder */, rc_mode);
    if (!cfg_res.is_ok()) return cfg_res.error();
    hw_config_id_ = *cfg_res;

    // Allocate 16 surfaces in GPU memory for input, reference, and reconstruction
    auto pool_res = surface_pool_.init(device_.get(), params_.width, params_.height, PixelFormat::NV12, 16);
    if (!pool_res.is_ok()) return pool_res.error();

    auto ctx_res = device_->create_context(hw_config_id_, params_.width, params_.height, surface_pool_.all_surfaces());
    if (!ctx_res.is_ok()) return ctx_res.error();
    hw_context_id_ = *ctx_res;

    // Allocate output coded bitstream buffer (at least 2MB for 1080p)
    uint32_t max_coded_size = std::max(2u * 1024u * 1024u, params_.width * params_.height * 3u / 2u);
    auto coded_res = device_->create_coded_buffer(hw_context_id_, max_coded_size);
    if (!coded_res.is_ok()) return coded_res.error();
    coded_buffer_id_ = *coded_res;

    hw_initialized_ = true;
    return MediaResult<void>::ok();
#else
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiEncoder::send_frame(const VideoFrame& frame) {
    if (!hw_initialized_ || !device_) return MediaError::HardwareError;
    if (!frame.is_valid()) return MediaError::InvalidData;

#if defined(ATUM_HAS_VAAPI_ENC_HEADERS)
    uint32_t input_surface = HW_INVALID_SURFACE_ID;
    bool borrowed_hw_surface = false;

    if (frame.is_hw_frame() && frame.hw_surface_id() != HW_INVALID_SURFACE_ID) {
        input_surface = frame.hw_surface_id();
        borrowed_hw_surface = true;
    } else {
        input_surface = surface_pool_.acquire_surface();
        if (input_surface == HW_INVALID_SURFACE_ID) {
            // Re-use first surface if pool exhausted
            input_surface = surface_pool_.all_surfaces()[0];
        }
        auto upload_res = device_->upload_frame_to_surface(input_surface, frame);
        if (!upload_res.is_ok()) {
            if (!borrowed_hw_surface) surface_pool_.return_surface(input_surface);
            return upload_res.error();
        }
    }

    const auto& surfaces = surface_pool_.all_surfaces();
    uint32_t recon_surface = surfaces[(frame_index_ + 1) % surfaces.size()];
    uint32_t ref_surface = surfaces[frame_index_ % surfaces.size()];

    bool is_idr = (frame_index_ == 0 || (frame_index_ % config_.idr_interval == 0));
    int64_t pts = (frame.pts() >= 0) ? frame.pts() : static_cast<int64_t>(frame_index_ * 1000000 / config_.fps);
    int64_t dts = (frame.dts() >= 0) ? frame.dts() : pts;

    auto enc_res = encode_h264_frame(input_surface, recon_surface, ref_surface, is_idr, pts, dts);

    if (!borrowed_hw_surface) {
        surface_pool_.return_surface(input_surface);
    }

    if (enc_res.is_ok()) {
        frame_index_++;
    }
    return enc_res;
#else
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiEncoder::encode_h264_frame(
    uint32_t input_surface,
    uint32_t recon_surface,
    uint32_t ref_surface,
    bool is_idr,
    int64_t pts,
    int64_t dts
) {
#if defined(ATUM_HAS_VAAPI_ENC_HEADERS)
    std::vector<uint32_t> param_buffers;

    // 1. Sequence Parameter Buffer (Generated on IDR keyframes)
    if (is_idr) {
        VAEncSequenceParameterBufferH264 seq{};
        seq.seq_parameter_set_id = 0;
        seq.level_idc = 41; // High Profile Level 4.1
        seq.intra_period = config_.gop_size;
        seq.intra_idr_period = config_.idr_interval;
        seq.ip_period = 1;
        seq.bits_per_second = config_.bitrate_bps;
        seq.max_num_ref_frames = 1;
        seq.picture_width_in_mbs = static_cast<uint16_t>((params_.width + 15) / 16);
        seq.picture_height_in_mbs = static_cast<uint16_t>((params_.height + 15) / 16);
        seq.seq_fields.bits.frame_mbs_only_flag = 1;
        seq.seq_fields.bits.direct_8x8_inference_flag = 1;

        auto seq_buf_res = device_->create_buffer(hw_context_id_, VAEncSequenceParameterBufferType, sizeof(seq), 1, &seq);
        if (!seq_buf_res.is_ok()) return seq_buf_res.error();
        param_buffers.push_back(*seq_buf_res);
    }

    // 2. Picture Parameter Buffer
    VAEncPictureParameterBufferH264 pic{};
    pic.CurrPic.picture_id = static_cast<VASurfaceID>(recon_surface);
    pic.CurrPic.flags = 0;
    pic.CurrPic.frame_idx = frame_index_ % config_.gop_size;

    for (size_t i = 0; i < 16; ++i) {
        pic.ReferenceFrames[i].picture_id = VA_INVALID_ID;
        pic.ReferenceFrames[i].flags = VA_PICTURE_H264_INVALID;
    }

    if (!is_idr && ref_surface != HW_INVALID_SURFACE_ID) {
        pic.ReferenceFrames[0].picture_id = static_cast<VASurfaceID>(ref_surface);
        pic.ReferenceFrames[0].flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
    }

    pic.coded_buf = static_cast<VABufferID>(coded_buffer_id_);
    pic.pic_parameter_set_id = 0;
    pic.seq_parameter_set_id = 0;
    pic.frame_num = static_cast<uint16_t>(frame_index_ % config_.gop_size);
    pic.pic_init_qp = static_cast<uint8_t>(config_.qp);
    pic.num_ref_idx_l0_active_minus1 = is_idr ? 0 : 0;
    pic.num_ref_idx_l1_active_minus1 = 0;

    pic.pic_fields.bits.idr_pic_flag = is_idr ? 1 : 0;
    pic.pic_fields.bits.reference_pic_flag = 1;
    pic.pic_fields.bits.entropy_coding_mode_flag = 1; // CABAC
    pic.pic_fields.bits.transform_8x8_mode_flag = 1;

    auto pic_buf_res = device_->create_buffer(hw_context_id_, VAEncPictureParameterBufferType, sizeof(pic), 1, &pic);
    if (!pic_buf_res.is_ok()) {
        for (auto b : param_buffers) (void)device_->destroy_buffer(b);
        return pic_buf_res.error();
    }
    param_buffers.push_back(*pic_buf_res);

    // 3. Slice Parameter Buffer
    VAEncSliceParameterBufferH264 slice{};
    slice.macroblock_address = 0;
    slice.num_macroblocks = static_cast<uint32_t>(((params_.width + 15) / 16) * ((params_.height + 15) / 16));
    slice.macroblock_info = VA_INVALID_ID;
    slice.slice_type = is_idr ? 2 : 0; // 2 = I-Slice, 0 = P-Slice
    slice.pic_parameter_set_id = 0;
    slice.idr_pic_id = is_idr ? static_cast<uint16_t>(frame_index_ / config_.idr_interval) : 0;
    slice.pic_order_cnt_lsb = static_cast<uint16_t>((frame_index_ * 2) % 256);

    auto slice_buf_res = device_->create_buffer(hw_context_id_, VAEncSliceParameterBufferType, sizeof(slice), 1, &slice);
    if (!slice_buf_res.is_ok()) {
        for (auto b : param_buffers) (void)device_->destroy_buffer(b);
        return slice_buf_res.error();
    }
    param_buffers.push_back(*slice_buf_res);

    // 4. Hardware Command Submission
    auto begin_res = device_->begin_picture(hw_context_id_, input_surface);
    if (!begin_res.is_ok()) {
        for (auto b : param_buffers) (void)device_->destroy_buffer(b);
        return begin_res.error();
    }

    auto render_res = device_->render_picture(hw_context_id_, param_buffers.data(), static_cast<uint32_t>(param_buffers.size()));
    auto end_res = device_->end_picture(hw_context_id_);

    // Destroy parameter buffers
    for (auto b : param_buffers) {
        (void)device_->destroy_buffer(b);
    }

    if (!render_res.is_ok()) return render_res.error();
    if (!end_res.is_ok()) return end_res.error();

    // 5. Hardware Synchronization
    auto sync_res = device_->sync_surface(input_surface);
    if (!sync_res.is_ok()) return sync_res.error();

    // 6. Retrieve Coded Bitstream Packet
    std::vector<uint8_t> coded_bytes;
    auto read_res = device_->read_coded_buffer(coded_buffer_id_, coded_bytes);
    if (!read_res.is_ok()) return read_res.error();

    if (!coded_bytes.empty()) {
        Packet packet = Packet::from_bytes(coded_bytes);
        packet.set_pts(pts);
        packet.set_dts(dts);
        packet.set_duration(1000000 / config_.fps);
        packet.set_keyframe(is_idr);
        ready_packets_.push(std::move(packet));
    }

    return MediaResult<void>::ok();
#else
    (void)input_surface; (void)recon_surface; (void)ref_surface; (void)is_idr; (void)pts; (void)dts;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiEncoder::receive_packet(Packet& packet) {
    if (ready_packets_.empty()) {
        return MediaError::NeedMoreData;
    }
    packet = std::move(ready_packets_.front());
    ready_packets_.pop();
    return MediaResult<void>::ok();
}

MediaResult<void> VaapiEncoder::flush() {
    return MediaResult<void>::ok();
}

} // namespace atum

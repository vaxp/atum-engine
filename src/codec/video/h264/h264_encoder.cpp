#include "atum/codec/video/h264/h264_encoder.hpp"
#include "atum/codec/video/h264/h264_entropy_encoder.hpp"
#include "atum/codec/video/h264/h264_rdo.hpp"
#include "atum/codec/video/h264/h264_forward_transform.hpp"
#include "atum/transform/h264_transform.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

H264Encoder::H264Encoder(const H264EncoderConfig& config)
    : config_(config), rc_(config.rc) {
    init_parameter_sets();
}

void H264Encoder::init_parameter_sets() {
    mb_width_ = (config_.width + 15) / 16;
    mb_height_ = (config_.height + 15) / 16;

    // 1. Initialize SPS
    sps_.profile_idc = (config_.profile == H264Profile::High) ? 100 :
                       ((config_.profile == H264Profile::Main) ? 77 : 66);
    sps_.constraint_set0_flag = false;
    sps_.constraint_set1_flag = (sps_.profile_idc == 66);
    sps_.constraint_set2_flag = false;
    sps_.constraint_set3_flag = false;
    sps_.constraint_set4_flag = false;
    sps_.constraint_set5_flag = false;
    sps_.level_idc = 40; // Level 4.0
    sps_.seq_parameter_set_id = 0;
    sps_.chroma_format_idc = 1; // 4:2:0
    sps_.bit_depth_luma_minus8 = 0;
    sps_.bit_depth_chroma_minus8 = 0;
    sps_.log2_max_frame_num_minus4 = 0;
    sps_.pic_order_cnt_type = 0;
    sps_.log2_max_pic_order_cnt_lsb_minus4 = 0;
    sps_.max_num_ref_frames = 1;
    sps_.pic_width_in_mbs_minus1 = mb_width_ - 1;
    sps_.pic_height_in_map_units_minus1 = mb_height_ - 1;
    sps_.frame_mbs_only_flag = true;
    sps_.direct_8x8_inference_flag = true;

    // Cropping flags for non-multiple of 16 resolutions
    if (config_.width != mb_width_ * 16 || config_.height != mb_height_ * 16) {
        sps_.frame_cropping_flag = true;
        sps_.frame_crop_left_offset = 0;
        sps_.frame_crop_right_offset = (mb_width_ * 16 - config_.width) / 2;
        sps_.frame_crop_top_offset = 0;
        sps_.frame_crop_bottom_offset = (mb_height_ * 16 - config_.height) / 2;
    } else {
        sps_.frame_cropping_flag = false;
    }
    sps_.vui_parameters_present_flag = false;

    // Serialize SPS to Annex-B
    std::vector<uint8_t> sps_rbsp = H264NalSerializer::serialize_sps(sps_);
    sps_annexb_ = H264NalSerializer::package_annexb(3, H264NalType::Sps, sps_rbsp);

    // 2. Initialize PPS
    pps_.pic_parameter_set_id = 0;
    pps_.seq_parameter_set_id = 0;
    pps_.entropy_coding_mode_flag = config_.use_cabac;
    pps_.pic_init_qp_minus26 = config_.rc.base_qp - 26;
    pps_.deblocking_filter_control_present_flag = true;

    // Serialize PPS to Annex-B
    std::vector<uint8_t> pps_rbsp = H264NalSerializer::serialize_pps(pps_);
    pps_annexb_ = H264NalSerializer::package_annexb(3, H264NalType::Pps, pps_rbsp);

    // Allocate reconstruction & reference frame buffers (32-byte SIMD aligned)
    ref_frame_ = VideoFrame::allocate(mb_width_ * 16, mb_height_ * 16, PixelFormat::YUV420P);
    recon_frame_ = VideoFrame::allocate(mb_width_ * 16, mb_height_ * 16, PixelFormat::YUV420P);
    has_ref_ = false;
    frame_num_ = 0;
    idr_pic_id_ = 0;
}

Packet H264Encoder::encode_frame(const VideoFrame& frame, int64_t pts, bool force_idr) {
    bool is_idr = (!has_ref_ || force_idr || (frame_num_ % config_.gop_size == 0));
    bool is_p = !is_idr;

    // Calculate spatial complexity for rate control
    double complexity = H264RateControl::calculate_spatial_complexity(
        frame.plane(0), frame.stride(0), frame.width(), frame.height()
    );
    int32_t frame_qp = rc_.start_frame(is_idr ? H264FrameType::IDR : H264FrameType::P, complexity);

    // Prepare Slice Header
    H264SliceHeader header{};
    header.first_mb_in_slice = 0;
    header.slice_type = is_p ? H264SliceType::P : H264SliceType::I;
    header.pic_parameter_set_id = 0;
    header.frame_num = frame_num_ & 0x0F;
    header.idr_pic_id = is_idr ? idr_pic_id_++ : 0;
    header.pic_order_cnt_lsb = (frame_num_ * 2) & 0x0F;
    header.slice_qp_delta = frame_qp - (pps_.pic_init_qp_minus26 + 26);

    // Bitstream serialization
    BitWriter bw;
    H264NalSerializer::serialize_slice_header(bw, header, sps_, pps_);

    // Setup entropy encoder
    std::unique_ptr<H264CavlcEncoder> cavlc;
    std::unique_ptr<H264CabacEncoder> cabac;
    if (config_.use_cabac) {
        cabac = std::make_unique<H264CabacEncoder>(bw);
        cabac->init_contexts(header.slice_type, frame_qp);
    } else {
        cavlc = std::make_unique<H264CavlcEncoder>(bw);
    }

    // Tracking grids for MV and nC predictions
    std::vector<std::vector<MotionVector>> mv_grid(mb_height_, std::vector<MotionVector>(mb_width_));
    std::vector<std::vector<int32_t>> nc_grid(mb_height_, std::vector<int32_t>(mb_width_, 0));

    const uint8_t* orig_y = frame.plane(0);
    size_t orig_y_stride = frame.stride(0);
    const uint8_t* orig_cb = frame.plane(1);
    size_t orig_cb_stride = frame.stride(1);
    const uint8_t* orig_cr = frame.plane(2);
    size_t orig_cr_stride = frame.stride(2);

    uint8_t* recon_y = recon_frame_.plane_mut(0);
    size_t recon_y_stride = recon_frame_.stride(0);
    uint8_t* recon_cb = recon_frame_.plane_mut(1);
    size_t recon_cb_stride = recon_frame_.stride(1);
    uint8_t* recon_cr = recon_frame_.plane_mut(2);
    size_t recon_cr_stride = recon_frame_.stride(2);

    const uint8_t* ref_y = has_ref_ ? ref_frame_.plane(0) : nullptr;
    size_t ref_y_stride = has_ref_ ? ref_frame_.stride(0) : 0;

    // Macroblock Iteration Loop
    for (uint32_t my = 0; my < mb_height_; ++my) {
        for (uint32_t mx = 0; mx < mb_width_; ++mx) {
            // Compute MVP from neighbors
            MotionVector mv_left = (mx > 0) ? mv_grid[my][mx - 1] : MotionVector{};
            MotionVector mv_top = (my > 0) ? mv_grid[my - 1][mx] : MotionVector{};
            MotionVector mv_tr = (my > 0 && mx + 1 < mb_width_) ? mv_grid[my - 1][mx + 1] : MotionVector{};
            MotionVector mv_tl = (my > 0 && mx > 0) ? mv_grid[my - 1][mx - 1] : MotionVector{};

            MotionVector mvp = MotionEstimator::calculate_mvp(
                mv_left, mx > 0,
                mv_top, my > 0,
                mv_tr, (my > 0 && mx + 1 < mb_width_),
                mv_tl, (my > 0 && mx > 0)
            );

            // RDO mode decision
            MacroblockDecision decision = H264Rdo::select_best_mode(
                orig_y + my * 16 * orig_y_stride + mx * 16, orig_y_stride,
                orig_cb + my * 8 * orig_cb_stride + mx * 8, orig_cb_stride,
                orig_cr + my * 8 * orig_cr_stride + mx * 8, orig_cr_stride,
                recon_y, recon_y_stride,
                recon_cb, recon_cb_stride,
                recon_cr, recon_cr_stride,
                ref_y, ref_y_stride,
                mb_width_ * 16, mb_height_ * 16,
                static_cast<int32_t>(mx), static_cast<int32_t>(my),
                mvp,
                frame_qp, is_p,
                config_.me_method, config_.me_range
            );

            // Update reconstructed frame pixels
            for (uint32_t y = 0; y < 16; ++y) {
                std::memcpy(
                    recon_y + (my * 16 + y) * recon_y_stride + mx * 16,
                    decision.recon_y.data() + y * 16,
                    16
                );
            }
            for (uint32_t y = 0; y < 8; ++y) {
                std::memcpy(
                    recon_cb + (my * 8 + y) * recon_cb_stride + mx * 8,
                    decision.recon_cb.data() + y * 8,
                    8
                );
                std::memcpy(
                    recon_cr + (my * 8 + y) * recon_cr_stride + mx * 8,
                    decision.recon_cr.data() + y * 8,
                    8
                );
            }

            // Save MV in grid
            mv_grid[my][mx] = decision.mvs[0];

            // Entropy encode macroblock
            int32_t nC_left = (mx > 0) ? nc_grid[my][mx - 1] : 0;
            int32_t nC_top = (my > 0) ? nc_grid[my - 1][mx] : 0;

            if (config_.use_cabac) {
                cabac->encode_macroblock(decision, is_p, 0);
            } else {
                cavlc->encode_macroblock(decision, is_p, 0, nC_left, nC_top);
            }
            nc_grid[my][mx] = (decision.cbp > 0) ? 4 : 0;
        }
    }

    if (config_.use_cabac) {
        cabac->flush();
    } else {
        bw.write_bit(1); // rbsp_stop_one_bit
        bw.align(0);
    }

    // Package Slice NAL
    uint8_t nal_ref_idc = is_idr ? 3 : 2;
    H264NalType nal_type = is_idr ? H264NalType::SliceIdr : H264NalType::SliceNonIdr;
    std::vector<uint8_t> slice_annexb = H264NalSerializer::package_annexb(nal_ref_idc, nal_type, bw.buffer());

    // Assemble output Packet
    std::vector<uint8_t> payload;
    if (is_idr) {
        payload.insert(payload.end(), sps_annexb_.begin(), sps_annexb_.end());
        payload.insert(payload.end(), pps_annexb_.begin(), pps_annexb_.end());
    }
    payload.insert(payload.end(), slice_annexb.begin(), slice_annexb.end());

    Packet pkt = Packet::from_bytes(payload);
    pkt.set_pts(pts);
    pkt.set_dts(pts);
    pkt.set_duration(1000000 / std::max(config_.fps, 1u));
    pkt.set_stream_index(0);
    pkt.set_flags(is_idr ? PacketFlags::KeyFrame : PacketFlags::None);

    // Rate control post-frame update
    rc_.end_frame(is_idr ? H264FrameType::IDR : H264FrameType::P, pkt.size() * 8);

    // Promote reconstructed frame to reference picture in DPB
    for (int p = 0; p < 3; ++p) {
        uint32_t h = (p == 0) ? mb_height_ * 16 : mb_height_ * 8;
        for (uint32_t y = 0; y < h; ++y) {
            std::memcpy(
                ref_frame_.plane_mut(p) + y * ref_frame_.stride(p),
                recon_frame_.plane(p) + y * recon_frame_.stride(p),
                (p == 0) ? mb_width_ * 16 : mb_width_ * 8
            );
        }
    }
    has_ref_ = true;
    frame_num_++;

    return pkt;
}

std::vector<Packet> H264Encoder::flush() {
    return {};
}

} // namespace atum

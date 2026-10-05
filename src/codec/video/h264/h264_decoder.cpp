#include "atum/codec/video/h264/h264_decoder.hpp"
#include <algorithm>
#include <cstring>
#include "atum/entropy/cabac.hpp"
#include "atum/entropy/h264_cabac_contexts.hpp"
#include "atum/codec/video/h264/h264_macroblock_syntax.hpp"
#include "atum/codec/video/h264/h264_residual_cabac.hpp"
#include "atum/transform/h264_transform.hpp"
#include "atum/transform/simd_transform.hpp"
#include "atum/predict/intra_h264.hpp"
#include "atum/predict/inter_filter.hpp"
#include "atum/inloop/deblock_h264.hpp"
#include "atum/hwaccel/vaapi_device.hpp"

namespace atum {

H264Decoder::H264Decoder() {
    params_.codec_id = CodecId::H264;
    params_.media_type = MediaType::Video;
}

MediaResult<void> H264Decoder::init(const CodecParameters& params) {
    params_ = params;
    params_.codec_id = CodecId::H264;
    params_.media_type = MediaType::Video;
    frame_count_ = 0;
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

    return {};
}

MediaResult<void> H264Decoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    auto data = packet.as_span();
    std::span<const uint8_t> latest_slice_payload;

    if (is_avcc_ && nal_length_size_ > 0) {
        size_t offset = 0;
        while (offset + nal_length_size_ <= data.size()) {
            uint32_t nal_len = 0;
            for (size_t i = 0; i < nal_length_size_; ++i) {
                nal_len = (nal_len << 8) | data[offset + i];
            }
            offset += nal_length_size_;
            if (offset + nal_len > data.size()) break;

            std::span<const uint8_t> nal_data{data.data() + offset, nal_len};
            if (!nal_data.empty()) {
                NalUnit nal{};
                nal.forbidden_zero_bit = (nal_data[0] >> 7) & 1;
                nal.ref_idc = (nal_data[0] >> 5) & 3;
                nal.type = nal_data[0] & 0x1F;
                nal.payload = nal_data.subspan(1);
                if (nal.type == static_cast<uint8_t>(H264NalType::SliceIdr) ||
                    nal.type == static_cast<uint8_t>(H264NalType::SliceNonIdr)) {
                    latest_slice_payload = nal.payload;
                }
                (void)parser_.parse_nal(nal);
            }
            offset += nal_len;
        }
    } else {
        std::vector<NalUnit> nals;
        if (error_concealment_enabled_) {
            nals = resilient_parser_.parse_and_recover(data);
        } else {
            nals = NalParser::split_annex_b(data);
        }
        for (const auto& nal : nals) {
            if (nal.type == static_cast<uint8_t>(H264NalType::SliceIdr) ||
                nal.type == static_cast<uint8_t>(H264NalType::SliceNonIdr)) {
                latest_slice_payload = nal.payload;
            }
            (void)parser_.parse_nal(nal);
        }
    }

    uint32_t w = parser_.width() > 0 ? parser_.width() : params_.width;
    uint32_t h = parser_.height() > 0 ? parser_.height() : params_.height;

    if (w > 0 && h > 0) {
        params_.width = w;
        params_.height = h;

        VideoFrame frame = VideoFrame::allocate(w, h, PixelFormat::YUV420P);
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

        const H264SliceHeader* slice_hdr = parser_.last_slice_header().has_value() ?
                                           &*parser_.last_slice_header() : nullptr;
        reconstruct_frame(frame, slice_hdr, parser_.last_slice_data());

        int32_t poc = slice_hdr ? static_cast<int32_t>(slice_hdr->pic_order_cnt_lsb) : static_cast<int32_t>(frame_count_);
        uint32_t frame_num = slice_hdr ? slice_hdr->frame_num : frame_count_;

        if (is_key) {
            dpb_.clear();
        }
        dpb_.insert(frame.clone(), poc, frame_num, is_key);

        ready_frames_.push(std::move(frame));
        frame_count_++;
    }

    return {};
}

void H264Decoder::reconstruct_frame(VideoFrame& frame, const H264SliceHeader* slice_hdr, std::span<const uint8_t> payload) {
    uint32_t w = frame.width();
    uint32_t h = frame.height();
    uint32_t mb_cols = (w + 15) / 16;
    uint32_t mb_rows = (h + 15) / 16;

    uint8_t* y_plane = frame.plane_mut(0);
    uint8_t* u_plane = frame.plane_mut(1);
    uint8_t* v_plane = frame.plane_mut(2);
    size_t y_stride = frame.stride(0);
    size_t u_stride = frame.stride(1);
    size_t v_stride = frame.stride(2);

    const auto* pps = parser_.active_pps();
    int32_t qp = pps ? (26 + pps->pic_init_qp_minus26) : 26;
    if (slice_hdr) {
        qp += slice_hdr->slice_qp_delta;
    }
    qp = std::clamp(qp, 0, 51);

    bool is_intra = !slice_hdr || slice_hdr->is_keyframe() || dpb_.empty();
    H264SliceType st = slice_hdr ? slice_hdr->slice_type : H264SliceType::I;

    H264CabacContextSet cabac_contexts;
    std::unique_ptr<CabacDecoder> cabac;
    std::unique_ptr<H264MacroblockDecoder> mb_decoder;
    std::unique_ptr<H264ResidualCabacDecoder> res_decoder;

    if (pps && pps->entropy_coding_mode_flag && !payload.empty()) {
        cabac = std::make_unique<CabacDecoder>(payload);
        uint32_t idc = slice_hdr ? slice_hdr->cabac_init_idc : 0;
        cabac_contexts.init_slice(st, idc, qp);
        mb_decoder = std::make_unique<H264MacroblockDecoder>(*cabac, cabac_contexts, pps ? pps->transform_8x8_mode_flag : false);
        res_decoder = std::make_unique<H264ResidualCabacDecoder>(*cabac, cabac_contexts);
    }

    std::vector<H264NeighborMbInfo> mb_grid(mb_cols * mb_rows);
    damage_mask_.reset(w, h, 16);
    if (payload.empty()) {
        damage_mask_.mark_damaged_range(0, 0, mb_cols, mb_rows);
    }

    const VideoFrame* ref_frame = dpb_.empty() ? nullptr : dpb_.get_ref(0);
    const uint8_t* ref_y = ref_frame ? ref_frame->plane(0) : nullptr;
    const uint8_t* ref_u = ref_frame ? ref_frame->plane(1) : nullptr;
    const uint8_t* ref_v = ref_frame ? ref_frame->plane(2) : nullptr;
    size_t ref_y_stride = ref_frame ? ref_frame->stride(0) : 0;
    size_t ref_u_stride = ref_frame ? ref_frame->stride(1) : 0;
    size_t ref_v_stride = ref_frame ? ref_frame->stride(2) : 0;

    for (uint32_t mby = 0; mby < mb_rows; ++mby) {
        for (uint32_t mbx = 0; mbx < mb_cols; ++mbx) {
            uint32_t px = mbx * 16;
            uint32_t py = mby * 16;
            bool has_top = (mby > 0);
            bool has_left = (mbx > 0);
            bool has_top_left = (has_top && has_left);

            H264NeighborMbInfo left_mb = (mbx > 0) ? mb_grid[mby * mb_cols + (mbx - 1)] : H264NeighborMbInfo{};
            H264NeighborMbInfo top_mb  = (mby > 0) ? mb_grid[(mby - 1) * mb_cols + mbx] : H264NeighborMbInfo{};

            H264MbInfo mb_info{};
            int32_t luma_res[256]{};
            int32_t cb_res[64]{};
            int32_t cr_res[64]{};

            if (mb_decoder && !cabac->is_end_of_substream()) {
                auto mb_res = mb_decoder->decode_macroblock_header(st, left_mb, top_mb, 1, 1);
                if (mb_res.is_ok()) {
                    mb_info = *mb_res;
                    if (mb_info.total_cbp() > 0 && res_decoder && !cabac->is_end_of_substream()) {
                        (void)res_decoder->decode_macroblock_residual(
                            mb_info.is_intra16x16,
                            mb_info.cbp_luma,
                            mb_info.cbp_chroma,
                            luma_res, cb_res, cr_res
                        );
                    }
                }
            }

            if (dpb_.empty() || st == H264SliceType::I || st == H264SliceType::SI) {
                mb_info.is_intra = true;
            }

            H264NeighborMbInfo& cur_mb = mb_grid[mby * mb_cols + mbx];
            cur_mb.available = true;
            cur_mb.is_skip = mb_info.is_skip;
            cur_mb.is_intra = mb_info.is_intra;
            cur_mb.cbp_luma_bit = mb_info.cbp_luma & 1;
            cur_mb.cbp_chroma = mb_info.cbp_chroma;
            cur_mb.ref_idx[0] = mb_info.ref_idx[0];
            cur_mb.mvd[0][0] = mb_info.mvd[0][0];
            cur_mb.mvd[0][1] = mb_info.mvd[0][1];
            cur_mb.transform_size_8x8_flag = mb_info.transform_size_8x8_flag;
            for (int k = 0; k < 16; ++k) {
                cur_mb.intra4x4_pred_mode[k] = mb_info.intra4x4_pred_mode[k];
            }

            uint8_t pred_dst[256]{};
            uint8_t u_pred[64]{};
            uint8_t v_pred[64]{};

            NeighborFlags avail = NeighborFlags::None;
            if (has_left) avail = avail | NeighborFlags::Left;
            if (has_top) avail = avail | NeighborFlags::Top;
            if (has_top_left) avail = avail | NeighborFlags::TopLeft;

            if (mb_info.is_intra) {
                // Intra Luma
                uint8_t top[16];
                uint8_t left[16];
                uint8_t tl = 128;

                if (has_top) {
                    const uint8_t* top_row = y_plane + (py - 1) * y_stride + px;
                    for (int i = 0; i < 16; ++i) top[i] = (px + static_cast<uint32_t>(i) < w) ? top_row[i] : 128;
                } else {
                    std::fill_n(top, 16, static_cast<uint8_t>(128));
                }

                if (has_left) {
                    for (int i = 0; i < 16; ++i) {
                        left[i] = (py + static_cast<uint32_t>(i) < h) ? *(y_plane + (py + i) * y_stride + px - 1) : 128;
                    }
                } else {
                    std::fill_n(left, 16, static_cast<uint8_t>(128));
                }

                if (has_top_left) {
                    tl = *(y_plane + (py - 1) * y_stride + px - 1);
                }

                uint32_t mode = 2; // DC
                if (mb_info.is_intra16x16) {
                    mode = mb_info.intra16x16_pred_mode;
                } else if (has_top && has_left) {
                    mode = 2;
                } else if (has_top) {
                    mode = 0;
                } else if (has_left) {
                    mode = 1;
                }

                IntraH264::predict16x16(mode, avail, top, left, tl, pred_dst, 16);

                // Intra Chroma
                uint32_t cpx = mbx * 8;
                uint32_t cpy = mby * 8;
                uint32_t cw = frame.plane_width(1);
                uint32_t ch = frame.plane_height(1);

                auto predict_chroma_plane = [&](const uint8_t* cplane, size_t cstride, uint8_t* out_cpred) {
                    uint8_t ctop[8];
                    uint8_t cleft[8];
                    uint8_t ctl = 128;
                    if (has_top) {
                        const uint8_t* tr = cplane + (cpy - 1) * cstride + cpx;
                        for (int i = 0; i < 8; ++i) ctop[i] = (cpx + static_cast<uint32_t>(i) < cw) ? tr[i] : 128;
                    } else {
                        std::fill_n(ctop, 8, static_cast<uint8_t>(128));
                    }
                    if (has_left) {
                        for (int i = 0; i < 8; ++i) {
                            cleft[i] = (cpy + static_cast<uint32_t>(i) < ch) ? *(cplane + (cpy + i) * cstride + cpx - 1) : 128;
                        }
                    } else {
                        std::fill_n(cleft, 8, static_cast<uint8_t>(128));
                    }
                    if (has_top_left) {
                        ctl = *(cplane + (cpy - 1) * cstride + cpx - 1);
                    }
                    IntraH264::predict_chroma8x8(mb_info.intra_chroma_pred_mode, avail, ctop, cleft, ctl, out_cpred, 8);
                };

                predict_chroma_plane(u_plane, u_stride, u_pred);
                predict_chroma_plane(v_plane, v_stride, v_pred);
            } else {
                // Inter / Skip prediction from DPB
                int32_t mv_x = 0;
                int32_t mv_y = 0;
                if (!mb_info.is_skip) {
                    mv_x = mb_info.mvd[0][0] / 4;
                    mv_y = mb_info.mvd[0][1] / 4;
                }

                if (ref_y) {
                    for (uint32_t y = 0; y < 16; ++y) {
                        int32_t sy = std::clamp(static_cast<int32_t>(py + y) + mv_y, 0, static_cast<int32_t>(h - 1));
                        for (uint32_t x = 0; x < 16; ++x) {
                            int32_t sx = std::clamp(static_cast<int32_t>(px + x) + mv_x, 0, static_cast<int32_t>(w - 1));
                            pred_dst[y * 16 + x] = ref_y[sy * ref_y_stride + sx];
                        }
                    }
                } else {
                    std::fill_n(pred_dst, 256, static_cast<uint8_t>(16));
                }

                // Chroma Inter
                int32_t cmv_x = mv_x / 2;
                int32_t cmv_y = mv_y / 2;
                uint32_t cpx = mbx * 8;
                uint32_t cpy = mby * 8;
                uint32_t cw = frame.plane_width(1);
                uint32_t ch = frame.plane_height(1);

                for (uint32_t y = 0; y < 8; ++y) {
                    int32_t sy = std::clamp(static_cast<int32_t>(cpy + y) + cmv_y, 0, static_cast<int32_t>(ch - 1));
                    for (uint32_t x = 0; x < 8; ++x) {
                        int32_t sx = std::clamp(static_cast<int32_t>(cpx + x) + cmv_x, 0, static_cast<int32_t>(cw - 1));
                        u_pred[y * 8 + x] = ref_u ? ref_u[sy * ref_u_stride + sx] : 128;
                        v_pred[y * 8 + x] = ref_v ? ref_v[sy * ref_v_stride + sx] : 128;
                    }
                }
            }

            // Write Luma (Prediction + Dequantized Residuals)
            for (uint32_t by = 0; by < 4; ++by) {
                for (uint32_t bx = 0; bx < 4; ++bx) {
                    uint32_t bpx = px + bx * 4;
                    uint32_t bpy = py + by * 4;
                    if (bpx < w && bpy < h) {
                        int32_t res[16]{};
                        uint32_t quad_idx = (by / 2) * 2 + (bx / 2);
                        uint32_t sub_idx  = (by % 2) * 2 + (bx % 2);
                        uint32_t blk_idx  = quad_idx * 4 + sub_idx;
                        for (int k = 0; k < 16; ++k) {
                            res[k] = luma_res[blk_idx * 16 + k];
                        }
                        H264Transform::dequant4x4(res, qp);

                        uint8_t block_pred[16];
                        for (int r = 0; r < 4; ++r) {
                            for (int c = 0; c < 4; ++c) {
                                block_pred[r * 4 + c] = pred_dst[(by * 4 + r) * 16 + (bx * 4 + c)];
                            }
                        }
                        if (bpx + 4 <= w && bpy + 4 <= h) {
                            simd::idct4x4_h264_add(res, block_pred, y_plane + bpy * y_stride + bpx, y_stride, 4);
                        } else {
                            for (uint32_t r = 0; r < 4 && bpy + r < h; ++r) {
                                for (uint32_t c = 0; c < 4 && bpx + c < w; ++c) {
                                    int32_t p = block_pred[r * 4 + c];
                                    y_plane[(bpy + r) * y_stride + bpx + c] = static_cast<uint8_t>(std::clamp(p + ((res[r * 4 + c] + 32) >> 6), 0, 255));
                                }
                            }
                        }
                    }
                }
            }

            // Write Chroma (U & V)
            uint32_t cpx = mbx * 8;
            uint32_t cpy = mby * 8;
            uint32_t cw = frame.plane_width(1);
            uint32_t ch = frame.plane_height(1);

            auto write_chroma = [&](uint8_t* cplane, size_t cstride, const uint8_t* pred_src, const int32_t* c_res) {
                for (uint32_t by = 0; by < 2; ++by) {
                    for (uint32_t bx = 0; bx < 2; ++bx) {
                        uint32_t bpx = cpx + bx * 4;
                        uint32_t bpy = cpy + by * 4;
                        if (bpx < cw && bpy < ch) {
                            int32_t res[16]{};
                            uint32_t blk_idx = by * 2 + bx;
                            for (int k = 0; k < 16; ++k) res[k] = c_res[blk_idx * 16 + k];
                            H264Transform::dequant4x4(res, qp);

                            uint8_t block_pred[16];
                            for (int r = 0; r < 4; ++r) {
                                for (int c = 0; c < 4; ++c) {
                                    block_pred[r * 4 + c] = pred_src[(by * 4 + r) * 8 + (bx * 4 + c)];
                                }
                            }
                            if (bpx + 4 <= cw && bpy + 4 <= ch) {
                                simd::idct4x4_h264_add(res, block_pred, cplane + bpy * cstride + bpx, cstride, 4);
                            } else {
                                for (uint32_t r = 0; r < 4 && bpy + r < ch; ++r) {
                                    for (uint32_t c = 0; c < 4 && bpx + c < cw; ++c) {
                                        int32_t p = block_pred[r * 4 + c];
                                        cplane[(bpy + r) * cstride + bpx + c] = static_cast<uint8_t>(std::clamp(p + ((res[r * 4 + c] + 32) >> 6), 0, 255));
                                    }
                                }
                            }
                        }
                    }
                }
            };

            write_chroma(u_plane, u_stride, u_pred, cb_res);
            write_chroma(v_plane, v_stride, v_pred, cr_res);
        }
    }

    // Apply Deblocking Filter with strict picture boundary preservation
    for (uint32_t mby = 0; mby < mb_rows; ++mby) {
        for (uint32_t mbx = 0; mbx < mb_cols; ++mbx) {
            uint32_t px = mbx * 16;
            uint32_t py = mby * 16;
            if (px + 16 <= w && py + 16 <= h) {
                uint8_t bS_vert[4][4];
                uint8_t bS_horiz[4][4];
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        bS_vert[i][j] = (i == 0) ? (mbx > 0 ? (is_intra ? 4 : 2) : 0) : 1;
                        bS_horiz[i][j] = (i == 0) ? (mby > 0 ? (is_intra ? 4 : 2) : 0) : 1;
                    }
                }
                DeblockH264::filter_macroblock(
                    y_plane + py * y_stride + px,
                    y_stride,
                    bS_vert,
                    bS_horiz,
                    qp
                );
            }
        }
    }

    if (error_concealment_enabled_ && damage_mask_.has_damaged_blocks()) {
        (void)error_concealer_.conceal(frame, damage_mask_, &dpb_);
    }
}

MediaResult<void> H264Decoder::receive_frame(VideoFrame& frame) {
    if (ready_frames_.empty()) {
        return MediaError::NeedMoreData;
    }
    frame = std::move(ready_frames_.front());
    ready_frames_.pop();
    return {};
}

MediaResult<void> H264Decoder::flush() {
    while (!ready_frames_.empty()) {
        ready_frames_.pop();
    }
    return {};
}

MediaResult<void> H264Decoder::reset() {
    flush();
    dpb_.clear();
    parser_.reset();
    frame_count_ = 0;
    return {};
}

} // namespace atum

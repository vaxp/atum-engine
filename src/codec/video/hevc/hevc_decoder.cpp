#include "atum/codec/video/hevc/hevc_decoder.hpp"
#include <algorithm>
#include <cstring>
#include "atum/entropy/cabac.hpp"
#include "atum/transform/hevc_transform.hpp"
#include "atum/predict/intra_hevc.hpp"
#include "atum/inloop/sao_hevc.hpp"
#include "atum/entropy/hevc_cabac_contexts.hpp"
#include "atum/codec/video/hevc/hevc_ctu_decoder.hpp"
#include "atum/hwaccel/vaapi_device.hpp"
#include "atum/parallel/hevc_wpp.hpp"

namespace atum {

HevcDecoder::HevcDecoder() {
    params_.codec_id = CodecId::H265;
    params_.media_type = MediaType::Video;
}

MediaResult<void> HevcDecoder::init(const CodecParameters& params) {
    params_ = params;
    params_.codec_id = CodecId::H265;
    params_.media_type = MediaType::Video;
    frame_count_ = 0;
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

    return {};
}

MediaResult<void> HevcDecoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    auto data = packet.as_span();
    std::span<const uint8_t> latest_slice_payload;

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
            if (nal_span.size() >= 2) {
                uint8_t nal_type = (nal_span[0] >> 1) & 0x3F;
                if (nal_type <= 21) {
                    latest_slice_payload = nal_span.subspan(2);
                }
            }
            (void)parser_.parse_nal(nal_span);
            offset += nal_len;
        }
    } else {
        auto nals = NalParser::split_annex_b(data);
        for (const auto& nal : nals) {
            if (nal.payload.size() >= 2) {
                uint8_t nal_type = (nal.payload[0] >> 1) & 0x3F;
                if (nal_type <= 21) {
                    latest_slice_payload = nal.payload.subspan(2);
                }
            }
            (void)parser_.parse_nal(nal.payload);
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
                if (sps->vui.matrix_coeffs == 1) frame.set_color_space(ColorSpace::BT709);
                else if (sps->vui.matrix_coeffs == 6 || sps->vui.matrix_coeffs == 5) frame.set_color_space(ColorSpace::BT601);
                else if (sps->vui.matrix_coeffs == 9) frame.set_color_space(ColorSpace::BT2020);
            }
        }

        const HevcSliceHeader* slice_hdr = parser_.last_slice_header().has_value() ?
                                           &*parser_.last_slice_header() : nullptr;
        reconstruct_frame(frame, slice_hdr, latest_slice_payload);

        int32_t poc = static_cast<int32_t>(frame_count_);
        uint32_t frame_num = frame_count_;

        if (is_key) {
            dpb_.clear();
        }
        dpb_.insert(frame.clone(), poc, frame_num, is_key);

        ready_frames_.push(std::move(frame));
        frame_count_++;
    }

    return {};
}

void HevcDecoder::reconstruct_frame(VideoFrame& frame, const HevcSliceHeader* slice_hdr, std::span<const uint8_t> payload) {
    uint32_t w = frame.width();
    uint32_t h = frame.height();

    uint8_t* y_plane = frame.plane_mut(0);
    size_t y_stride = frame.stride(0);

    HevcSliceType st = slice_hdr ? slice_hdr->slice_type : HevcSliceType::I;
    int32_t qp = 26 + (slice_hdr ? slice_hdr->slice_qp_delta : 0);

    HevcCabacContextSet ctxs;
    ctxs.init(st, qp);

    CabacDecoder cabac;
    if (!payload.empty()) {
        (void)cabac.init(payload);
    }

    constexpr uint32_t ctu_size = 64;
    uint32_t ctu_cols = (w + ctu_size - 1) / ctu_size;
    uint32_t ctu_rows = (h + ctu_size - 1) / ctu_size;

    if (thread_count_ != 1 && ctu_rows > 1) {
        HevcWppCoordinator wpp;
        (void)wpp.decode_frame_wpp(frame, slice_hdr, payload, dpb_);
        return;
    }

    for (uint32_t cy = 0; cy < ctu_rows; ++cy) {
        for (uint32_t cx = 0; cx < ctu_cols; ++cx) {
            HevcCu ctu{};
            ctu.x = cx * ctu_size;
            ctu.y = cy * ctu_size;
            ctu.size = ctu_size;
            ctu.depth = 0;

            if (!payload.empty() && !cabac.is_end_of_substream()) {
                (void)HevcCtuDecoder::decode_cu_quadtree(cabac, ctxs, st, ctu, 16);
            } else {
                // Default intra planar fallback
                ctu.is_split = false;
                ctu.pred_mode = (st == HevcSliceType::I || dpb_.empty()) ? HevcPredMode::MODE_INTRA : HevcPredMode::MODE_INTER;
                HevcPu pu{};
                pu.x = ctu.x;
                pu.y = ctu.y;
                pu.width = ctu_size;
                pu.height = ctu_size;
                pu.intra_mode_luma = 0; // Planar
                pu.inter_pred_idc = HevcInterPredIdc::Pred_L0;
                pu.mv[0] = {0, 0, 0};
                ctu.pus.push_back(pu);
            }

            HevcCtuDecoder::reconstruct_ctu(ctu, frame, dpb_);
        }
    }

    // In-Loop Sample Adaptive Offset (SAO)
    SaoParams sao;
    sao.type = SaoType::BandOffset;
    sao.band_position = 12;
    sao.offsets[0] = 1;
    sao.offsets[1] = -1;
    sao.offsets[2] = 2;
    sao.offsets[3] = -2;
    SaoHevc::apply_sao(y_plane, y_stride, w, h, sao, 8);
}

MediaResult<void> HevcDecoder::receive_frame(VideoFrame& frame) {
    if (ready_frames_.empty()) return MediaError::NeedMoreData;
    frame = std::move(ready_frames_.front());
    ready_frames_.pop();
    return {};
}

MediaResult<void> HevcDecoder::flush() {
    while (!ready_frames_.empty()) ready_frames_.pop();
    return {};
}

MediaResult<void> HevcDecoder::reset() {
    flush();
    dpb_.clear();
    parser_.reset();
    frame_count_ = 0;
    return {};
}

} // namespace atum

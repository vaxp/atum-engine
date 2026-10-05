#include "atum/codec/video/vp9/vp9_decoder.hpp"
#include <algorithm>
#include <cstring>
#include "atum/entropy/vp9_bool_decoder.hpp"
#include "atum/entropy/vp9_probability_tables.hpp"
#include "atum/codec/video/vp9/vp9_block_decoder.hpp"
#include "atum/transform/vp9_transform.hpp"

namespace atum {

Vp9Decoder::Vp9Decoder() {
    params_.codec_id = CodecId::VP9;
    params_.media_type = MediaType::Video;
}

MediaResult<void> Vp9Decoder::init(const CodecParameters& params) {
    params_ = params;
    params_.codec_id = CodecId::VP9;
    params_.media_type = MediaType::Video;
    frame_count_ = 0;
    dpb_.clear();
    return {};
}

MediaResult<void> Vp9Decoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    auto subframes_res = parser_.parse_packet(packet.as_span());
    if (!subframes_res) return subframes_res.error();

    for (const auto& sf : *subframes_res) {
        uint32_t w = sf.header.has_value() ? sf.header->width : (parser_.width() > 0 ? parser_.width() : params_.width);
        uint32_t h = sf.header.has_value() ? sf.header->height : (parser_.height() > 0 ? parser_.height() : params_.height);

        if (w > 0 && h > 0) {
            params_.width = w;
            params_.height = h;

            VideoFrame frame = VideoFrame::allocate(w, h, PixelFormat::YUV420P);
            frame.set_pts(packet.pts());
            frame.set_dts(packet.dts());
            frame.set_duration(packet.duration());

            bool is_key = false;
            if (sf.header.has_value() && sf.header->is_keyframe()) {
                is_key = true;
            } else if (packet.is_keyframe()) {
                is_key = true;
            }
            frame.set_keyframe(is_key);

            if (sf.header.has_value()) {
                frame.set_color_range(sf.header->color_range_full ? ColorRange::Full : ColorRange::Studio);
                switch (sf.header->color_space) {
                    case Vp9ColorSpace::BT709:
                        frame.set_color_space(ColorSpace::BT709);
                        break;
                    case Vp9ColorSpace::BT601:
                    case Vp9ColorSpace::SMPTE170:
                    case Vp9ColorSpace::SMPTE240:
                        frame.set_color_space(ColorSpace::BT601);
                        break;
                    case Vp9ColorSpace::BT2020:
                        frame.set_color_space(ColorSpace::BT2020);
                        break;
                    default:
                        break;
                }
            }

            const Vp9UncompressedHeader* hdr = sf.header.has_value() ? &*sf.header : nullptr;
            reconstruct_frame(frame, hdr, sf.data);

            int32_t poc = static_cast<int32_t>(frame_count_);
            uint32_t frame_num = frame_count_;

            if (is_key) {
                dpb_.clear();
            }
            dpb_.insert(frame.clone(), poc, frame_num, is_key);

            ready_frames_.push(std::move(frame));
            frame_count_++;
        }
    }

    return {};
}

void Vp9Decoder::reconstruct_frame(VideoFrame& frame, const Vp9UncompressedHeader* header, std::span<const uint8_t> payload) {
    uint32_t w = frame.width();
    uint32_t h = frame.height();

    bool is_key = !header || header->is_keyframe() || dpb_.empty();
    auto ctx = Vp9ProbabilityTables::get_default_context();

    Vp9BoolDecoder bool_dec;
    if (!payload.empty()) {
        (void)bool_dec.init(payload);
    }

    constexpr uint32_t sb_size = 64;
    uint32_t sb_cols = (w + sb_size - 1) / sb_size;
    uint32_t sb_rows = (h + sb_size - 1) / sb_size;

    for (uint32_t sby = 0; sby < sb_rows; ++sby) {
        for (uint32_t sbx = 0; sbx < sb_cols; ++sbx) {
            Vp9Superblock sb{};
            sb.x = sbx * sb_size;
            sb.y = sby * sb_size;
            sb.size = sb_size;
            sb.bsize_level = 3;

            if (!payload.empty() && !bool_dec.is_exhausted()) {
                (void)Vp9BlockDecoder::decode_superblock_tree(bool_dec, ctx, is_key, sb);
            } else {
                sb.partition = Vp9Partition::None;
                Vp9Block blk{};
                blk.x = sb.x;
                blk.y = sb.y;
                blk.width = sb_size;
                blk.height = sb_size;
                blk.is_intra = is_key;
                blk.mode = is_key ? Vp9PredMode::Dc : Vp9PredMode::ZeroMv;
                blk.ref_frame[0] = is_key ? Vp9Ref::Intra : Vp9Ref::Last;
                sb.leaf_blocks.push_back(blk);
            }

            Vp9BlockDecoder::reconstruct_superblock(sb, frame, dpb_);
        }
    }
}

MediaResult<void> Vp9Decoder::receive_frame(VideoFrame& frame) {
    if (ready_frames_.empty()) return MediaError::NeedMoreData;
    frame = std::move(ready_frames_.front());
    ready_frames_.pop();
    return {};
}

MediaResult<void> Vp9Decoder::flush() {
    while (!ready_frames_.empty()) ready_frames_.pop();
    return {};
}

MediaResult<void> Vp9Decoder::reset() {
    flush();
    dpb_.clear();
    parser_.reset();
    frame_count_ = 0;
    return {};
}

} // namespace atum

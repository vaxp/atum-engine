#include "atum/codec/video/av1/av1_decoder.hpp"
#include <algorithm>
#include <cstring>
#include "atum/entropy/av1_symbol_decoder.hpp"
#include "atum/entropy/av1_cdf_tables.hpp"
#include "atum/codec/video/av1/av1_partition.hpp"
#include "atum/inloop/cdef_av1.hpp"
#include "atum/inloop/av1_restoration.hpp"
#include "atum/inloop/av1_film_grain.hpp"
#include "atum/parallel/av1_tile_grid.hpp"

namespace atum {

Av1Decoder::Av1Decoder() {
    params_.codec_id = CodecId::AV1;
    params_.media_type = MediaType::Video;
}

MediaResult<void> Av1Decoder::init(const CodecParameters& params) {
    params_ = params;
    params_.codec_id = CodecId::AV1;
    params_.media_type = MediaType::Video;
    frame_count_ = 0;
    dpb_.clear();

    if (!params.extradata.empty()) {
        auto res = parser_.parse_extradata(params.extradata);
        if (res) {
            if (parser_.width() > 0) params_.width = parser_.width();
            if (parser_.height() > 0) params_.height = parser_.height();
        }
    }
    return {};
}

MediaResult<void> Av1Decoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    (void)parser_.parse_stream(packet.as_span());

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
        if (parser_.last_frame_header().has_value() && parser_.last_frame_header()->is_keyframe()) {
            is_key = true;
        } else if (packet.is_keyframe()) {
            is_key = true;
        }
        frame.set_keyframe(is_key);

        if (parser_.sequence_header().has_value()) {
            const auto& col = parser_.sequence_header()->color_config;
            frame.set_color_range(col.color_range_full ? ColorRange::Full : ColorRange::Studio);
            if (col.color_description_present_flag) {
                if (col.matrix_coefficients == 1) frame.set_color_space(ColorSpace::BT709);
                else if (col.matrix_coefficients == 6 || col.matrix_coefficients == 5) frame.set_color_space(ColorSpace::BT601);
                else if (col.matrix_coefficients == 9) frame.set_color_space(ColorSpace::BT2020);
            }
        }

        const Av1FrameHeader* hdr = parser_.last_frame_header().has_value() ?
                                    &*parser_.last_frame_header() : nullptr;
        reconstruct_frame(frame, hdr, packet.as_span());

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

void Av1Decoder::reconstruct_frame(VideoFrame& frame, const Av1FrameHeader* header, std::span<const uint8_t> payload) {
    uint32_t w = frame.width();
    uint32_t h = frame.height();

    bool is_key = !header || header->is_keyframe() || dpb_.empty();
    auto ctx = Av1CdfTables::get_default_context();

    Av1SymbolDecoder sym_dec;
    if (!payload.empty()) {
        (void)sym_dec.init(payload);
    }

    constexpr uint32_t sb_size = 64;
    uint32_t sb_cols = (w + sb_size - 1) / sb_size;
    uint32_t sb_rows = (h + sb_size - 1) / sb_size;

    if (thread_count_ != 1 && (tile_cols_ > 1 || tile_rows_ > 1) && (sb_cols > 1 || sb_rows > 1)) {
        Av1TileGridDecoder tile_dec;
        (void)tile_dec.decode_tile_grid(frame, header, payload, dpb_, tile_cols_, tile_rows_);
        return;
    }

    for (uint32_t sby = 0; sby < sb_rows; ++sby) {
        for (uint32_t sbx = 0; sbx < sb_cols; ++sbx) {
            Av1Superblock sb{};
            sb.x = sbx * sb_size;
            sb.y = sby * sb_size;
            sb.size = sb_size;

            if (!payload.empty()) {
                (void)Av1PartitionDecoder::decode_superblock_tree(sym_dec, ctx, is_key, sb);
            } else {
                sb.partition = Av1Partition::None;
                Av1Block blk{};
                blk.x = sb.x;
                blk.y = sb.y;
                blk.width = sb_size;
                blk.height = sb_size;
                blk.is_intra = is_key;
                sb.leaf_blocks.push_back(blk);
            }

            Av1PartitionDecoder::reconstruct_superblock(sb, frame, dpb_);
        }
    }

    // 1. Apply AV1 CDEF In-Loop Filter
    uint8_t* y_plane = frame.plane_mut(0);
    size_t y_stride = frame.stride(0);
    for (uint32_t by = 0; by < (h + 7) / 8; ++by) {
        for (uint32_t bx = 0; bx < (w + 7) / 8; ++bx) {
            uint32_t px = bx * 8;
            uint32_t py = by * 8;
            if (px + 8 <= w && py + 8 <= h) {
                uint8_t* blk_ptr = y_plane + py * y_stride + px;
                uint8_t tmp_out[64];
                CdefAv1::filter_8x8(blk_ptr, y_stride, tmp_out, 8, 4, 1, 5);
                for (uint32_t y = 0; y < 8; ++y) {
                    for (uint32_t x = 0; x < 8; ++x) {
                        blk_ptr[y * y_stride + x] = tmp_out[y * 8 + x];
                    }
                }
            }
        }
    }

    // 2. Apply Dual Loop Restoration (Wiener Filter)
    Av1LoopRestoration::apply_frame_restoration(frame, Av1RestoreType::Wiener);

    // 3. Film Grain Synthesis
    Av1FilmGrainParams grain_params{};
    grain_params.apply_grain = true;
    grain_params.grain_seed = 0x5A5A;
    grain_params.scaling_shift = 8;
    grain_params.num_y_points = 2;
    grain_params.scaling_points_y[0] = { 0, 16 };
    grain_params.scaling_points_y[1] = { 255, 32 };
    Av1FilmGrainSynthesizer::apply_grain_to_frame(frame, grain_params);
}

MediaResult<void> Av1Decoder::receive_frame(VideoFrame& frame) {
    if (ready_frames_.empty()) return MediaError::NeedMoreData;
    frame = std::move(ready_frames_.front());
    ready_frames_.pop();
    return {};
}

MediaResult<void> Av1Decoder::flush() {
    while (!ready_frames_.empty()) ready_frames_.pop();
    return {};
}

MediaResult<void> Av1Decoder::reset() {
    flush();
    dpb_.clear();
    parser_.reset();
    frame_count_ = 0;
    return {};
}

} // namespace atum

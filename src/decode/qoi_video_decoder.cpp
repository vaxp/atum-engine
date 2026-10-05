#include "atum/decode/qoi_video_decoder.hpp"
#include <cstring>
#include <array>

namespace atum {

namespace {

struct RgbaPixel {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};
};

inline size_t qoi_hash(RgbaPixel px) noexcept {
    return (static_cast<size_t>(px.r) * 3 +
            static_cast<size_t>(px.g) * 5 +
            static_cast<size_t>(px.b) * 7 +
            static_cast<size_t>(px.a) * 11) % 64;
}

constexpr uint8_t QOI_OP_INDEX = 0x00;
constexpr uint8_t QOI_OP_DIFF  = 0x40;
constexpr uint8_t QOI_OP_LUMA  = 0x80;
constexpr uint8_t QOI_OP_RUN   = 0xc0;
constexpr uint8_t QOI_OP_RGB   = 0xfe;
constexpr uint8_t QOI_OP_RGBA  = 0xff;
constexpr uint8_t QOI_MASK_2   = 0xc0;

} // anonymous namespace

MediaResult<VideoFrame> QoiVideoDecoder::decode_frame_bytes(std::span<const uint8_t> qoi_bytes) {
    if (qoi_bytes.size() < 14 + 8) { // Header (14) + End marker (8)
        return MediaError::InvalidData;
    }

    const uint8_t* bytes = qoi_bytes.data();
    size_t size = qoi_bytes.size();

    // Verify magic 'qoif'
    if (bytes[0] != 'q' || bytes[1] != 'o' || bytes[2] != 'i' || bytes[3] != 'f') {
        return MediaError::InvalidData;
    }

    uint32_t width = (static_cast<uint32_t>(bytes[4]) << 24) |
                     (static_cast<uint32_t>(bytes[5]) << 16) |
                     (static_cast<uint32_t>(bytes[6]) << 8)  |
                     static_cast<uint32_t>(bytes[7]);

    uint32_t height = (static_cast<uint32_t>(bytes[8]) << 24) |
                      (static_cast<uint32_t>(bytes[9]) << 16) |
                      (static_cast<uint32_t>(bytes[10]) << 8) |
                      static_cast<uint32_t>(bytes[11]);

    uint8_t channels = bytes[12];
    if (width == 0 || height == 0 || (channels != 3 && channels != 4)) {
        return MediaError::InvalidData;
    }

    auto frame = VideoFrame::allocate(width, height, PixelFormat::RGBA32);
    frame.set_color_range(ColorRange::Full);
    frame.set_color_space(ColorSpace::BT709);
    frame.set_keyframe(true);

    uint8_t* dst = frame.plane_mut(0);
    size_t dst_stride = frame.stride(0);

    std::array<RgbaPixel, 64> index{};
    RgbaPixel px{0, 0, 0, 255};
    int run = 0;

    size_t p = 14;
    size_t chunks_len = size - 8;
    size_t total_pixels = static_cast<size_t>(width) * height;

    for (size_t px_idx = 0; px_idx < total_pixels; ++px_idx) {
        if (run > 0) {
            run--;
        } else if (p < chunks_len) {
            uint8_t b1 = bytes[p++];

            if (b1 == QOI_OP_RGB) {
                px.r = bytes[p++];
                px.g = bytes[p++];
                px.b = bytes[p++];
            } else if (b1 == QOI_OP_RGBA) {
                px.r = bytes[p++];
                px.g = bytes[p++];
                px.b = bytes[p++];
                px.a = bytes[p++];
            } else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
                px = index[b1];
            } else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
                px.r += ((b1 >> 4) & 0x03) - 2;
                px.g += ((b1 >> 2) & 0x03) - 2;
                px.b += (b1 & 0x03) - 2;
            } else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
                uint8_t b2 = bytes[p++];
                int vg = (b1 & 0x3f) - 32;
                px.r += static_cast<uint8_t>(vg - 8 + ((b2 >> 4) & 0x0f));
                px.g += static_cast<uint8_t>(vg);
                px.b += static_cast<uint8_t>(vg - 8 + (b2 & 0x0f));
            } else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
                run = (b1 & 0x3f);
            }

            index[qoi_hash(px)] = px;
        }

        uint32_t y = static_cast<uint32_t>(px_idx / width);
        uint32_t x = static_cast<uint32_t>(px_idx % width);
        uint8_t* row = dst + y * dst_stride;
        row[x * 4 + 0] = px.r;
        row[x * 4 + 1] = px.g;
        row[x * 4 + 2] = px.b;
        row[x * 4 + 3] = px.a;
    }

    return frame;
}

MediaResult<void> QoiVideoDecoder::init(const CodecParameters& params) {
    params_ = params;
    packet_queue_.clear();
    return MediaResult<void>::ok();
}

MediaResult<void> QoiVideoDecoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::InvalidData;
    packet_queue_.push_back(packet);
    return MediaResult<void>::ok();
}

MediaResult<void> QoiVideoDecoder::receive_frame(VideoFrame& frame) {
    if (packet_queue_.empty()) {
        return MediaError::NeedMoreData;
    }

    Packet pkt = std::move(packet_queue_.front());
    packet_queue_.pop_front();

    auto res = decode_frame_bytes(pkt.as_span());
    if (!res.is_ok()) return res.error();

    frame = std::move(*res);
    frame.set_pts(pkt.pts());
    frame.set_dts(pkt.dts());
    frame.set_duration(pkt.duration());
    frame.set_keyframe(pkt.is_keyframe());

    return MediaResult<void>::ok();
}

MediaResult<void> QoiVideoDecoder::flush() {
    packet_queue_.clear();
    return MediaResult<void>::ok();
}

MediaResult<void> QoiVideoDecoder::reset() {
    packet_queue_.clear();
    return MediaResult<void>::ok();
}

} // namespace atum

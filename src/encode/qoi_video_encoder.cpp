#include "atum/encode/qoi_video_encoder.hpp"
#include "atum/surface/color_convert.hpp"
#include <cstring>
#include <array>

namespace atum {

namespace {

struct RgbaPixel {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    bool operator==(const RgbaPixel& o) const noexcept {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
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

} // anonymous namespace

MediaResult<std::vector<uint8_t>> QoiVideoEncoder::encode_frame_bytes(const VideoFrame& frame) {
    if (!frame.is_valid()) return MediaError::InvalidData;

    VideoFrame rgba_frame;
    const VideoFrame* src = &frame;
    if (frame.pixel_format() != PixelFormat::RGBA32) {
        rgba_frame = ColorConverter::convert(frame, PixelFormat::RGBA32);
        src = &rgba_frame;
    }

    uint32_t width = src->width();
    uint32_t height = src->height();
    size_t total_pixels = static_cast<size_t>(width) * height;

    std::vector<uint8_t> out;
    out.reserve(14 + total_pixels * 4 + 8);

    // QOI Header
    out.push_back('q');
    out.push_back('o');
    out.push_back('i');
    out.push_back('f');
    out.push_back(static_cast<uint8_t>((width >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((width >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((width >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(width & 0xff));
    out.push_back(static_cast<uint8_t>((height >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((height >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((height >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(height & 0xff));
    out.push_back(4); // RGBA
    out.push_back(0); // sRGB

    std::array<RgbaPixel, 64> index{};
    RgbaPixel prev_px{0, 0, 0, 255};
    int run = 0;

    const uint8_t* src_ptr = src->plane(0);
    size_t stride = src->stride(0);

    for (size_t px_idx = 0; px_idx < total_pixels; ++px_idx) {
        uint32_t y = static_cast<uint32_t>(px_idx / width);
        uint32_t x = static_cast<uint32_t>(px_idx % width);
        const uint8_t* p = src_ptr + y * stride + x * 4;

        RgbaPixel px{p[0], p[1], p[2], p[3]};

        if (px == prev_px) {
            run++;
            if (run == 62) {
                out.push_back(QOI_OP_RUN | static_cast<uint8_t>(run - 1));
                run = 0;
            }
        } else {
            if (run > 0) {
                out.push_back(QOI_OP_RUN | static_cast<uint8_t>(run - 1));
                run = 0;
            }

            size_t idx = qoi_hash(px);
            if (index[idx] == px) {
                out.push_back(QOI_OP_INDEX | static_cast<uint8_t>(idx));
            } else {
                index[idx] = px;

                if (px.a == prev_px.a) {
                    int8_t vr = static_cast<int8_t>(px.r - prev_px.r);
                    int8_t vg = static_cast<int8_t>(px.g - prev_px.g);
                    int8_t vb = static_cast<int8_t>(px.b - prev_px.b);

                    int8_t vg_r = static_cast<int8_t>(vr - vg);
                    int8_t vg_b = static_cast<int8_t>(vb - vg);

                    if (vr >= -2 && vr <= 1 && vg >= -2 && vg <= 1 && vb >= -2 && vb <= 1) {
                        out.push_back(QOI_OP_DIFF |
                            static_cast<uint8_t>((vr + 2) << 4) |
                            static_cast<uint8_t>((vg + 2) << 2) |
                            static_cast<uint8_t>(vb + 2));
                    } else if (vg_r >= -8 && vg_r <= 7 && vg >= -32 && vg <= 31 && vg_b >= -8 && vg_b <= 7) {
                        out.push_back(QOI_OP_LUMA | static_cast<uint8_t>(vg + 32));
                        out.push_back(static_cast<uint8_t>((vg_r + 8) << 4) | static_cast<uint8_t>(vg_b + 8));
                    } else {
                        out.push_back(QOI_OP_RGB);
                        out.push_back(px.r);
                        out.push_back(px.g);
                        out.push_back(px.b);
                    }
                } else {
                    out.push_back(QOI_OP_RGBA);
                    out.push_back(px.r);
                    out.push_back(px.g);
                    out.push_back(px.b);
                    out.push_back(px.a);
                }
            }
            prev_px = px;
        }
    }

    if (run > 0) {
        out.push_back(QOI_OP_RUN | static_cast<uint8_t>(run - 1));
    }

    // 8-byte end marker: 7x 0x00, 1x 0x01
    for (int i = 0; i < 7; ++i) out.push_back(0x00);
    out.push_back(0x01);

    return out;
}

MediaResult<void> QoiVideoEncoder::init(const CodecParameters& params) {
    params_ = params;
    ready_packets_.clear();
    return MediaResult<void>::ok();
}

MediaResult<void> QoiVideoEncoder::send_frame(const VideoFrame& frame) {
    if (!frame.is_valid()) return MediaError::InvalidData;

    auto res = encode_frame_bytes(frame);
    if (!res.is_ok()) return res.error();

    Packet pkt = Packet::from_bytes(*res);
    pkt.set_pts(frame.pts());
    pkt.set_dts(frame.dts());
    pkt.set_duration(frame.duration());
    pkt.set_keyframe(true); // Every QOI frame is an intra-keyframe

    ready_packets_.push_back(std::move(pkt));
    return MediaResult<void>::ok();
}

MediaResult<void> QoiVideoEncoder::receive_packet(Packet& packet) {
    if (ready_packets_.empty()) {
        return MediaError::NeedMoreData;
    }
    packet = std::move(ready_packets_.front());
    ready_packets_.pop_front();
    return MediaResult<void>::ok();
}

MediaResult<void> QoiVideoEncoder::flush() {
    return MediaResult<void>::ok();
}

} // namespace atum

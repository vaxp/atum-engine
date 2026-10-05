#pragma once

#include <queue>
#include <memory>
#include <span>
#include <vector>
#include "atum/decode/decoder.hpp"
#include "atum/codec/video/vp9/vp9_parser.hpp"
#include "atum/predict/dpb.hpp"

namespace atum {

/// Sovereign VP9 Video Decoder.
/// Integrates 128-state Boolean entropy decoder, VP9 IDCT/IADST transforms,
/// subpixel motion compensation, and multi-reference frame buffer.
class Vp9Decoder : public VideoDecoder {
public:
    Vp9Decoder();
    ~Vp9Decoder() override = default;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_packet(const Packet& packet) override;
    MediaResult<void> receive_frame(VideoFrame& frame) override;
    MediaResult<void> flush() override;
    MediaResult<void> reset() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override {
        return params_;
    }

    [[nodiscard]] const Vp9Parser& parser() const noexcept {
        return parser_;
    }

    [[nodiscard]] const DecodedPictureBuffer& dpb() const noexcept {
        return dpb_;
    }

private:
    CodecParameters params_;
    Vp9Parser parser_;
    std::queue<VideoFrame> ready_frames_;
    DecodedPictureBuffer dpb_{8};
    uint32_t frame_count_{0};

    void reconstruct_frame(VideoFrame& frame, const Vp9UncompressedHeader* header, std::span<const uint8_t> payload);
};

} // namespace atum


#pragma once

#include <queue>
#include <memory>
#include <span>
#include <vector>
#include "atum/decode/decoder.hpp"
#include "atum/codec/video/hevc/hevc_parser.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/hwaccel/hwaccel.hpp"

namespace atum {

/// Sovereign HEVC / H.265 Video Decoder.
/// Integrates CABAC entropy decoding, HEVC DCT/DST transforms,
/// 35-mode angular intra prediction, DPB reference management, and Sample Adaptive Offset (SAO).
class HevcDecoder : public VideoDecoder {
public:
    HevcDecoder();
    ~HevcDecoder() override = default;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_packet(const Packet& packet) override;
    MediaResult<void> receive_frame(VideoFrame& frame) override;
    MediaResult<void> flush() override;
    MediaResult<void> reset() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override {
        return params_;
    }

    [[nodiscard]] const HevcParser& parser() const noexcept {
        return parser_;
    }

    [[nodiscard]] const DecodedPictureBuffer& dpb() const noexcept {
        return dpb_;
    }

    [[nodiscard]] HwDevice* hw_device() const noexcept {
        return hw_device_.get();
    }

    [[nodiscard]] bool is_hw_supported() const noexcept {
        return is_hw_supported_;
    }

    /// Configures the number of worker threads for WPP (0 = default pool, 1 = single-threaded).
    void set_thread_count(size_t threads) noexcept { thread_count_ = threads; }
    [[nodiscard]] size_t thread_count() const noexcept { return thread_count_; }

private:
    CodecParameters params_;
    HevcParser parser_;
    std::queue<VideoFrame> ready_frames_;
    DecodedPictureBuffer dpb_{16};
    std::unique_ptr<HwDevice> hw_device_;
    bool is_hw_supported_{false};
    bool is_hvcc_{false};
    uint8_t nal_length_size_{4};
    uint32_t frame_count_{0};
    size_t thread_count_{0};

    void reconstruct_frame(VideoFrame& frame, const HevcSliceHeader* slice_hdr, std::span<const uint8_t> payload);
};

} // namespace atum


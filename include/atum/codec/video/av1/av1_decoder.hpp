#pragma once

#include <queue>
#include <memory>
#include <span>
#include <vector>
#include "atum/decode/decoder.hpp"
#include "atum/codec/video/av1/av1_parser.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/hwaccel/hwaccel.hpp"

namespace atum {

/// Sovereign Hardware-Accelerated AV1 Video Decoder.
class Av1Decoder : public VideoDecoder {
public:
    Av1Decoder();
    ~Av1Decoder() override;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_packet(const Packet& packet) override;
    MediaResult<void> receive_frame(VideoFrame& frame) override;
    MediaResult<void> flush() override;
    MediaResult<void> reset() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override {
        return params_;
    }

    [[nodiscard]] const Av1Parser& parser() const noexcept {
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

    void set_thread_count(size_t threads) noexcept { thread_count_ = threads; }
    [[nodiscard]] size_t thread_count() const noexcept { return thread_count_; }

private:
    CodecParameters params_;
    Av1Parser parser_;
    std::queue<VideoFrame> ready_frames_;
    DecodedPictureBuffer dpb_{8};
    std::unique_ptr<HwDevice> hw_device_;
    bool is_hw_supported_{false};
    uint32_t frame_count_{0};
    size_t thread_count_{0};

    // Hardware Acceleration Pipeline State
    uint32_t hw_config_id_{0};
    uint32_t hw_context_id_{0};
    std::vector<uint32_t> hw_surfaces_;
    uint32_t current_surface_idx_{0};
    bool hw_pipeline_initialized_{false};

    void init_hardware_pipeline(uint32_t width, uint32_t height);
    void release_hardware_pipeline();
};

} // namespace atum

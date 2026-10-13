#pragma once

#include <queue>
#include <memory>
#include <span>
#include <vector>
#include "atum/decode/decoder.hpp"
#include "atum/codec/video/h264/h264_parser.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/hwaccel/hwaccel.hpp"
#include "atum/resilience/resilient_nal_parser.hpp"
#include "atum/resilience/error_concealer.hpp"

namespace atum {

/// Sovereign H.264 / AVC Video Decoder.
/// Integrates CABAC entropy engine, SIMD integer IDCT transforms,
/// spatial/temporal prediction, DPB reference management, adaptive in-loop deblocking,
/// and autonomous resilience & error concealment.
class H264Decoder : public VideoDecoder {
public:
    H264Decoder();
    ~H264Decoder() override;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> send_packet(const Packet& packet) override;
    MediaResult<void> receive_frame(VideoFrame& frame) override;
    MediaResult<void> flush() override;
    MediaResult<void> reset() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override {
        return params_;
    }

    [[nodiscard]] const H264Parser& parser() const noexcept {
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

    void set_error_concealment_enabled(bool enabled) noexcept {
        error_concealment_enabled_ = enabled;
    }

    [[nodiscard]] bool is_error_concealment_enabled() const noexcept {
        return error_concealment_enabled_;
    }

    [[nodiscard]] ErrorConcealer& error_concealer() noexcept {
        return error_concealer_;
    }

    [[nodiscard]] const ErrorConcealer& error_concealer() const noexcept {
        return error_concealer_;
    }

    [[nodiscard]] ResilientNalParser& resilient_parser() noexcept {
        return resilient_parser_;
    }

    [[nodiscard]] const ResilientNalParser& resilient_parser() const noexcept {
        return resilient_parser_;
    }

private:
    CodecParameters params_;
    H264Parser parser_;
    std::queue<VideoFrame> ready_frames_;
    DecodedPictureBuffer dpb_{16};
    std::unique_ptr<HwDevice> hw_device_;
    bool is_hw_supported_{false};
    bool is_avcc_{false};
    uint8_t nal_length_size_{4};
    uint32_t frame_count_{0};

    bool error_concealment_enabled_{true};
    ErrorConcealer error_concealer_;
    ResilientNalParser resilient_parser_{ResilientNalParser::CodecStandard::H264};
    ErrorConcealmentMask damage_mask_;

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


#pragma once

#include <memory>
#include <queue>
#include <vector>
#include "atum/encode/encoder.hpp"
#include "atum/hwaccel/vaapi_device.hpp"
#include "atum/hwaccel/hw_surface_pool.hpp"

namespace atum {

/// Configuration parameters for VA-API Hardware Video Encoding.
struct HwEncoderConfig {
    uint32_t gop_size{30};            ///< Group of Pictures (GOP) size / Keyframe distance
    uint32_t idr_interval{30};       ///< IDR keyframe interval
    uint32_t bitrate_bps{4000000};   ///< Target bitrate in bits per second (e.g. 4 Mbps)
    uint32_t qp{26};                 ///< Quantization Parameter for Constant QP (CQP) mode
    uint32_t fps{30};                ///< Frame rate numerator (e.g. 30 fps)
    bool use_cqp{false};             ///< True for Constant QP, False for Variable/Constant Bitrate
};

/// Sovereign VA-API Hardware Video Encoder for Intel/AMD/SoC GPUs.
/// Performs real-time GPU hardware video compression with zero CPU pixel processing overhead.
class VaapiEncoder : public VideoEncoder {
public:
    VaapiEncoder();
    ~VaapiEncoder() override;

    MediaResult<void> init(const CodecParameters& params) override;
    MediaResult<void> init(const CodecParameters& params, const HwEncoderConfig& config);

    MediaResult<void> send_frame(const VideoFrame& frame) override;
    MediaResult<void> receive_packet(Packet& packet) override;
    MediaResult<void> flush() override;

    [[nodiscard]] const CodecParameters& parameters() const noexcept override {
        return params_;
    }

    [[nodiscard]] const HwEncoderConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] bool is_hardware_active() const noexcept {
        return hw_initialized_;
    }

    [[nodiscard]] VaapiDevice* hw_device() const noexcept {
        return device_.get();
    }

private:
    CodecParameters params_;
    HwEncoderConfig config_;
    std::unique_ptr<VaapiDevice> device_;
    HwSurfacePool surface_pool_;
    uint32_t hw_config_id_{0};
    uint32_t hw_context_id_{0};
    uint32_t coded_buffer_id_{0};
    uint32_t frame_index_{0};
    bool hw_initialized_{false};
    std::queue<Packet> ready_packets_;

    MediaResult<void> encode_h264_frame(uint32_t input_surface, uint32_t recon_surface, uint32_t ref_surface, bool is_idr, int64_t pts, int64_t dts);
    void release_pipeline() noexcept;
};

} // namespace atum

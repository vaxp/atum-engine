#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/core/packet.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/codec/video/h264/h264_types.hpp"
#include "atum/codec/video/h264/h264_sps.hpp"
#include "atum/codec/video/h264/h264_pps.hpp"
#include "atum/codec/video/h264/h264_rate_control.hpp"
#include "atum/codec/video/motion_estimate.hpp"

namespace atum {

struct H264EncoderConfig {
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t fps{30};
    uint32_t gop_size{30};              // Keyframe interval in frames
    H264Profile profile{H264Profile::High};
    bool use_cabac{false};              // true = CABAC, false = CAVLC
    RateControlConfig rc{};
    MotionSearchMethod me_method{MotionSearchMethod::Hexagonal};
    int32_t me_range{16};
};

class H264Encoder {
public:
    explicit H264Encoder(const H264EncoderConfig& config);
    ~H264Encoder() = default;

    /// Encodes a raw VideoFrame (YUV420P) into an H.264 Annex-B Packet.
    /// `pts`: Presentation timestamp in microseconds.
    /// `force_idr`: Forces an IDR keyframe with SPS/PPS emission.
    Packet encode_frame(const VideoFrame& frame, int64_t pts, bool force_idr = false);

    /// Flushes any pending delayed frames (if lookahead enabled).
    std::vector<Packet> flush();

    /// Returns the active SPS.
    [[nodiscard]] const H264Sps& sps() const noexcept { return sps_; }

    /// Returns the active PPS.
    [[nodiscard]] const H264Pps& pps() const noexcept { return pps_; }

    /// Returns the rate control engine for telemetry.
    [[nodiscard]] const H264RateControl& rate_control() const noexcept { return rc_; }

    /// Returns encoder configuration.
    [[nodiscard]] const H264EncoderConfig& config() const noexcept { return config_; }

    /// Returns the reconstructed working frame for inspection/telemetry.
    [[nodiscard]] const VideoFrame& reconstructed_frame() const noexcept { return recon_frame_; }

private:
    void init_parameter_sets();

    H264EncoderConfig config_;
    H264Sps sps_{};
    H264Pps pps_{};
    H264RateControl rc_;

    std::vector<uint8_t> sps_annexb_;
    std::vector<uint8_t> pps_annexb_;

    // Reference Picture in Decoded Picture Buffer (DPB)
    VideoFrame ref_frame_{};
    bool has_ref_{false};

    // Reconstructed working frame for the current picture
    VideoFrame recon_frame_{};

    uint32_t frame_num_{0};
    uint32_t idr_pic_id_{0};
    uint32_t mb_width_{0};
    uint32_t mb_height_{0};
};

} // namespace atum

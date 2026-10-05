#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include "atum/core/media_types.hpp"
#include "atum/codec/video/h264/h264_encoder.hpp"
#include "atum/container/container.hpp"
#include "atum/io/file_stream.hpp"

namespace nisaba {
    class Canvas;
    class Pixmap;
}

namespace atum {

enum class OutputContainer {
    MP4,
    MKV,
    RawAnnexB
};

struct CanvasRecorderConfig {
    uint32_t width{1280};
    uint32_t height{720};
    uint32_t fps{30};
    uint32_t gop_size{30};
    OutputContainer container{OutputContainer::MP4};
    int32_t qp{23};
    bool use_cabac{false};
    MotionSearchMethod me_method{MotionSearchMethod::Hexagonal};
};

class CanvasRecorder {
public:
    CanvasRecorder(std::string_view output_filepath, const CanvasRecorderConfig& config);
    ~CanvasRecorder();

    /// Captures a packed 32-bit RGBA buffer and encodes it as the next frame.
    void capture_rgba(const uint8_t* rgba_pixels, size_t stride_bytes, int64_t timestamp_us = -1);

    /// Captures the current surface of a Nisaba Canvas.
    void capture_canvas(nisaba::Canvas& canvas, int64_t timestamp_us = -1);

    /// Finalizes the recording and writes file trailer / index.
    void finish();

    [[nodiscard]] size_t frames_captured() const noexcept { return frame_count_; }
    [[nodiscard]] bool is_active() const noexcept { return active_; }
    [[nodiscard]] const H264EncoderConfig& encoder_config() const noexcept { return encoder_->config(); }

private:
    std::string filepath_;
    CanvasRecorderConfig config_;
    std::unique_ptr<H264Encoder> encoder_;
    std::unique_ptr<Muxer> muxer_;
    std::shared_ptr<FileWriter> file_writer_;

    VideoFrame yuv_frame_{};
    size_t frame_count_{0};
    int32_t stream_index_{-1};
    bool active_{false};
};

} // namespace atum

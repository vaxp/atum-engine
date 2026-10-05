#include "atum/recorder/canvas_recorder.hpp"
#if __has_include("nisaba/canvas/canvas.hpp")
#include "nisaba/canvas/canvas.hpp"
#endif
#include "atum/container/isobmff_muxer.hpp"
#include "atum/container/mkv/mkv_muxer.hpp"
#include "atum/surface/color_convert.hpp"
#include <stdexcept>

namespace atum {

CanvasRecorder::CanvasRecorder(std::string_view output_filepath, const CanvasRecorderConfig& config)
    : filepath_(output_filepath), config_(config) {
    auto file_res = FileWriter::open(filepath_);
    if (!file_res) {
        throw std::runtime_error("CanvasRecorder failed to open output file: " + std::string(filepath_));
    }
    file_writer_ = std::make_shared<FileWriter>(std::move(*file_res));

    // Configure H.264 video encoder
    H264EncoderConfig enc_cfg;
    enc_cfg.width = config_.width;
    enc_cfg.height = config_.height;
    enc_cfg.fps = config_.fps;
    enc_cfg.gop_size = config_.gop_size;
    enc_cfg.profile = H264Profile::High;
    enc_cfg.use_cabac = config_.use_cabac;
    enc_cfg.me_method = config_.me_method;
    enc_cfg.rc.mode = RateControlMode::CQP;
    enc_cfg.rc.base_qp = config_.qp;
    enc_cfg.rc.fps = config_.fps;

    encoder_ = std::make_unique<H264Encoder>(enc_cfg);

    // Setup container multiplexer
    if (config_.container == OutputContainer::MP4) {
        muxer_ = std::make_unique<IsoBmffMuxer>();
    } else if (config_.container == OutputContainer::MKV) {
        muxer_ = std::make_unique<MkvMuxer>();
    } else {
        muxer_ = nullptr; // Raw Annex-B elementary stream
    }

    if (muxer_) {
        muxer_->open(file_writer_);
        CodecParameters params{};
        params.codec_id = CodecId::H264;
        params.width = config_.width;
        params.height = config_.height;
        params.pixel_format = PixelFormat::YUV420P;
        auto st_res = muxer_->add_stream(params, Rational{1, static_cast<int32_t>(config_.fps)});
        if (st_res) {
            stream_index_ = *st_res;
        }
        muxer_->write_header();
    }

    yuv_frame_ = VideoFrame::allocate(config_.width, config_.height, PixelFormat::YUV420P);
    active_ = true;
}

CanvasRecorder::~CanvasRecorder() {
    if (active_) {
        finish();
    }
}

void CanvasRecorder::capture_rgba(const uint8_t* rgba_pixels, size_t stride_bytes, int64_t timestamp_us) {
    if (!active_ || !rgba_pixels) return;

    // Convert RGBA packed surface to planar YUV420P
    ColorConverter::rgba_to_yuv420p(
        rgba_pixels, stride_bytes,
        yuv_frame_.plane_mut(0), yuv_frame_.stride(0),
        yuv_frame_.plane_mut(1), yuv_frame_.stride(1),
        yuv_frame_.plane_mut(2), yuv_frame_.stride(2),
        config_.width, config_.height
    );

    int64_t pts = (timestamp_us >= 0) ? timestamp_us :
                  static_cast<int64_t>((frame_count_ * 1000000ULL) / std::max(config_.fps, 1u));

    Packet pkt = encoder_->encode_frame(yuv_frame_, pts);

    if (muxer_) {
        pkt.set_stream_index(stream_index_);
        muxer_->write_packet(pkt);
    } else if (file_writer_) {
        file_writer_->write(pkt.data(), pkt.size());
    }

    frame_count_++;
}

void CanvasRecorder::capture_canvas([[maybe_unused]] nisaba::Canvas& canvas, [[maybe_unused]] int64_t timestamp_us) {
#if __has_include("nisaba/canvas/canvas.hpp")
    auto& pm = canvas.pixmap();
    capture_rgba(
        reinterpret_cast<const uint8_t*>(pm.data()),
        pm.stride_bytes(),
        timestamp_us
    );
#endif
}

void CanvasRecorder::finish() {
    if (!active_) return;

    if (muxer_) {
        muxer_->write_trailer();
    }
    if (file_writer_) {
        file_writer_->flush();
        file_writer_->close();
    }
    active_ = false;
}

} // namespace atum

#pragma once

#include <queue>
#include <memory>
#include <span>
#include <vector>
#include "atum/decode/decoder.hpp"
#include "atum/codec/video/av1/av1_parser.hpp"
#include "atum/predict/dpb.hpp"

namespace atum {

/// Sovereign AV1 Video Decoder.
/// Integrates multi-symbol CDF arithmetic symbol decoder,
/// Constrained Directional Enhancement Filter (CDEF), and reference frame buffer.
class Av1Decoder : public VideoDecoder {
public:
    Av1Decoder();
    ~Av1Decoder() override = default;

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

    /// Configures the number of parallel worker threads (0 = default pool, 1 = single-threaded).
    void set_thread_count(size_t threads) noexcept { thread_count_ = threads; }
    [[nodiscard]] size_t thread_count() const noexcept { return thread_count_; }

    /// Configures tile grid partitioning.
    void set_tile_grid(uint32_t cols, uint32_t rows) noexcept {
        tile_cols_ = cols;
        tile_rows_ = rows;
    }

private:
    CodecParameters params_;
    Av1Parser parser_;
    std::queue<VideoFrame> ready_frames_;
    DecodedPictureBuffer dpb_{8};
    uint32_t frame_count_{0};
    size_t thread_count_{0};
    uint32_t tile_cols_{2};
    uint32_t tile_rows_{2};

    void reconstruct_frame(VideoFrame& frame, const Av1FrameHeader* header, std::span<const uint8_t> payload);
};

} // namespace atum


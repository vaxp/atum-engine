#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/codec/video/hevc/hevc_types.hpp"
#include "atum/codec/video/hevc/hevc_slice.hpp"
#include "atum/codec/video/hevc/hevc_ctu_decoder.hpp"
#include "atum/entropy/hevc_cabac_contexts.hpp"
#include "atum/predict/dpb.hpp"
#include "atum/parallel/thread_pool.hpp"

namespace atum {

/// HEVC Wavefront Parallel Processing (WPP) execution coordinator (ITU-T H.265 Section 9.3.2).
/// Dispatches independent thread workers per CTU row with a staggered 2-CTU dependency pipeline
/// and synchronized CABAC context state handover from CTU(r, 1) to CTU(r+1, 0).
class HevcWppCoordinator {
public:
    explicit HevcWppCoordinator(ThreadPool& pool = ThreadPool::default_pool());
    ~HevcWppCoordinator() = default;

    /// Decodes an HEVC frame using Wavefront Parallel Processing across worker threads.
    /// `substream_offsets`: Optional byte offsets for each WPP row derived from slice entry points.
    MediaResult<void> decode_frame_wpp(
        VideoFrame& frame,
        const HevcSliceHeader* slice_hdr,
        std::span<const uint8_t> payload,
        const DecodedPictureBuffer& dpb,
        const std::vector<size_t>& substream_offsets = {}
    );

    // =========================================================================
    // Diagnostics & Telemetry
    // =========================================================================

    [[nodiscard]] size_t last_rows_processed() const noexcept { return last_rows_; }
    [[nodiscard]] size_t last_threads_utilized() const noexcept { return last_threads_; }
    [[nodiscard]] uint32_t ctu_size() const noexcept { return ctu_size_; }

private:
    void decode_row_worker(
        uint32_t row_idx,
        uint32_t ctu_cols,
        VideoFrame& frame,
        HevcSliceType slice_type,
        int32_t base_qp,
        std::span<const uint8_t> row_payload,
        const DecodedPictureBuffer& dpb
    );

    ThreadPool& pool_;
    uint32_t ctu_size_{64};

    // Synchronization state across rows
    std::vector<std::atomic<int32_t>> row_progress_;
    std::vector<HevcCabacContextSet> saved_row_contexts_;
    std::vector<std::atomic<bool>> row_context_ready_;

    std::mutex sync_mutex_;
    std::condition_variable sync_cv_;

    size_t last_rows_{0};
    size_t last_threads_{0};
};

} // namespace atum

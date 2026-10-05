#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/parallel/thread_pool.hpp"

namespace atum {

/// Representation of a frame in the asynchronous decoding dependency graph (DAG).
struct FrameGraphNode {
    uint64_t frame_id{0};
    int64_t pts{0};
    int64_t dts{0};
    bool is_keyframe{false};
    bool is_reference{true};

    std::vector<uint64_t> ref_dependencies; // Predecessor frames required before decode
    std::vector<uint64_t> dependents;       // Successor frames waiting for this frame

    std::atomic<int32_t> unresolved_dependencies{0};
    std::atomic<bool> is_completed{false};
    std::function<VideoFrame()> decode_job;
    VideoFrame decoded_frame{};
};

/// High-performance Asynchronous Frame-Level Dependency Graph Scheduler.
/// Tracks inter-picture dependencies across GOP structures and dispatches independent
/// non-reference B-frames and parallel P-frames concurrently to the ThreadPool as soon
/// as their DPB reference pictures reach the reconstructed state.
class FrameGraphScheduler {
public:
    explicit FrameGraphScheduler(ThreadPool& pool = ThreadPool::default_pool());
    ~FrameGraphScheduler();

    FrameGraphScheduler(const FrameGraphScheduler&) = delete;
    FrameGraphScheduler& operator=(const FrameGraphScheduler&) = delete;

    /// Submits a frame decoding job into the dependency graph.
    /// `frame_id`: Unique monotonic frame ID.
    /// `pts`, `dts`: Presentation and decode timestamps.
    /// `ref_ids`: IDs of DPB reference pictures required to reconstruct this frame.
    /// `job`: The lambda/callable performing the frame decoding.
    void submit_frame(
        uint64_t frame_id,
        int64_t pts,
        int64_t dts,
        bool is_keyframe,
        bool is_reference,
        const std::vector<uint64_t>& ref_ids,
        std::function<VideoFrame()> job
    );

    /// Retrieves the next frame in strict presentation order (ordered by PTS).
    /// Returns MediaError::NeedMoreData if the next presentation frame is still in-flight.
    MediaResult<void> receive_presentation_frame(VideoFrame& frame);

    /// Waits for all submitted graph frames to complete.
    void wait_all();

    /// Clears the scheduler state and pending queues.
    void reset();

    // =========================================================================
    // Diagnostics & Telemetry
    // =========================================================================

    [[nodiscard]] size_t frames_in_flight() const noexcept;
    [[nodiscard]] size_t total_frames_scheduled() const noexcept { return total_scheduled_.load(std::memory_order_relaxed); }
    [[nodiscard]] size_t max_concurrency_achieved() const noexcept { return max_concurrency_.load(std::memory_order_relaxed); }

private:
    void on_frame_completed(uint64_t frame_id);

    struct PtsComparator {
        bool operator()(const std::shared_ptr<FrameGraphNode>& a, const std::shared_ptr<FrameGraphNode>& b) const {
            return a->pts > b->pts; // Min-heap by PTS
        }
    };

    ThreadPool& pool_;

    mutable std::mutex graph_mutex_;
    std::condition_variable completion_cv_;

    std::unordered_map<uint64_t, std::shared_ptr<FrameGraphNode>> nodes_;
    std::priority_queue<std::shared_ptr<FrameGraphNode>,
                        std::vector<std::shared_ptr<FrameGraphNode>>,
                        PtsComparator> presentation_queue_;

    int64_t next_expected_pts_{INT64_MIN};
    std::atomic<size_t> active_decodes_{0};
    std::atomic<size_t> max_concurrency_{0};
    std::atomic<size_t> total_scheduled_{0};
    std::atomic<bool> stopped_{false};
};

} // namespace atum

#include "atum/parallel/frame_graph.hpp"
#include <algorithm>
#include <iostream>

namespace atum {

FrameGraphScheduler::FrameGraphScheduler(ThreadPool& pool)
    : pool_(pool) {
}

FrameGraphScheduler::~FrameGraphScheduler() {
    reset();
}

void FrameGraphScheduler::submit_frame(
    uint64_t frame_id,
    int64_t pts,
    int64_t dts,
    bool is_keyframe,
    bool is_reference,
    const std::vector<uint64_t>& ref_ids,
    std::function<VideoFrame()> job
) {
    auto node = std::make_shared<FrameGraphNode>();
    node->frame_id = frame_id;
    node->pts = pts;
    node->dts = dts;
    node->is_keyframe = is_keyframe;
    node->is_reference = is_reference;
    node->decode_job = std::move(job);

    bool ready_to_dispatch = false;

    {
        std::lock_guard<std::mutex> lock(graph_mutex_);
        if (stopped_) return;

        int32_t pending_refs = 0;
        for (uint64_t ref_id : ref_ids) {
            auto it = nodes_.find(ref_id);
            if (it != nodes_.end() && !it->second->is_completed.load(std::memory_order_acquire)) {
                pending_refs++;
                it->second->dependents.push_back(frame_id);
            }
        }

        node->unresolved_dependencies.store(pending_refs, std::memory_order_release);
        nodes_[frame_id] = node;
        total_scheduled_.fetch_add(1, std::memory_order_relaxed);

        if (pending_refs == 0) {
            ready_to_dispatch = true;
        }
    }

    if (ready_to_dispatch) {
        pool_.enqueue([this, frame_id]() {
            std::shared_ptr<FrameGraphNode> target;
            {
                std::lock_guard<std::mutex> lock(graph_mutex_);
                auto it = nodes_.find(frame_id);
                if (it != nodes_.end()) target = it->second;
            }

            if (!target) return;

            size_t in_flight = active_decodes_.fetch_add(1, std::memory_order_relaxed) + 1;
            size_t cur_max = max_concurrency_.load(std::memory_order_relaxed);
            while (in_flight > cur_max &&
                   !max_concurrency_.compare_exchange_weak(cur_max, in_flight, std::memory_order_relaxed)) {
                // spin to update max
            }

            if (target->decode_job) {
                target->decoded_frame = target->decode_job();
            }

            active_decodes_.fetch_sub(1, std::memory_order_relaxed);
            on_frame_completed(frame_id);
        }, is_keyframe ? TaskPriority::High : TaskPriority::Normal);
    }
}

void FrameGraphScheduler::on_frame_completed(uint64_t frame_id) {
    std::vector<uint64_t> unblocked_frame_ids;

    {
        std::lock_guard<std::mutex> lock(graph_mutex_);
        auto it = nodes_.find(frame_id);
        if (it == nodes_.end()) return;

        auto node = it->second;
        node->is_completed.store(true, std::memory_order_release);
        presentation_queue_.push(node);

        for (uint64_t dep_id : node->dependents) {
            auto dep_it = nodes_.find(dep_id);
            if (dep_it != nodes_.end()) {
                if (dep_it->second->unresolved_dependencies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    unblocked_frame_ids.push_back(dep_id);
                }
            }
        }
    }

    completion_cv_.notify_all();

    // Dispatch all newly unblocked frames to ThreadPool
    for (uint64_t unblocked_id : unblocked_frame_ids) {
        pool_.enqueue([this, unblocked_id]() {
            std::shared_ptr<FrameGraphNode> target;
            {
                std::lock_guard<std::mutex> lock(graph_mutex_);
                auto it = nodes_.find(unblocked_id);
                if (it != nodes_.end()) target = it->second;
            }

            if (!target) return;

            size_t in_flight = active_decodes_.fetch_add(1, std::memory_order_relaxed) + 1;
            size_t cur_max = max_concurrency_.load(std::memory_order_relaxed);
            while (in_flight > cur_max &&
                   !max_concurrency_.compare_exchange_weak(cur_max, in_flight, std::memory_order_relaxed)) {
            }

            if (target->decode_job) {
                target->decoded_frame = target->decode_job();
            }

            active_decodes_.fetch_sub(1, std::memory_order_relaxed);
            on_frame_completed(unblocked_id);
        }, TaskPriority::Normal);
    }
}

MediaResult<void> FrameGraphScheduler::receive_presentation_frame(VideoFrame& frame) {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    if (presentation_queue_.empty()) {
        return MediaError::NeedMoreData;
    }

    auto top = presentation_queue_.top();
    presentation_queue_.pop();

    frame = std::move(top->decoded_frame);

    // Prune completed non-reference nodes or nodes with no pending dependents
    if (!top->is_reference && top->dependents.empty()) {
        nodes_.erase(top->frame_id);
    }

    return {};
}

void FrameGraphScheduler::wait_all() {
    std::unique_lock<std::mutex> lock(graph_mutex_);
    completion_cv_.wait(lock, [this]() {
        for (const auto& [id, node] : nodes_) {
            if (!node->is_completed.load(std::memory_order_acquire)) return false;
        }
        return true;
    });
}

void FrameGraphScheduler::reset() {
    {
        std::lock_guard<std::mutex> lock(graph_mutex_);
        stopped_ = true;
        while (!presentation_queue_.empty()) presentation_queue_.pop();
        nodes_.clear();
    }
    completion_cv_.notify_all();
    stopped_ = false;
    total_scheduled_.store(0, std::memory_order_relaxed);
    max_concurrency_.store(0, std::memory_order_relaxed);
}

size_t FrameGraphScheduler::frames_in_flight() const noexcept {
    return active_decodes_.load(std::memory_order_relaxed);
}

} // namespace atum

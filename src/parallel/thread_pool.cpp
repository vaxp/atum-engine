#include "atum/parallel/thread_pool.hpp"
#include <algorithm>
#include <iostream>

namespace atum {

// =============================================================================
// TaskGroup Implementation
// =============================================================================

TaskGroup::TaskGroup(ThreadPool& pool)
    : pool_(pool) {
}

TaskGroup::~TaskGroup() {
    wait();
}

void TaskGroup::run(std::function<void()> task) {
    active_tasks_.fetch_add(1, std::memory_order_release);

    pool_.enqueue([this, user_task = std::move(task)]() {
        try {
            user_task();
        } catch (...) {
            // Task exceptions shouldn't crash the worker thread
        }

        if (active_tasks_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lock(mutex_);
            cv_.notify_all();
        }
    }, TaskPriority::Normal);
}

void TaskGroup::wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() {
        return active_tasks_.load(std::memory_order_acquire) == 0;
    });
}

// =============================================================================
// ThreadPool Implementation
// =============================================================================

ThreadPool::ThreadPool(size_t num_threads) {
    size_t count = num_threads;
    if (count == 0) {
        count = std::max(1u, std::thread::hardware_concurrency());
    }

    workers_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown(false);
}

void ThreadPool::enqueue(std::function<void()> task, TaskPriority priority) {
    if (!task) return;

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) return;

        TaskItem item;
        item.func = std::move(task);
        item.priority = priority;

        if (priority == TaskPriority::High) {
            high_priority_queue_.push(std::move(item));
        } else if (priority == TaskPriority::Low) {
            low_priority_queue_.push(std::move(item));
        } else {
            normal_priority_queue_.push(std::move(item));
        }
    }
    cv_.notify_one();
}

void ThreadPool::shutdown(bool wait) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) return;
        stop_ = true;
        immediate_stop_ = !wait;
    }

    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

ThreadPool& ThreadPool::default_pool() {
    static ThreadPool instance;
    return instance;
}

size_t ThreadPool::pending_tasks() const noexcept {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return high_priority_queue_.size() + normal_priority_queue_.size() + low_priority_queue_.size();
}

void ThreadPool::worker_loop() {
    while (true) {
        TaskItem item;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            cv_.wait(lock, [this]() {
                if (immediate_stop_) return true;
                if (stop_ && high_priority_queue_.empty() && normal_priority_queue_.empty() && low_priority_queue_.empty()) return true;
                return !high_priority_queue_.empty() || !normal_priority_queue_.empty() || !low_priority_queue_.empty();
            });

            if (immediate_stop_) {
                return;
            }

            if (stop_ && high_priority_queue_.empty() && normal_priority_queue_.empty() && low_priority_queue_.empty()) {
                return;
            }

            if (!high_priority_queue_.empty()) {
                item = std::move(high_priority_queue_.front());
                high_priority_queue_.pop();
            } else if (!normal_priority_queue_.empty()) {
                item = std::move(normal_priority_queue_.front());
                normal_priority_queue_.pop();
            } else if (!low_priority_queue_.empty()) {
                item = std::move(low_priority_queue_.front());
                low_priority_queue_.pop();
            } else {
                continue;
            }

            active_workers_.fetch_add(1, std::memory_order_relaxed);
        }

        try {
            if (item.func) {
                item.func();
            }
        } catch (...) {
            // Suppress uncaught task exceptions inside worker thread
        }

        active_workers_.fetch_sub(1, std::memory_order_relaxed);
        completed_tasks_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace atum

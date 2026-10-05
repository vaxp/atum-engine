#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <memory>
#include <atomic>
#include <type_traits>

namespace atum {

enum class TaskPriority : uint8_t {
    High,
    Normal,
    Low
};

class ThreadPool;

/// Lightweight synchronization latch for a batch of parallel tasks.
class TaskGroup {
public:
    explicit TaskGroup(ThreadPool& pool);
    ~TaskGroup();

    TaskGroup(const TaskGroup&) = delete;
    TaskGroup& operator=(const TaskGroup&) = delete;

    /// Dispatches a task to the associated thread pool as part of this group.
    void run(std::function<void()> task);

    /// Blocks until all tasks dispatched through this group have finished execution.
    void wait();

    /// Returns true if all dispatched tasks have completed.
    [[nodiscard]] bool is_done() const noexcept {
        return active_tasks_.load(std::memory_order_acquire) == 0;
    }

private:
    friend class ThreadPool;
    ThreadPool& pool_;
    std::atomic<int32_t> active_tasks_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
};

/// High-performance sovereign C++20 thread pool with priority queuing and task grouping.
class ThreadPool {
public:
    /// Constructs a ThreadPool with `num_threads` workers.
    /// If `num_threads` is 0, defaults to `std::max(1u, std::thread::hardware_concurrency())`.
    explicit ThreadPool(size_t num_threads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Submits a callable to the thread pool and returns a std::future for its result.
    template <typename F, typename... Args>
    auto submit(TaskPriority priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            [func = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
                return func(args...);
            }
        );

        std::future<ReturnType> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("ThreadPool::submit on stopped ThreadPool");
            }

            TaskItem item;
            item.func = [task]() { (*task)(); };
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
        return res;
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        return submit(TaskPriority::Normal, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /// Enqueues a fire-and-forget void callable.
    void enqueue(std::function<void()> task, TaskPriority priority = TaskPriority::Normal);

    /// Shuts down the thread pool.
    /// If `wait` is true, all pending tasks in the queues are executed before workers exit.
    void shutdown(bool wait = true);

    /// Global singleton instance for shared media processing workloads.
    static ThreadPool& default_pool();

    // =========================================================================
    // Diagnostics & Telemetry
    // =========================================================================

    [[nodiscard]] size_t thread_count() const noexcept { return workers_.size(); }
    [[nodiscard]] size_t active_workers() const noexcept { return active_workers_.load(std::memory_order_relaxed); }
    [[nodiscard]] size_t pending_tasks() const noexcept;
    [[nodiscard]] uint64_t total_tasks_completed() const noexcept { return completed_tasks_.load(std::memory_order_relaxed); }

private:
    struct TaskItem {
        std::function<void()> func;
        TaskPriority priority{TaskPriority::Normal};
    };

    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<TaskItem> high_priority_queue_;
    std::queue<TaskItem> normal_priority_queue_;
    std::queue<TaskItem> low_priority_queue_;

    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::condition_variable wait_cv_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> immediate_stop_{false};
    std::atomic<size_t> active_workers_{0};
    std::atomic<uint64_t> completed_tasks_{0};
};

} // namespace atum

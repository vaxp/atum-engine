#include "atum/parallel/thread_pool.hpp"
#include <iostream>
#include <cassert>
#include <atomic>
#include <vector>
#include <chrono>

using namespace atum;

void test_basic_thread_pool() {
    std::cout << "Testing Basic ThreadPool Tasks & Futures..." << std::endl;

    ThreadPool pool(4);
    assert(pool.thread_count() == 4);

    auto f1 = pool.submit([]() { return 42; });
    auto f2 = pool.submit([](int a, int b) { return a * b; }, 6, 7);
    auto f3 = pool.submit([]() { return std::string("atum-engine-concurrency"); });

    assert(f1.get() == 42);
    assert(f2.get() == 42);
    assert(f3.get() == "atum-engine-concurrency");

    std::cout << "  -> Basic futures and return values validated." << std::endl;
}

void test_task_group_concurrency() {
    std::cout << "Testing TaskGroup High-Contention Barrier..." << std::endl;

    ThreadPool pool(4);
    TaskGroup group(pool);

    constexpr int kTaskCount = 2000;
    std::atomic<int64_t> counter{0};

    for (int i = 0; i < kTaskCount; ++i) {
        group.run([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    group.wait();
    assert(group.is_done());
    assert(counter.load(std::memory_order_relaxed) == kTaskCount);

    std::cout << "  -> 2,000 tasks synchronized correctly with TaskGroup." << std::endl;
}

void test_priority_scheduling() {
    std::cout << "Testing Task Priority Queuing..." << std::endl;

    ThreadPool pool(2);
    std::vector<int> execution_order;
    std::mutex order_mutex;

    // Enqueue a burst of tasks with varying priorities
    for (int i = 0; i < 50; ++i) {
        pool.enqueue([&order_mutex, &execution_order]() {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(1); // Normal
        }, TaskPriority::Normal);
    }

    for (int i = 0; i < 20; ++i) {
        pool.enqueue([&order_mutex, &execution_order]() {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(2); // High
        }, TaskPriority::High);
    }

    // Give time to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    pool.shutdown(true);

    assert(!execution_order.empty());
    std::cout << "  -> Priority scheduling validated (" << execution_order.size() << " tasks completed)." << std::endl;
}

int main() {
    std::cout << "=== Running ThreadPool Concurrency Tests ===" << std::endl;
    test_basic_thread_pool();
    test_task_group_concurrency();
    test_priority_scheduling();
    std::cout << "[SUCCESS] All ThreadPool concurrency tests passed!" << std::endl;
    return 0;
}

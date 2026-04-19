// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <vector>

namespace aicode {

/// 通用无类型线程池
/// - 任务提交时捕获所有状态（无共享状态竞争）
/// - 可配置最大并发数
/// - 优雅关闭支持
class ThreadPool {
public:
    /// 创建线程池
    /// @param num_threads 工作线程数（0 = hardware_concurrency）
    /// @param max_pending 最大待处理任务数（0 = 无限制）
    explicit ThreadPool(size_t num_threads = 0, size_t max_pending = 0);

    ~ThreadPool();

    /// 提交任务（无返回值）
    /// T.10: Could use concepts (C++20) for stronger constraints: template<std::invocable F>
    template<typename F>
    void Submit(F&& f) {
        using ReturnType = std::invoke_result_t<F>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::forward<F>(f)
        );

        std::future<ReturnType> future = task->get_future();
        (void)future;  // 不关心返回值

        SubmitTask([task]() { (*task)(); });
    }

    /// 提交任务（返回 future）
    /// T.10: Could use concepts (C++20) for stronger constraints
    template<typename F>
    auto SubmitWithFuture(F&& f) -> std::future<std::invoke_result_t<F>> {
        using ReturnType = std::invoke_result_t<F>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::forward<F>(f)
        );

        std::future<ReturnType> future = task->get_future();

        SubmitTask([task]() { (*task)(); });

        return future;
    }

    /// 提交带回调的任务
    /// T.10: Could use concepts (C++20) for stronger constraints
    template<typename F, typename C>
    void SubmitWithCallback(F&& func, C&& callback) {
        Submit([func = std::forward<F>(func),
                callback = std::forward<C>(callback)]() {
            try {
                auto result = func();
                callback(true, result);
            } catch (const std::exception& e) {
                callback(false, std::string(e.what()));
            }
        });
    }

    /// 获取待处理任务数
    size_t GetPendingCount() const;

    /// 获取活跃任务数
    size_t GetActiveCount() const;

    /// 设置最大并发数（0 = 无限制）
    void SetMaxConcurrency(size_t max);

    /// 等待所有任务完成
    void WaitAll(int timeout_ms = 30000);

    /// 关闭线程池
    void Shutdown();

private:
    using Task = std::function<void()>;

    void SubmitTask(Task task);
    void WorkerLoop();

    std::vector<std::thread> workers_;
    std::queue<Task> task_queue_;

    mutable std::mutex queue_mutex_;
    std::condition_variable task_cv_;
    std::condition_variable done_cv_;

    std::atomic<size_t> max_concurrency_;
    std::atomic<size_t> active_tasks_{0};
    std::atomic<bool> stop_{false};

    size_t max_pending_;
};

// ============== Implementation ==============

inline ThreadPool::ThreadPool(size_t num_threads, size_t max_pending)
    : max_concurrency_(num_threads > 0 ? num_threads : std::thread::hardware_concurrency())
    , max_pending_(max_pending) {

    size_t thread_count = max_concurrency_.load();
    workers_.reserve(thread_count);

    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back(&ThreadPool::WorkerLoop, this);
    }
}

inline ThreadPool::~ThreadPool() {
    Shutdown();
}

inline void ThreadPool::SubmitTask(Task task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 如果队列满了，阻塞等待
        if (max_pending_ > 0 && task_queue_.size() >= max_pending_) {
            task_cv_.wait(lock, [this]() {
                return task_queue_.size() < max_pending_ || stop_.load();
            });
        }

        if (stop_) {
            return;
        }

        task_queue_.push(std::move(task));
    }
    task_cv_.notify_one();
}

inline void ThreadPool::WorkerLoop() {
    while (true) {
        Task task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // 等待任务或停止信号
            task_cv_.wait(lock, [this]() {
                return stop_ || !task_queue_.empty();
            });

            if (stop_ && task_queue_.empty()) {
                return;
            }

            // 检查并发限制
            if (active_tasks_.load() >= max_concurrency_.load()) {
                lock.unlock();
                std::this_thread::yield();
                continue;
            }

            if (task_queue_.empty()) {
                continue;
            }

            task = std::move(task_queue_.front());
            task_queue_.pop();
        }

        // 执行任务
        active_tasks_++;

        try {
            task();
        } catch (...) {
            // 任务异常不传播到工作线程
        }

        active_tasks_--;
        done_cv_.notify_all();
    }
}

inline size_t ThreadPool::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

inline size_t ThreadPool::GetActiveCount() const {
    return active_tasks_.load();
}

inline void ThreadPool::SetMaxConcurrency(size_t max) {
    max_concurrency_ = max;
}

inline void ThreadPool::WaitAll(int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    std::unique_lock<std::mutex> lock(queue_mutex_);
    done_cv_.wait_until(lock, deadline, [this]() {
        return task_queue_.empty() && active_tasks_.load() == 0;
    });
}

inline void ThreadPool::Shutdown() {
    stop_ = true;
    task_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

}  // namespace aicode

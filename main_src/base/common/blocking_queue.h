// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// 线程安全的有界阻塞队列（mutex + condition_variable）。
// ASR/TTS/文本队列共用：生产者 Push/TryPush，消费者 Pop；Close 唤醒所有等待者。
//
// 容量语义：
//   - Push    -> 满时覆盖最旧（TTS/文本，新鲜度优先）
//   - TryPush -> 满时拒绝新元素（ASR 背压，保留已排队话轮）
//
// 存储用 std::deque：两端 push/pop 均为 O(1)，容量固定后不会再增长。
// （容量为 4~10、频率为每句/每次播报一次，不需要环形缓冲的微优化。）

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

#include "common/time_wrapper.h"

namespace prosophor {

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t capacity)
        : capacity_(capacity > 0 ? capacity : 1) {}

    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    ~BlockingQueue() { Close(); }

    /// 非阻塞入队；满时拒绝新元素（ASR 背压）。返回 true = 已入队。
    bool TryPush(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || queue_.size() >= capacity_) {
            return false;
        }
        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    /// 非阻塞入队；满时覆盖最旧（TTS/文本新鲜度优先）。
    void Push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return;
        }
        if (queue_.size() >= capacity_) {
            queue_.pop_front();
        }
        queue_.push_back(std::move(value));
        not_empty_.notify_one();
    }

    /// 阻塞取；Close 且队列空时返回 false
    bool Pop(T& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [&] { return closed_ || !queue_.empty(); });
        return PopLocked(out);
    }

    /// 带超时取；返回 false = 超时或 Close
    bool Pop(T& out, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_empty_.wait_for(lock, prosophor::Milliseconds(timeout_ms),
                                 [&] { return closed_ || !queue_.empty(); })) {
            return false;
        }
        return PopLocked(out);
    }

    /// 关闭并唤醒所有等待者；队列内容丢弃
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return;
        }
        closed_ = true;
        queue_.clear();
        not_empty_.notify_all();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    bool PopLocked(T& out) {
        if (queue_.empty()) {
            return false;   // closed 且空
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::deque<T> queue_;
    size_t capacity_ = 1;
    bool closed_ = false;
};

}  // namespace prosophor

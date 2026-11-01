// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// 缓冲/队列工具（两族，契约不同）：
//   BlockingQueue —— 跨线程交接：有界、Pop 阻塞、Close 唤醒等待者
//   ListBuffer    —— 无阻塞累积：增长、轮询 PopAll 整体取走
//   FixedBuffer   —— 非线程安全定长环形缓冲：永远保留最近 N 条
//
// 选择依据：
//   - 需要背压/新鲜度/线程退出唤醒 -> BlockingQueue
//   - 生产者只追加、消费者稍后批量取 -> ListBuffer
//   - 只要最近 N 条历史 -> FixedBuffer（共享时调用方自己加锁）

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

#include "common/noncopyable.h"
#include "common/time_wrapper.h"

namespace prosophor {

// ── BlockingQueue: 线程安全的有界阻塞队列（mutex + condition_variable）──
// ASR/TTS/文本队列共用：生产者 Push/TryPush，消费者 Pop；Close 唤醒所有等待者。
//
// 容量语义：
//   - Push    -> 满时覆盖最旧（TTS/文本，新鲜度优先）
//   - TryPush -> 满时拒绝新元素（ASR 背压，保留已排队话轮）
//
// 存储是定长环形缓冲：std::vector<T> 预构造 capacity 个槽 + head/size 索引。
// 两端操作 O(1)，槽位循环复用、不搬移元素；容量为 4~32、频率为每句/
// 每次播报一次，不需要再上无锁或分段优化。
template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t capacity)
        : buffer_(capacity > 0 ? capacity : 1), capacity_(buffer_.size()) {}

    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    ~BlockingQueue() { Close(); }

    /// 非阻塞入队；满时拒绝新元素（ASR 背压）。返回 true = 已入队。
    bool TryPush(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || size_ >= capacity_) {
            return false;
        }
        buffer_[(head_ + size_) % capacity_] = std::move(value);
        ++size_;
        not_empty_.notify_one();
        return true;
    }

    /// 非阻塞入队；满时覆盖最旧（TTS/文本新鲜度优先）。
    /// 返回 false = 队列已关闭（未入队）。
    bool Push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        if (size_ >= capacity_) {
            buffer_[head_] = std::move(value);   // 覆盖最旧，头指针顺移
            head_ = (head_ + 1) % capacity_;
        } else {
            buffer_[(head_ + size_) % capacity_] = std::move(value);
            ++size_;
        }
        not_empty_.notify_one();
        return true;
    }

    /// 阻塞取；Close 且队列空时返回 false
    bool Pop(T& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [&] { return closed_ || size_ > 0; });
        return PopLocked(out);
    }

    /// 带超时取；返回 false = 超时或 Close
    bool Pop(T& out, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_empty_.wait_for(lock, prosophor::Milliseconds(timeout_ms),
                                 [&] { return closed_ || size_ > 0; })) {
            return false;
        }
        return PopLocked(out);
    }

    /// 非阻塞取走当前全部元素（按入队顺序）；out 清空后填入，返回取走条数。
    /// 批量消费（如主循环一次取尽 ASR final）只加一次锁。
    size_t Drain(std::vector<T>& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        out.clear();
        out.reserve(size_);
        for (size_t i = 0; i < size_; ++i) {
            const size_t index = (head_ + i) % capacity_;
            out.push_back(std::move(buffer_[index]));
            buffer_[index] = T{};
        }
        const size_t count = size_;
        head_ = 0;
        size_ = 0;
        return count;
    }

    /// 关闭并唤醒所有等待者；队列内容丢弃
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return;
        }
        closed_ = true;
        ResetLocked();
        not_empty_.notify_all();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        ResetLocked();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    bool PopLocked(T& out) {
        if (size_ == 0) {
            return false;   // closed 且空
        }
        out = std::move(buffer_[head_]);
        buffer_[head_] = T{};
        head_ = (head_ + 1) % capacity_;
        --size_;
        return true;
    }

    /// 清空已占用槽位并复位索引；槽位保持预构造，不重新分配 buffer
    void ResetLocked() {
        for (size_t i = 0; i < size_; ++i) {
            buffer_[(head_ + i) % capacity_] = T{};
        }
        head_ = 0;
        size_ = 0;
    }

    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::vector<T> buffer_;   // 定长：capacity_ 个预构造槽位
    size_t capacity_ = 1;
    size_t head_ = 0;         // 最旧元素所在槽位
    size_t size_ = 0;         // 已占用槽数
    bool closed_ = false;
};

// ── ListBuffer: 线程安全的增长缓冲（无阻塞，消费方轮询）──
template <typename T>
class ListBuffer : public Noncopyable {
public:
    ListBuffer() = default;

    ListBuffer(ListBuffer&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mtx_);
        data_ = std::move(other.data_);
    }

    ListBuffer& operator=(ListBuffer&& other) noexcept {
        if (this == &other) return *this;
        std::lock_guard<std::mutex> lock(mtx_);
        std::lock_guard<std::mutex> lock_other(other.mtx_);
        data_ = std::move(other.data_);
        return *this;
    }

    void Push(const T& item) {
        std::lock_guard<std::mutex> lock(mtx_);
        data_.push_back(item);
    }

    void Push(const T* data, size_t count) {
        std::lock_guard<std::mutex> lock(mtx_);
        data_.insert(data_.end(), data, data + count);
    }

    std::vector<T> PopAll() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<T> out;
        std::swap(out, data_);
        return out;
    }

    /// Pop at most n items into pre-allocated out buffer. Returns actual count.
    size_t PopFront(T* out, size_t n) {
        std::lock_guard<std::mutex> lock(mtx_);
        n = std::min(n, data_.size());
        if (n == 0) return 0;
        std::copy_n(data_.begin(), n, out);
        data_.erase(data_.begin(), data_.begin() + n);
        return n;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        data_.clear();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return data_.empty();
    }

    int Size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return static_cast<int>(data_.size());
    }

private:
    std::vector<T> data_;
    mutable std::mutex mtx_;
};

// ── FixedBuffer: 固定容量环形缓冲（非线程安全，保留最近 N 条）──
/// Non-thread-safe ring buffer. Always keeps the most recent kCapacity items.
template <typename T, int kCapacity>
class FixedBuffer {
public:
    void Push(const T& item) {
        data_[head_] = item;
        head_ = (head_ + 1) % kCapacity;
        if (count_ < kCapacity) count_++;
    }

    std::vector<T> ReadAll() const {
        std::vector<T> out;
        if (count_ == 0) return out;
        out.reserve(count_);
        int start = (head_ - count_ + kCapacity) % kCapacity;
        for (int i = 0; i < count_; i++)
            out.push_back(data_[(start + i) % kCapacity]);
        return out;
    }

    void Clear() {
        head_ = 0;
        count_ = 0;
    }

    int Size() const { return count_; }
    static constexpr int Capacity() { return kCapacity; }

private:
    T data_[kCapacity];
    int head_ = 0;
    int count_ = 0;
};

}  // namespace prosophor

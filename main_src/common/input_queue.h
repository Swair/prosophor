// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <condition_variable>
#include <string>
#include <atomic>
#include <vector>

#include "common/noncopyable.h"

namespace aicode {

/// Thread-safe queue for producer-consumer pattern
class InputQueue : public Noncopyable {
 public:
    InputQueue() = default;
    ~InputQueue() = default;

    /// Push a line into the queue
    void Push(const std::string& line) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.push_back(line);
        }
        cv_.notify_one();
    }

    /// Pop all available lines from the queue, blocks if empty
    /// Returns concatenated string, or empty string if interrupted
    std::string WaitAndPopAll(const std::atomic<bool>& interrupted) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this, &interrupted]() {
            return interrupted || !buffer_.empty();
        });

        if (interrupted || buffer_.empty()) {
            return "";
        }

        // Concatenate all available messages
        std::string result;
        for (const auto& line : buffer_) {
            if (!result.empty()) result += "\n";
            result += line;
        }
        buffer_.clear();
        return result;
    }

    /// Check if queue is empty (non-blocking)
    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.empty();
    }

    /// Wake up all waiting threads
    void NotifyAll() {
        cv_.notify_all();
    }

 private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::string> buffer_;
};

}  // namespace aicode

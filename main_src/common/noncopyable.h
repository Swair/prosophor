// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace aicode {

/// Base class for non-copyable objects
class Noncopyable {
 protected:
    Noncopyable() = default;
    ~Noncopyable() = default;
    Noncopyable(const Noncopyable&) = delete;
    Noncopyable& operator=(const Noncopyable&) = delete;
    Noncopyable(Noncopyable&&) = default;
    Noncopyable& operator=(Noncopyable&&) = default;
};

}  // namespace aicode

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <string>

#include "common/config.h"
#include "common/noncopyable.h"

namespace prosophor {

/// Manages the lifecycle of a local llama-server subprocess
/// Uses fork+exec to start/stop llama-server as a background daemon
class LocalModelManager : public Noncopyable {
 public:
    static LocalModelManager& GetInstance();

    /// Start llama-server with the given config
    /// Returns true if started successfully or already running
    bool Start(const LocalModelConfig& config);

    /// Stop the running server (SIGTERM → wait → SIGKILL)
    void Stop();

    /// Check if the server process is alive
    bool IsRunning() const;

    /// Get the port the server is listening on
    int GetPort() const { return config_.port; }

    /// Get the child PID (or -1 if not running)
    int GetPid() const { return pid_; }

 private:
    LocalModelManager() = default;
    ~LocalModelManager();

    /// Wait for the server to start listening on the given port
    bool WaitForPort(int port, int timeout_ms) const;

    /// Wait for the /health endpoint to return OK (after port is open)
    bool WaitForHealth(int port, int timeout_ms) const;

    volatile int pid_ = -1;
    LocalModelConfig config_;
    mutable std::atomic<bool> running_{false};
};

}  // namespace prosophor

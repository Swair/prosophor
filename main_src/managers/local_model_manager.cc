// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/local_model_manager.h"

#include <filesystem>
#include <sstream>
#include <thread>

#include "common/local_model_utils.h"
#include "common/log_wrapper.h"
#include "platform/platform.h"

namespace prosophor {

LocalModelManager& LocalModelManager::GetInstance() {
    static LocalModelManager instance;
    return instance;
}

LocalModelManager::~LocalModelManager() {
    if (running_.load()) {
        Stop();
    }
}

bool LocalModelManager::Start(const LocalModelConfig& config) {
    if (running_.load()) {
        LOG_DEBUG("Local model server already running on port {}", config_.port);
        return true;
    }

    std::string server_path = config.server_path;
    if (server_path.empty()) {
        server_path = FindServerBinary();
    }
    if (server_path.empty()) {
        LOG_ERROR("llama-server binary not found. Use /setup to configure or set server_path in settings.json.");
        return false;
    }
    if (!std::filesystem::exists(server_path)) {
        LOG_ERROR("llama-server not found at: {}", server_path);
        return false;
    }
    std::string resolved_model = ResolveModelPath(config.model_path);
    if (!std::filesystem::exists(resolved_model)) {
        LOG_ERROR("Model file not found: {} (resolved: {})", config.model_path, resolved_model);
        return false;
    }

    LOG_INFO("Starting llama-server: {} -> {}:{}", server_path, resolved_model, config.port);

    std::vector<std::string> args;
    args.push_back(server_path);
    args.push_back("-m");
    args.push_back(resolved_model);
    args.push_back("--port");
    args.push_back(std::to_string(config.port));
    args.push_back("--host");
    args.push_back("127.0.0.1");

    if (config.n_gpu_layers > 0) {
        args.push_back("-ngl");
        args.push_back(std::to_string(config.n_gpu_layers));
    } else if (config.n_gpu_layers == -1) {
        args.push_back("-ngl");
        args.push_back("999");
    }

    if (config.n_threads > 0) {
        args.push_back("-t");
        args.push_back(std::to_string(config.n_threads));
    }

    args.push_back("-c");
    args.push_back("4096");

    auto proc = platform::LaunchProcess(args);
    if (proc.pid < 0) {
        LOG_ERROR("Failed to start llama-server");
        return false;
    }

    pid_ = proc.pid;
    config_ = config;
    running_.store(true);

    if (!WaitForPort(config.port, config.start_timeout_ms)) {
        LOG_ERROR("llama-server failed to start (timeout waiting for port {})", config.port);
        Stop();
        return false;
    }

    // Brief health check (non-blocking) — model may still be loading, service works once ready
    if (!WaitForHealth(config.port, 5000)) {
        LOG_DEBUG("llama-server port open, model loading in progress...");
    }

    LOG_INFO("llama-server started on port {} (PID: {})", config.port, pid_);
    return true;
}

void LocalModelManager::Stop() {
    if (!running_.load()) return;

    LOG_INFO("Stopping llama-server (PID: {})", pid_);

    if (pid_ > 0) {
        if (!platform::KillProcess(pid_, false)) {
            LOG_WARN("Failed to stop llama-server gracefully, forcing...");
            platform::KillProcess(pid_, true);
        }
    }

    pid_ = -1;
    running_.store(false);
    LOG_INFO("llama-server stopped");
}

bool LocalModelManager::IsRunning() const {
    if (!running_.load()) return false;

    // Check process first (reliable), then port (best-effort)
    if (pid_ > 0 && !platform::IsProcessAlive(pid_)) {
        running_.store(false);
        return false;
    }

    if (pid_ > 0) return true;

    // No PID tracked — fall back to port check (may false-positive on WSL2)
    if (platform::CheckPortOpen(config_.port)) return true;

    running_.store(false);
    return false;
}

bool LocalModelManager::WaitForPort(int port, int timeout_ms) const {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        if (platform::CheckPortOpen(port)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return false;
}

bool LocalModelManager::WaitForHealth(int port, int timeout_ms) const {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/health";

    while (std::chrono::steady_clock::now() < deadline) {
        // /health returns 503 while model loads, 200 when ready
        std::string result = platform::RunShellCommand(
            ("curl -s -o /dev/null -w \"%{http_code}\" " + url + " 2>/dev/null").c_str());
        if (result == "200") {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    return false;
}

}  // namespace prosophor

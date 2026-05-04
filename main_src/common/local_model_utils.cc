// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/local_model_utils.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "common/config.h"
#include "common/log_wrapper.h"
#include "platform/platform.h"

namespace prosophor {

std::vector<std::string> GetSearchPaths() {
    std::vector<std::string> paths;

    // 1. Relative to the running prosophor binary
    std::string self_path = platform::GetSelfExePath();
    if (!self_path.empty()) {
        auto bin_dir = std::filesystem::path(self_path).parent_path().string();
        paths.push_back(bin_dir + "/llama-server");
    }

    // 2. ~/.prosophor/bin/llama-server
    std::string home = platform::HomeDir();
    if (!home.empty()) {
        paths.push_back(home + "/.prosophor/bin/llama-server");
        paths.push_back(home + "/.prosophor/llama-server");
    }

    // 3. Development: ../llama.cpp/build/bin/llama-server
    if (!self_path.empty()) {
        auto exe_dir = std::filesystem::path(self_path).parent_path();
        if (!exe_dir.empty()) {
            paths.push_back((exe_dir / ".." / ".." / "llama.cpp" / "build" / "bin" / "llama-server").lexically_normal().string());
            paths.push_back((exe_dir / ".." / "llama.cpp" / "build" / "bin" / "llama-server").lexically_normal().string());
        }
    }

    // 4. CWD relative
    paths.push_back("./llama-server");

    // 5. Common locations
    paths.push_back("/usr/local/bin/llama-server");
    paths.push_back("/usr/bin/llama-server");

    return paths;
}

std::string FindServerBinary() {
    const auto& config = ProsophorConfig::GetInstance();
    for (const auto& lm : config.local_models) {
        if (!lm.server_path.empty() && std::filesystem::exists(lm.server_path)) {
            return lm.server_path;
        }
    }

    for (const auto& path : GetSearchPaths()) {
        if (std::filesystem::exists(path)) {
            LOG_DEBUG("Found llama-server at: {}", path);
            return path;
        }
    }

    // Try `which llama-server`
    std::string which = platform::RunShellCommand("which llama-server 2>/dev/null");
    if (!which.empty()) {
        if (which.back() == '\n') which.pop_back();
        if (std::filesystem::exists(which)) {
            return which;
        }
    }

    return "";
}

std::string ResolveModelPath(const std::string& path) {
    if (path.empty()) return path;
    if (std::filesystem::exists(path)) {
        return std::filesystem::absolute(path).string();
    }

    std::string self_path = platform::GetSelfExePath();
    if (!self_path.empty()) {
        auto bin_dir = std::filesystem::path(self_path).parent_path();
        auto try_path = (bin_dir / path).lexically_normal();
        if (std::filesystem::exists(try_path)) {
            return try_path.string();
        }
        auto project_root = bin_dir.parent_path().parent_path();
        try_path = (project_root / path).lexically_normal();
        if (std::filesystem::exists(try_path)) {
            return try_path.string();
        }
    }

    return path;
}

void DetectHardware(int& out_gpu_layers, int& out_threads) {
    out_gpu_layers = 0;
    out_threads = 0;

    if (platform::kIsLinux) {
        std::string nvidia = platform::RunShellCommand(
            "nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits 2>/dev/null");
        if (!nvidia.empty()) {
            int vram_mb = std::atoi(nvidia.c_str());
            if (vram_mb > 0) {
                LOG_DEBUG("Detected NVIDIA GPU with {} MB VRAM", vram_mb);
                if (vram_mb >= 24000) {
                    out_gpu_layers = -1;
                } else if (vram_mb >= 12000) {
                    out_gpu_layers = 999;
                } else if (vram_mb >= 6000) {
                    out_gpu_layers = 32;
                } else {
                    out_gpu_layers = 16;
                }
            }
        }

        if (out_gpu_layers == 0) {
            std::string other_gpu = platform::RunShellCommand(
                "lspci 2>/dev/null | grep -i 'vga\\|3d' | grep -iv 'nvidia' | head -1");
            if (!other_gpu.empty()) {
                LOG_DEBUG("Detected non-NVIDIA GPU: {}", other_gpu);
            }
        }

        std::string cpu_count_str = platform::RunShellCommand(
            "grep -c '^processor' /proc/cpuinfo 2>/dev/null || nproc");
        if (!cpu_count_str.empty()) {
            long cpu_count = std::atol(cpu_count_str.c_str());
            out_threads = std::max(1L, cpu_count - 1);
            LOG_DEBUG("Detected {} CPU threads, recommending {}", cpu_count, out_threads);
        }
    } else if (platform::kIsMacOS) {
        std::string ram_str = platform::RunShellCommand("sysctl -n hw.memsize 2>/dev/null");
        if (!ram_str.empty()) {
            int64_t ram = std::atoll(ram_str.c_str()) / (1024LL * 1024LL * 1024LL);
            LOG_DEBUG("Detected {} GB unified memory", ram);
            if (ram >= 32) {
                out_gpu_layers = -1;
            } else if (ram >= 16) {
                out_gpu_layers = 999;
            } else {
                out_gpu_layers = 16;
            }
        }

        std::string cpu_str = platform::RunShellCommand("sysctl -n hw.ncpu 2>/dev/null");
        if (!cpu_str.empty()) {
            out_threads = std::max(1, std::atoi(cpu_str.c_str()) - 2);
        }
    }

    LOG_INFO("Hardware detection: GPU layers={}, threads={}",
             out_gpu_layers == -1 ? -1 : out_gpu_layers,
             out_threads == 0 ? -1 : out_threads);
}

}  // namespace prosophor

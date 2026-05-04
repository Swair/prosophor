// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace prosophor {

/// Search paths for llama-server binary
std::vector<std::string> GetSearchPaths();

/// Find llama-server binary using multiple strategies
std::string FindServerBinary();

/// Resolve model path: try as-is, then relative to binary location
std::string ResolveModelPath(const std::string& path);

/// Auto-detect hardware and recommend settings for local model inference
void DetectHardware(int& out_gpu_layers, int& out_threads);

}  // namespace prosophor

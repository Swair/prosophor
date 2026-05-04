// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <ctime>
#include <string>
#include <vector>

// Make Windows headers available everywhere via platform.h
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace prosophor {
namespace platform {

#ifdef _WIN32
constexpr bool kIsWindows = true;
#else
constexpr bool kIsWindows = false;
#endif

#ifdef __linux__
constexpr bool kIsLinux = true;
#else
constexpr bool kIsLinux = false;
#endif

#ifdef __APPLE__
constexpr bool kIsMacOS = true;
#else
constexpr bool kIsMacOS = false;
#endif

/// Convert system native encoding to UTF-8 (no-op on Linux/macOS)
std::string NativeToUtf8(const std::string& input);

/// Read a line from stdin (handles Windows console encoding)
std::string ReadLine();

/// Get home directory (cross-platform: HOME / USERPROFILE)
std::string HomeDir();

/// Convert time_t to local tm (thread-safe, cross-platform)
std::tm LocalTime(std::time_t t);

/// Read a line from console (bypasses stdin encoding issues on Windows)
std::string ReadConsoleLine();

/// Convert UTF-8 to wide string (UTF-16 on Windows, required for CreateProcessW etc.)
std::wstring Utf8ToWide(const std::string& utf8_str);

/// Escape argument for shell (cross-platform)
std::string ShellEscape(const std::string& arg);

/// Set console to UTF-8 mode (no-op on POSIX, sets CP on Windows)
void SetConsoleUtf8();

/// Get current process ID
int GetPid();

/// Get path to current executable
std::string GetSelfExePath();

/// Check if a TCP port is open on localhost
bool CheckPortOpen(int port);

/// Run a shell command and return stdout (empty on error)
std::string RunShellCommand(const char* cmd);

/// Opaque handle to a launched subprocess
struct Subprocess {
    int pid = -1;
};

/// Launch a subprocess with args, detach stdin/stdout/stderr
Subprocess LaunchProcess(const std::vector<std::string>& args);

/// Check if a process with given PID is still alive
bool IsProcessAlive(int pid);

/// Kill a process, optionally forcing immediate termination
bool KillProcess(int pid, bool force = false);

/// Result of running a command with output capture
struct CommandOutput {
    std::string output;
    int exit_code = -1;
};

/// Run a shell command and capture stdout+stderr with optional timeout and workdir
CommandOutput RunCommandWithOutput(const std::string& command,
                                    int timeout_seconds = 0,
                                    const std::string& workdir = "");

}  // namespace platform
}  // namespace prosophor

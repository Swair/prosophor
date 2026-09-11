// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace prosophor {

// ============================================================================
// Path Utilities
// ============================================================================

/// GetHomeDir - Get home directory path
/// @return Home directory path
std::string GetHomeDir();

/// ExpandHome - Expand ~ to home directory
/// @param path Input path (may start with ~/)
/// @return Expanded path with ~ replaced by home directory
std::string ExpandHome(const std::string& path);

/// EnsureDirectory - Create directory if it doesn't exist
/// @param path Directory path
/// @return true if directory exists or was created successfully
bool EnsureDirectory(const std::string& path);

// ============================================================================
// File Reading
// ============================================================================

/// ReadFile - Read entire file content as string
/// @param path File path
/// @return File content, or nullopt if file doesn't exist or can't be read
std::optional<std::string> ReadFile(const std::string& path);

/// ReadFileOrFail - Read entire file content, throw on error
/// @param path File path
/// @return File content
/// @throws std::runtime_error if file can't be read
std::string ReadFileOrFail(const std::string& path);

/// ReadJson - Read and parse JSON file
/// @param path File path
/// @return Parsed JSON object, or nullopt if file doesn't exist
std::optional<nlohmann::json> ReadJson(const std::string& path);

/// ReadJsonOrFail - Read and parse JSON file, throw on error
/// @param path File path
/// @return Parsed JSON object
/// @throws std::runtime_error if file can't be read or parsed
nlohmann::json ReadJsonOrFail(const std::string& path);

/// LoadWav - Load 16-bit PCM mono WAV file into int16 vector
/// @param path WAV file path
/// @return PCM samples (mono, mixed down if stereo), empty on error
std::vector<int16_t> LoadWav(const std::string& path);

/// LoadBinaryFile - 整份读入二进制文件到字节缓冲（无解释、无对齐要求）
/// @param path 文件路径
/// @param out 输出缓冲；失败时保持为空
/// @return true 表示读取成功
bool LoadBinaryFile(const std::string& path, std::vector<uint8_t>& out);

/// LoadRawFloats - 把 .raw 权重文件按 float32 整份读入（TensorFlow/ONNX 导出的裸张量常见格式）
/// @param path 文件路径
/// @param out 输出缓冲，长度 = 文件字节数 / sizeof(float)；失败时保持为空
/// @return true 表示读取成功（文件大小为 float 整数倍且完整读入）
bool LoadRawFloats(const std::string& path, std::vector<float>& out);

/// WriteWav - Write 16-bit PCM mono data to WAV file
/// @param path Output WAV file path
/// @param samples PCM samples (int16)
/// @param sample_rate Sample rate (e.g. 24000)
/// @return true if write succeeded
bool WriteWav(const std::string& path, const std::vector<int16_t>& samples, int sample_rate);

// ============================================================================
// File Writing
// ============================================================================

/// WriteFile - Write string content to file
/// @param path File path
/// @param content Content to write
/// @param append If true, append to existing file
/// @return true if write succeeded
bool WriteFile(const std::string& path, const std::string& content, bool append = false);

/// WriteJson - Write JSON to file with formatting
/// @param path File path
/// @param json JSON object to write
/// @param indent Indentation (default: 2)
/// @return true if write succeeded
bool WriteJson(const std::string& path, const nlohmann::json& json, int indent = 2);

/// WriteOrderedJson - Write ordered_json to file preserving key insertion order
/// @param path File path
/// @param json ordered_json object to write
/// @param indent Indentation (default: 2)
/// @return true if write succeeded
bool WriteOrderedJson(const std::string& path, const nlohmann::ordered_json& json, int indent = 2);

/// WriteJsonOrFail - Write JSON to file, throw on error
/// @param path File path
/// @param json JSON object to write
/// @param indent Indentation (default: 2)
/// @throws std::runtime_error if write fails
void WriteJsonOrFail(const std::string& path, const nlohmann::json& json, int indent = 2);

// ============================================================================
// File Existence Checks
// ============================================================================

/// FileExists - Check if file exists
/// @param path File path
/// @return true if file exists
bool FileExists(const std::string& path);

/// DirExists - Check if directory exists
/// @param path Directory path
/// @return true if directory exists
bool DirExists(const std::string& path);

/// RemoveFile - Delete a file
/// @param path File path
/// @return true if file was deleted
bool RemoveFile(const std::string& path);

/// FileSize - Get file size in bytes
/// @param path File path
/// @return File size, or 0 if file doesn't exist or can't be read
uintmax_t FileSize(const std::string& path);

/// FindFileInDirs - Search base_dir + immediate subdirectories for a file
/// @param base_dir Base directory
/// @param filename File name to search for
/// @return Full path if found, empty string otherwise
std::string FindFileInDirs(const std::string& base_dir, const std::string& filename);

/// ParentDir - Get the parent directory of a path
/// @param path File or directory path
/// @return Parent directory, or "." if no separator found
std::string ParentDir(const std::string& path);

// ============================================================================
// Path Joining (cross-platform, no double separators)
// ============================================================================

/// JoinPath - Join path components using std::filesystem::path::operator/=
/// @tparam Args Path component types (convertible to std::filesystem::path)
/// @param first First path component
/// @param rest Remaining path components
/// @return Joined path string
template <typename T, typename... Args>
std::string JoinPath(T&& first, Args&&... rest) {
    std::filesystem::path p(std::forward<T>(first));
    ((p /= std::forward<Args>(rest)), ...);
    return p.string();
}

/// ListDir - List filenames (not full paths) in a directory, optionally filtered by extension
/// @param dir Directory path
/// @param ext Filter extension (e.g. ".json"), empty = all files
/// @return Sorted list of filenames
std::vector<std::string> ListDir(const std::string& dir, const std::string& ext = "");

/// ReadDir - List entries in a directory, returning full paths
/// @param dir Directory path
/// @return List of full paths for regular files
std::vector<std::string> ReadDir(const std::string& dir);

}  // namespace prosophor

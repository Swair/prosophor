// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/file_utils.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <algorithm>

#include "common/log_wrapper.h"

namespace prosophor {

// ============================================================================
// Path Utilities
// ============================================================================

std::string GetHomeDir() {
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    return home ? std::string(home) : "";
}

std::string ExpandHome(const std::string& path) {
    std::string expanded = path;
    if (expanded.size() >= 2 && expanded.substr(0, 2) == "~/") {
        std::string home = GetHomeDir();
        if (!home.empty()) {
            expanded = home + expanded.substr(1);
        }
    }
    return expanded;
}

bool EnsureDirectory(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec)) {
        return true;
    }
    return std::filesystem::create_directories(path, ec);
}

// ============================================================================
// File Reading
// ============================================================================

std::optional<std::string> ReadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

std::string ReadFileOrFail(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

std::optional<nlohmann::json> ReadJson(const std::string& path) {
    auto content = ReadFile(path);
    if (!content) {
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(*content);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse JSON file {}: {}", path, e.what());
        return std::nullopt;
    }
}

nlohmann::json ReadJsonOrFail(const std::string& path) {
    std::string content = ReadFileOrFail(path);
    try {
        return nlohmann::json::parse(content);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse JSON file " + path + ": " + e.what());
    }
}

// ============================================================================
// File Writing
// ============================================================================

bool WriteFile(const std::string& path, const std::string& content, bool append) {
    std::ofstream file(path, append ? std::ios::app : std::ios::out);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: {}", path);
        return false;
    }
    file << content;
    return file.good();
}

bool WriteJson(const std::string& path, const nlohmann::json& json, int indent) {
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: {}", path);
        return false;
    }
    file << json.dump(indent);
    return file.good();
}

bool WriteOrderedJson(const std::string& path, const nlohmann::ordered_json& json, int indent) {
    std::string content = json.dump(indent);
    return WriteFile(path, content);
}

void WriteJsonOrFail(const std::string& path, const nlohmann::json& json, int indent) {
    if (!WriteJson(path, json, indent)) {
        throw std::runtime_error("Failed to write JSON file: " + path);
    }
}

// ============================================================================
// File Existence Checks
// ============================================================================

bool FileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

bool DirExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec);
}

bool RemoveFile(const std::string& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

uintmax_t FileSize(const std::string& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : size;
}

std::string FindFileInDirs(const std::string& base_dir, const std::string& filename) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(base_dir, ec)) {
        if (!entry.is_directory()) continue;
        std::filesystem::path candidate = entry.path() / filename;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate.string();
        }
    }
    return {};
}

// ============================================================================
// Binary / Raw Tensor Loading
// ============================================================================

bool LoadBinaryFile(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        LOG_ERROR("LoadBinaryFile: cannot open: {}", path);
        return false;
    }
    const std::streamsize size = f.tellg();
    if (size < 0) {
        LOG_ERROR("LoadBinaryFile: cannot size: {}", path);
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(out.data()), size)) {
        LOG_ERROR("LoadBinaryFile: short read: {}", path);
        out.clear();
        return false;
    }
    return true;
}

bool LoadRawFloats(const std::string& path, std::vector<float>& out) {
    out.clear();
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        LOG_ERROR("LoadRawFloats: cannot open: {}", path);
        return false;
    }
    const std::streamsize size = f.tellg();
    if (size < 0 || size % static_cast<std::streamsize>(sizeof(float)) != 0) {
        LOG_ERROR("LoadRawFloats: size {} not a multiple of float: {}", size, path);
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size) / sizeof(float));
    if (size > 0 && !f.read(reinterpret_cast<char*>(out.data()), size)) {
        LOG_ERROR("LoadRawFloats: short read: {}", path);
        out.clear();
        return false;
    }
    return true;
}

// ============================================================================
// WAV Loading
// ============================================================================

std::vector<int16_t> LoadWav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        LOG_ERROR("LoadWav: cannot open: {}", path);
        return {};
    }

    struct WavPcmHeader {
        char     riff[4];
        uint32_t file_size;
        char     wave[4];
        char     fmt_id[4];
        uint32_t fmt_size;
        uint16_t audio_format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
    };
    WavPcmHeader hdr{};
    static_assert(sizeof(WavPcmHeader) == 36, "WAV header size mismatch");
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f) {
        return {};
    }

    if (std::string_view(hdr.riff, 4) != "RIFF" ||
        std::string_view(hdr.wave, 4) != "WAVE") {
        LOG_ERROR("LoadWav: not a valid WAV file: {}", path);
        return {};
    }

    if (hdr.audio_format != 1) {
        LOG_ERROR("LoadWav: only PCM WAV supported, format={}", hdr.audio_format);
        return {};
    }

    if (hdr.fmt_size > 16) {
        f.seekg(hdr.fmt_size - 16, std::ios::cur);
    }

    uint32_t data_size = 0;
    char chunk_id[4];
    uint32_t chunk_size;
    while (f.read(chunk_id, 4) && f.read(reinterpret_cast<char*>(&chunk_size), 4)) {
        if (std::string_view(chunk_id, 4) == "data") {
            data_size = chunk_size;
            break;
        }
        f.seekg(chunk_size, std::ios::cur);
    }

    if (data_size == 0) {
        LOG_ERROR("LoadWav: no data chunk in: {}", path);
        return {};
    }

    if (hdr.bits_per_sample != 16) {
        LOG_ERROR("LoadWav: only 16-bit PCM supported, got {} in {}", hdr.bits_per_sample, path);
        return {};
    }

    size_t num_samples = data_size / 2;
    std::vector<int16_t> buf(num_samples);
    if (!f.read(reinterpret_cast<char*>(buf.data()), data_size)) {
        return {};
    }

    if (hdr.num_channels == 1) {
        return buf;
    }

    std::vector<int16_t> mono(num_samples / 2);
    for (size_t i = 0; i < mono.size(); ++i) {
        mono[i] = static_cast<int16_t>(
            (static_cast<int32_t>(buf[i * 2]) + static_cast<int32_t>(buf[i * 2 + 1])) / 2);
    }
    return mono;
}

bool WriteWav(const std::string& path, const std::vector<int16_t>& samples, int sample_rate) {
    if (samples.empty()) return false;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    int bits_per_sample = 16;
    int num_channels = 1;
    int byte_rate = sample_rate * num_channels * bits_per_sample / 8;
    int block_align = num_channels * bits_per_sample / 8;
    uint32_t data_size = static_cast<uint32_t>(samples.size()) * sizeof(int16_t);
    uint32_t file_size = 36 + data_size;

    struct {
        char     chunk_id[4] = {'R','I','F','F'};
        uint32_t chunk_size;
        char     format[4]   = {'W','A','V','E'};
        char     subchunk1_id[4] = {'f','m','t',' '};
        uint32_t subchunk1_size = 16;
        uint16_t audio_format = 1;  // PCM
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
        char     subchunk2_id[4] = {'d','a','t','a'};
        uint32_t subchunk2_size;
    } header{};

    header.chunk_size = file_size;
    header.num_channels = num_channels;
    header.sample_rate = static_cast<uint32_t>(sample_rate);
    header.byte_rate = byte_rate;
    header.block_align = block_align;
    header.bits_per_sample = bits_per_sample;
    header.subchunk2_size = data_size;

    f.write(reinterpret_cast<const char*>(&header), sizeof(header));
    f.write(reinterpret_cast<const char*>(samples.data()), data_size);
    return true;
}

// ============================================================================
// Path Utilities
// ============================================================================

std::string ParentDir(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? "." : path.substr(0, pos);
}

std::vector<std::string> ListDir(const std::string& dir, const std::string& ext) {
    std::vector<std::string> result;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().string();
        if (ext.empty() || (name.size() >= ext.size() &&
            name.compare(name.size() - ext.size(), ext.size(), ext) == 0)) {
            result.push_back(std::move(name));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> ReadDir(const std::string& dir) {
    std::vector<std::string> result;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        result.push_back(entry.path().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace prosophor

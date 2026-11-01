// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "file_utils.h"
#include "time_wrapper.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <string>
#include <unordered_map>
#include <chrono>
#include <ctime>
#include <vector>
#include <memory>

namespace prosophor {

/// Access the ring buffer sink for in-memory log viewing (real-time, no file I/O)
inline std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> GetLogRingSink() {
    static auto sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(1000);
    return sink;
}

inline void InitLog(const std::string& level = "info") {
    static const std::unordered_map<std::string, spdlog::level::level_enum> kLevelMap = {
        {"trace", spdlog::level::trace},
        {"debug", spdlog::level::debug},
        {"info", spdlog::level::info},
        {"warn", spdlog::level::warn},
        {"error", spdlog::level::err},
        {"critical", spdlog::level::critical}
    };

    auto it = kLevelMap.find(level);
    spdlog::level::level_enum log_level = (it != kLevelMap.end()) ? it->second : spdlog::level::info;

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    sinks.push_back(GetLogRingSink());

// Always write log files to ~/.prosophor/log/log-YYYYMMDD.txt
    std::string log_dir = ExpandHome("~/.prosophor/log");
    EnsureDirectory(log_dir);
    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &now_t);
#else
    localtime_r(&now_t, &tm);
#endif
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    std::string log_file = log_dir + "/log-" + buf + ".txt";
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true));

    auto logger = std::make_shared<spdlog::logger>("prosophor", sinks.begin(), sinks.end());
    logger->set_level(log_level);
    // %L = 级别首字母（大写 T/D/I/W/E/C）
    // logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e|%l|%t][%3#|%-16s] %v");
    logger->set_pattern("[%H:%M:%S.%e|%L|%t][%3#|%-16s] %v");
    spdlog::flush_every(std::chrono::seconds(30));  // flush to disk every 30s
    spdlog::set_default_logger(logger);
}

/// Rate-limited logger: ensures a log site fires at most once per interval_ms.
/// Usage: static ThrottleLog tlog;
///        if (tlog.Check(5000)) { LOG_DEBUG("..."); }
class ThrottleLog {
public:
    ThrottleLog() : last_(SteadyClock::Now()) {}

    /// Returns true if interval_ms has elapsed since last true return,
    /// and resets the internal timer. Thread-safe if called from one thread.
    bool Check(int64_t interval_ms) {
        if (SteadyClock::IsExpired(last_, interval_ms)) {
            last_ = SteadyClock::Now();
            return true;
        }
        return false;
    }

private:
    SteadyClock::TimePoint last_;
};

}  // namespace prosophor

#define LOG_INFO(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::info, __VA_ARGS__)
#define LOG_DEBUG(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::debug, __VA_ARGS__)
#define LOG_WARN(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::err, __VA_ARGS__)
#define LOG_FATAL(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::err, __VA_ARGS__)

/// Throttled log: fires at most once per interval_ms per call site.
/// Usage: THROTTLE_LOG(5000, "Generating... {} tokens", n);
#define THROTTLE_LOG(interval_ms, ...) do { \
    static ::prosophor::ThrottleLog _prosophor_tlog; \
    if (_prosophor_tlog.Check(interval_ms)) { \
        LOG_INFO(__VA_ARGS__); \
    } \
} while(0)

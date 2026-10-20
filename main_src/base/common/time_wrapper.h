// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <ctime>
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>


namespace prosophor {

// 时长字面量的统一入口：业务代码不直接写 std::chrono::milliseconds/seconds
using Milliseconds = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;

// ============================================================================
// SystemClock - 墙钟时间工具类（用于时间戳、日期显示）
// ============================================================================

class SystemClock {
public:
    /// Get local time from system_clock
    static std::tm GetLocalTime(std::chrono::system_clock::time_point time_point = std::chrono::system_clock::now()) {
        auto time_t_val = std::chrono::system_clock::to_time_t(time_point);
        return GetLocalTime(time_t_val);
    }

    /// Get local time from time_t (thread-safe, cross-platform)
    static std::tm GetLocalTime(std::time_t time) {
        std::tm tm_result{};
#ifdef _WIN32
        localtime_s(&tm_result, &time);
#else
        localtime_r(&time, &tm_result);
#endif
        return tm_result;
    }

    /// Format time_point to string
    static std::string FormatTimestamp(
        std::chrono::system_clock::time_point time_point = std::chrono::system_clock::now(),
        const char* format = "%Y-%m-%d %H:%M:%S") {
        auto time_t_val = std::chrono::system_clock::to_time_t(time_point);
        return FormatTimestamp(time_t_val, format);
    }

    /// Format time_t to string
    static std::string FormatTimestamp(std::time_t time, const char* format = "%Y-%m-%d %H:%M:%S") {
        std::tm tm_result = GetLocalTime(time);
        std::ostringstream oss;
        oss << std::put_time(&tm_result, format);
        return oss.str();
    }

    /// Get current timestamp string (YYYY-MM-DD HH:MM:SS)
    static std::string GetCurrentTimestamp() {
        return FormatTimestamp(NowSystem());
    }

    /// Get current date string (YYYY-MM-DD)
    static std::string GetCurrentDate() {
        return FormatTimestamp(NowSystem(), "%Y-%m-%d");
    }

    /// Get current time formatted with custom format string
    static std::string FormatCurrentTime(const char* format = "%Y-%m-%d %H:%M:%S") {
        return FormatTimestamp(NowSystem(), format);
    }

    /// Get current time as seconds since epoch (double, suitable for timestamps)
    static double GetCurrentEpochSeconds() {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()) / 1000.0;
    }

    /// Get current time as milliseconds since epoch
    static int64_t GetCurrentTimeMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    /// Generate unique ID with timestamp prefix
    static std::string GenerateIdWithTimestamp(const std::string& prefix) {
        return prefix + std::to_string(GetCurrentTimeMillis());
    }

    /// Format GMT date string in HTTP/Python style:
    /// "Wed May 27 2026 10:30:00 GMT+0000 (Coordinated Universal Time)"
    static std::string FormatGmtString() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm gmt{};
#ifdef _WIN32
        gmtime_s(&gmt, &t);
#else
        gmtime_r(&t, &gmt);
#endif
        char buf[128];
        std::strftime(buf, sizeof(buf),
            "%a %b %d %Y %H:%M:%S GMT+0000 (Coordinated Universal Time)", &gmt);
        return buf;
    }

private:
    /// Internal helper to get current system_clock time point
    static std::chrono::system_clock::time_point NowSystem() {
        return std::chrono::system_clock::now();
    }
};

// ============================================================================
// SteadyClock - 单调时钟工具类（用于计时、超时判断）
// ============================================================================

class SteadyClock {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::steady_clock::duration;

    /// Get current time point
    static TimePoint Now() {
        return std::chrono::steady_clock::now();
    }

    /// Convert duration to milliseconds
    template<typename Rep, typename Period>
    static int64_t ToMillis(std::chrono::duration<Rep, Period> duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    /// Get elapsed time in milliseconds since a time point
    static int64_t ElapsedMillis(TimePoint since) {
        return ToMillis(Now() - since);
    }

    /// Get elapsed time in seconds since a time point
    static double ElapsedSeconds(TimePoint since) {
        return std::chrono::duration<double>(Now() - since).count();
    }

    /// Check if elapsed time exceeds threshold (milliseconds)
    static bool IsExpired(TimePoint since, int64_t threshold_ms) {
        return ElapsedMillis(since) >= threshold_ms;
    }

    /// Check if elapsed time exceeds threshold (seconds)
    static bool IsExpired(TimePoint since, double threshold_s) {
        return ElapsedSeconds(since) >= threshold_s;
    }
};

}  // namespace prosophor

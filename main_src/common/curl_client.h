// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/time_wrapper.h"
#include <cstddef>
#include <curl/curl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace prosophor {

struct StreamHandler;

/// RAII wrapper for HTTP header list
class HeaderList {
   public:
    HeaderList();
    ~HeaderList();

    struct curl_slist* get() const { return list_; }
    void append(const char* str);
    void clear();

   private:
    struct curl_slist* list_;
};

/// Configuration for HTTP requests (supports both blocking and streaming)
struct HttpRequest {
    std::string url;
    std::string body;
    struct curl_slist* headers = nullptr;
    long timeout_seconds = 30;
    long low_speed_limit = 0;  // bytes/sec, 0 = disabled
    long low_speed_time = 0;   // seconds
    std::string user_agent;

    // Streaming callback: called for each chunk of data received
    // If nullptr, the request is treated as blocking and body is collected
    StreamHandler* write_data = nullptr;
};

/// Result of an HTTP request
struct HttpResponse {
    CURLcode curl_code = CURLE_OK;
    int status_code = 0;
    std::string body;
    std::string error_msg;
    int retry_after_seconds = 0;

    bool success() const { return curl_code == CURLE_OK && status_code >= 200 && status_code < 300; }
    bool failed() const { return !success(); }
};

/// High-level HTTP client with RAII-managed curl global state and connection reuse
class HttpClient {
   public:
    static HttpClient& Instance();

    HttpResponse Get(const HttpRequest& request);
    HttpResponse Post(const HttpRequest& request);
    HttpResponse Post(const std::string& url,
                     const std::string& body,
                     struct curl_slist* headers,
                     long timeout_seconds);

   private:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    CURL* AcquireHandle(const std::string& url);
    void ReleaseHandle(const std::string& url, CURL* handle);

    static constexpr double kPooledHandleMaxIdleSec = 90.0;
    static constexpr size_t kPoolMaxHandlesPerHost = 4;

    struct PooledHandle {
        CURL* handle;
        SteadyClock::TimePoint last_used;
    };

    std::mutex pool_mutex_;
    std::unordered_map<std::string, std::vector<PooledHandle>> handle_pool_;
};

/// Stream output phase (for UI thinking/content/tool_calls state tracking)
enum class StreamPhase {
    kNone,
    kThinking,
    kContent,
    kToolCalls,
};

/// Base class for handling streaming responses
struct StreamHandler {
    std::string error_msg;  // Non-empty means API returned an error
    StreamPhase phase = StreamPhase::kNone;

    virtual ~StreamHandler() = default;

    /// Called when a complete line is received
    virtual void OnLine(const std::string& line) = 0;

    /// Called when an event is parsed (for SSE)
    virtual void OnEvent(const std::string& event_type, const std::string& data) = 0;

    /// Get the buffer for processing
    virtual std::string& Buffer() = 0;
};

/// SSE (Server-Sent Events) stream handler
struct SseStreamHandler : public StreamHandler {
    struct PendingToolCall {
        std::string id;
        std::string name;
        std::string arguments;
    };
    std::vector<PendingToolCall> pending_tool_calls;

    void OnLine(const std::string& line) override;
    void OnEvent(const std::string& event_type, const std::string& data) override;
    std::string& Buffer() { return buffer; }

    std::string buffer;
    std::string current_event;
    std::string current_data;
};

/// Write callback for simple body collection
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);

/// Header callback for parsing response headers
size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata);

/// Write callback that delegates to a StreamHandler (for SSE streaming)
size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

}  // namespace prosophor

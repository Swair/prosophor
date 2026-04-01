// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <curl/curl.h>
#include <functional>
#include <memory>
#include <string>

namespace aicode {

/// RAII wrapper for HTTP header list
class HeaderList {
   public:
    HeaderList();
    ~HeaderList();

    void* get() const { return list_; }
    struct curl_slist* raw() const { return static_cast<struct curl_slist*>(list_); }
    void append(const char* str);
    void clear();

   private:
    void* list_;
};

/// Configuration for HTTP requests
struct HttpRequest {
    std::string url;
    std::string post_data;
    void* headers = nullptr;
    long timeout_seconds = 30;
    long low_speed_limit = 0;  // bytes/sec, 0 = disabled
    long low_speed_time = 0;   // seconds
    std::string user_agent;
};

/// Result of an HTTP request
struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string error;
    int retry_after_seconds = 0;

    bool success() const { return status_code >= 200 && status_code < 300; }
    bool failed() const { return !error.empty() || status_code >= 400; }
};

/// High-level HTTP client for blocking requests
class HttpClient {
   public:
    // Perform a blocking HTTP POST request
    static HttpResponse Post(const HttpRequest& request);

    // Perform a blocking HTTP POST request with simple parameters
    static HttpResponse Post(const std::string& url,
                            const std::string& post_data,
                            void* headers,
                            long timeout_seconds);
};

/// Configuration for streaming requests
struct StreamRequest {
    std::string url;
    std::string post_data;
    void* headers = nullptr;
    long timeout_seconds = 30;
    long low_speed_limit = 0;
    long low_speed_time = 0;
    std::string user_agent;

    // Streaming callback: called for each chunk of data received
    using DataCallback = size_t (*)(void*, size_t, size_t, void*);
    DataCallback write_function = nullptr;
    void* write_data = nullptr;
};

/// ChatStream client for SSE and other streaming protocols
class StreamClient {
   public:
    // Perform a streaming HTTP POST request
    static void Post(const StreamRequest& request);
};

/// Base class for handling streaming responses
class StreamHandler {
   public:
    virtual ~StreamHandler() = default;

    /// Called when a complete line is received
    virtual void OnLine(const std::string& line) = 0;

    /// Called when an event is parsed (for SSE)
    virtual void OnEvent(const std::string& event_type, const std::string& data);

    /// Called when the stream ends
    virtual void OnStreamEnd();

    /// Get the buffer for processing
    virtual std::string& Buffer() = 0;
};

/// SSE (Server-Sent Events) stream handler
class SseStreamHandler : public StreamHandler {
   public:
    void OnLine(const std::string& line) override;
    void OnEvent(const std::string& event_type, const std::string& data) override;

    /// Set callback for data events
    using DataCallback = std::function<void(const std::string& event_type, const std::string& data)>;
    void SetDataCallback(DataCallback callback) { data_callback_ = callback; }

    /// Get the event stream buffer for custom processing
    std::string& Buffer() { return buffer_; }

   protected:
    std::string buffer_;
    std::string current_event_;
    std::string current_data_;
    DataCallback data_callback_;
};

/// Write callback for simple body collection
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);

/// Header callback for parsing response headers
size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata);

/// Write callback that delegates to a StreamHandler (for SSE streaming)
size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

}  // namespace aicode

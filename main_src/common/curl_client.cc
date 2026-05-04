// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/curl_client.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <nlohmann/json.hpp>
#include "log_wrapper.h"

namespace prosophor {

// ============================================================================
// HttpClient RAII singleton
// ============================================================================

HttpClient& HttpClient::Instance() {
    static HttpClient instance;
    return instance;
}

HttpClient::HttpClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

HttpClient::~HttpClient() {
    for (auto& [key, entries] : handle_pool_) {
        for (auto& entry : entries) {
            curl_easy_cleanup(entry.handle);
        }
    }
    handle_pool_.clear();
    curl_global_cleanup();
}

// ============================================================================
// Connection pool
// ============================================================================

static std::string ExtractHostKey(const std::string& url) {
    auto pos = url.find("://");
    if (pos == std::string::npos) return url;
    auto start = pos + 3;
    auto end = url.find('/', start);
    return url.substr(0, end);
}

CURL* HttpClient::AcquireHandle(const std::string& url) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto key = ExtractHostKey(url);
    auto it = handle_pool_.find(key);
    if (it != handle_pool_.end() && !it->second.empty()) {
        auto& vec = it->second;
        auto& entry = vec.back();
        if (ElapsedSeconds(entry.last_used) < kPooledHandleMaxIdleSec) {
            CURL* handle = entry.handle;
            vec.pop_back();
            if (vec.empty()) {
                handle_pool_.erase(it);
            }
            curl_easy_reset(handle);
            return handle;
        }
        // purging expired handles
        for (auto& e : vec) {
            curl_easy_cleanup(e.handle);
        }
        handle_pool_.erase(it);
    }
    return curl_easy_init();
}

void HttpClient::ReleaseHandle(const std::string& url, CURL* handle) {
    if (!handle) return;
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto key = ExtractHostKey(url);
    auto& vec = handle_pool_[key];
    if (vec.size() < kPoolMaxHandlesPerHost) {
        vec.push_back({handle, SteadyClock::Now()});
    } else {
        curl_easy_cleanup(handle);
    }
}

// ============================================================================
// Retry-After header parser
// ============================================================================

static int ParseRetryAfter(const std::string& header_text) {
    int retry_after_seconds = 0;
    std::istringstream stream(header_text);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.size() <= 12) continue;

        std::string lower = line.substr(0, 12);
        for (auto& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (lower != "retry-after:") continue;

        std::string value = line.substr(12);
        auto start = value.find_first_not_of(" \t");
        if (start == std::string::npos) continue;

        value = value.substr(start);
        try {
            retry_after_seconds = std::stoi(value);
            break;
        } catch (...) {
            continue;
        }
    }

    return retry_after_seconds;
}

// ============================================================================
// HeaderList implementation
// ============================================================================

HeaderList::HeaderList() : list_(nullptr) {}

HeaderList::~HeaderList() {
    if (list_) {
        curl_slist_free_all(list_);
    }
}

void HeaderList::append(const char* str) {
    list_ = curl_slist_append(list_, str);
}

void HeaderList::clear() {
    if (list_) {
        curl_slist_free_all(list_);
        list_ = nullptr;
    }
}

// ============================================================================
// HttpClient implementation
// ============================================================================

HttpResponse HttpClient::Get(const HttpRequest& request) {
    HttpResponse response;

    CURL* curl = AcquireHandle(request.url);
    if (!curl) {
        response.error_msg = "Failed to initialize CURL handle";
        return response;
    }

    std::string res_header;
    std::string res_body;
    const bool is_streaming = request.write_data != nullptr;

    // Configure the request for GET
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request.headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    // Default: collect body (blocking mode)
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res_body);

    // Override with user-provided callback for streaming
    if (is_streaming) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, request.write_data);
    } else {
        // Header collection only needed for blocking mode
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res_header);
    }

    if (request.low_speed_limit > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, request.low_speed_limit);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, request.low_speed_time);
    }

    if (!request.user_agent.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, request.user_agent.c_str());
    } else {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.88.1");
    }

    // Enable automatic gzip/deflate decompression
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, br");

    // Execute
    CURLcode res = curl_easy_perform(curl);

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    ReleaseHandle(request.url, curl);

    response.curl_code = res;
    response.status_code = static_cast<int>(code);

    if (res != CURLE_OK) {
        response.error_msg = "CURL request failed: " + std::string(curl_easy_strerror(res));
        return response;
    }

    response.body = res_body;
    response.retry_after_seconds = ParseRetryAfter(res_header);
    return response;
}

HttpResponse HttpClient::Post(const HttpRequest& request) {
    HttpResponse response;

    CURL* curl = AcquireHandle(request.url);
    if (!curl) {
        response.error_msg = "Failed to initialize CURL handle";
        return response;
    }

    std::string res_header;
    std::string res_body;
    const bool is_streaming = request.write_data != nullptr;

    // Configure the request
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request.headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    // Default: collect body (blocking mode)
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res_body);

    // Override with user-provided callback for streaming
    if (is_streaming) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, request.write_data);
    } else {
        // Header collection only needed for blocking mode
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res_header);
    }

    if (request.low_speed_limit > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, request.low_speed_limit);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, request.low_speed_time);
    }

    if (!request.user_agent.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, request.user_agent.c_str());
    } else {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.88.1");
    }

    // Enable automatic gzip/deflate decompression
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, br");

    // Execute
    LOG_DEBUG("curl_easy_perform begin, req timeout_seconds {}", request.timeout_seconds);
    LOG_DEBUG("req payload {}", request.body);
    CURLcode res = curl_easy_perform(curl);
    LOG_DEBUG("curl_easy_perform end");

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    ReleaseHandle(request.url, curl);

    response.curl_code = res;
    response.status_code = static_cast<int>(code);

    if (res != CURLE_OK) {
        response.error_msg = "CURL request failed: " + std::string(curl_easy_strerror(res));
        LOG_ERROR("{}", response.error_msg);
        return response;
    }

    if(!is_streaming) LOG_DEBUG("res_body = {}", res_body);
    response.body = res_body;
    response.retry_after_seconds = ParseRetryAfter(res_header);
    return response;
}

HttpResponse HttpClient::Post(const std::string& url,
                              const std::string& body,
                              struct curl_slist* headers,
                              long timeout_seconds) {
    HttpRequest request;
    request.url = url;
    request.body = body;
    request.headers = headers;
    request.timeout_seconds = timeout_seconds;
    return Post(request);
}

void SseStreamHandler::OnLine(const std::string& line) {
    // Remove trailing CR if present
    std::string cleaned = line;
    if (!cleaned.empty() && cleaned.back() == '\r') {
        cleaned.pop_back();
    }

    if (cleaned.empty()) return;

    // Parse SSE format
    if (cleaned.substr(0, 6) == "event:") {
        current_event = cleaned.substr(6);
        if (!current_event.empty() && current_event[0] == ' ') {
            current_event = current_event.substr(1);
        }
        return;
    }

    if (cleaned.substr(0, 5) == "data:") {
        current_data = cleaned.substr(5);
        if (!current_data.empty() && current_data[0] == ' ') {
            current_data = current_data.substr(1);
        }
        // exp. event:content_block_delta，data:{"delta":{"type":"text_delta","text":"Hello"}}
        OnEvent(current_event, current_data);
        current_event.clear();
        current_data.clear();
        return;
    }

    // Non-SSE line — check for JSON error response
    if (!cleaned.empty() && cleaned[0] == '{') {
        try {
            auto j = nlohmann::json::parse(cleaned);
            if (j.contains("error")) {
                error_msg = j["error"].is_string() ? j["error"].get<std::string>()
                                                   : j["error"].dump();
                LOG_ERROR("SSE stream API error: {}", error_msg);
            }
        } catch (...) {
            error_msg = "SSE stream API unkown error: " + cleaned;
            LOG_ERROR("SSE stream API unkown error: {}", cleaned);
        }
    }
}

void SseStreamHandler::OnEvent(const std::string& /*event_type*/, const std::string& /*data*/) {
    // Default: do nothing
}

// ============================================================================
// Callback implementations
// ============================================================================

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append(static_cast<char*>(contents), total);
    return total;
}

size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    if (userdata) {
        std::string* res_header = static_cast<std::string*>(userdata);
        res_header->append(static_cast<char*>(buffer), size * nitems);
    }
    return size * nitems;
}

size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* handler = static_cast<StreamHandler*>(userp);
    size_t total = size * nmemb;
    std::string chunk(static_cast<char*>(contents), total);

    // Process line by line
    std::string& buffer = handler->Buffer();
    buffer += chunk;

    LOG_DEBUG("res StreamCallback = {}", buffer);

    size_t pos;
    while ((pos = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);
        handler->OnLine(line);
    }

    return total;
}

}  // namespace prosophor

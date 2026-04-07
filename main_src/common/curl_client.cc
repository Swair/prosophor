// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/curl_client.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <sstream>

namespace aicode {

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
        curl_slist_free_all(static_cast<struct curl_slist*>(list_));
    }
}

void HeaderList::append(const char* str) {
    list_ = curl_slist_append(static_cast<struct curl_slist*>(list_), str);
}

void HeaderList::clear() {
    if (list_) {
        curl_slist_free_all(static_cast<struct curl_slist*>(list_));
        list_ = nullptr;
    }
}

// ============================================================================
// HttpClient implementation
// ============================================================================

HttpResponse HttpClient::Get(const HttpRequest& request) {
    HttpResponse response;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize CURL handle";
        return response;
    }

    std::string res_header;
    std::string res_body;
    struct curl_slist* headers = static_cast<struct curl_slist*>(request.headers);

    // Configure the request for GET
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout_seconds);

    // Default callbacks for blocking request
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res_header);

    if (request.low_speed_limit > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, request.low_speed_limit);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, request.low_speed_time);
    }

    if (!request.user_agent.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, request.user_agent.c_str());
    } else {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.88.1");
    }

    // Execute
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = "CURL request failed: " + std::string(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return response;
    }

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    response.status_code = static_cast<int>(code);
    response.body = res_body;
    response.retry_after_seconds = ParseRetryAfter(res_header);

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return response;
}

HttpResponse HttpClient::Post(const HttpRequest& request) {
    HttpResponse response;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize CURL handle";
        return response;
    }

    std::string res_header;
    std::string res_body;
    struct curl_slist* headers = static_cast<struct curl_slist*>(request.headers);

    // Configure the request
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout_seconds);

    // Default callbacks for blocking request
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res_header);

    if (request.low_speed_limit > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, request.low_speed_limit);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, request.low_speed_time);
    }

    if (!request.user_agent.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, request.user_agent.c_str());
    } else {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.88.1");
    }

    // Execute
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = "CURL request failed: " + std::string(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return response;
    }

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    response.status_code = static_cast<int>(code);
    response.body = res_body;
    response.retry_after_seconds = ParseRetryAfter(res_header);

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return response;
}

HttpResponse HttpClient::Post(const std::string& url,
                              const std::string& post_data,
                              void* headers,
                              long timeout_seconds) {
    HttpRequest request;
    request.url = url;
    request.post_data = post_data;
    request.headers = headers;
    request.timeout_seconds = timeout_seconds;
    return Post(request);
}

// ============================================================================
// StreamClient implementation
// ============================================================================

void StreamClient::Post(const StreamRequest& request) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL handle");
    }

    struct curl_slist* headers = static_cast<struct curl_slist*>(request.headers);

    // Configure the request
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout_seconds);

    // Use provided streaming callback
    if (request.write_function) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, request.write_function);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, request.write_data);
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

    // Execute
    CURLcode res = curl_easy_perform(curl);

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (res != CURLE_OK) {
        throw std::runtime_error("CURL streaming request failed: " + std::string(curl_easy_strerror(res)));
    }

    if (code >= 400) {
        throw std::runtime_error("HTTP error: " + std::to_string(code));
    }
}

// ============================================================================
// StreamHandler implementation
// ============================================================================

void StreamHandler::OnEvent(const std::string& /*event_type*/, const std::string& /*data*/) {
    // Default: do nothing
}

void StreamHandler::OnStreamEnd() {
    // Default: do nothing
}

// ============================================================================
// SseStreamHandler implementation
// ============================================================================

void SseStreamHandler::OnLine(const std::string& line) {
    // Remove trailing CR if present
    std::string cleaned = line;
    if (!cleaned.empty() && cleaned.back() == '\r') {
        cleaned.pop_back();
    }

    if (cleaned.empty()) return;

    // Parse SSE format
    if (cleaned.substr(0, 6) == "event:") {
        current_event_ = cleaned.substr(6);
        if (!current_event_.empty() && current_event_[0] == ' ') {
            current_event_ = current_event_.substr(1);
        }
        return;
    }

    if (cleaned.substr(0, 5) == "data:") {
        current_data_ = cleaned.substr(5);
        if (!current_data_.empty() && current_data_[0] == ' ') {
            current_data_ = current_data_.substr(1);
        }
        OnEvent(current_event_, current_data_);
        current_event_.clear();
        current_data_.clear();
    }
}

void SseStreamHandler::OnEvent(const std::string& event_type, const std::string& data) {
    if (data_callback_) {
        data_callback_(event_type, data);
    }
}

// ============================================================================
// Callback implementations
// ============================================================================

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
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

    size_t pos;
    while ((pos = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);
        handler->OnLine(line);
    }

    return total;
}

}  // namespace aicode

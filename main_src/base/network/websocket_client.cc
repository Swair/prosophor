// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "network/websocket_client.h"

#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "common/log_wrapper.h"

namespace prosophor {

// Apply proxy settings from environment variables (https_proxy, http_proxy, no_proxy)
// This is needed because libcurl on Windows doesn't reliably read lowercase env vars.
static void ApplyProxyFromEnv(CURL* curl) {
    const char* proxy = nullptr;
    // Try https_proxy first for wss:// URLs
    (proxy = std::getenv("HTTPS_PROXY"))   ||
    (proxy = std::getenv("https_proxy"))   ||
    (proxy = std::getenv("HTTP_PROXY"))    ||
    (proxy = std::getenv("http_proxy"));
    if (proxy && proxy[0]) {
        LOG_DEBUG("WebSocket: using proxy {}", proxy);
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
        if (const char* no_proxy = std::getenv("NO_PROXY")) {
            curl_easy_setopt(curl, CURLOPT_NOPROXY, no_proxy);
        } else if (const char* no_proxy_lower = std::getenv("no_proxy")) {
            curl_easy_setopt(curl, CURLOPT_NOPROXY, no_proxy_lower);
        }
    } else {
        LOG_DEBUG("WebSocket: no proxy set, connecting directly");
    }
}

WebSocketClient::WebSocketClient() = default;

WebSocketClient::~WebSocketClient() {
    Close();
}

WebSocketClient::WebSocketClient(WebSocketClient&& other) noexcept
    : curl_(other.curl_),
      resp_headers_(other.resp_headers_),
      error_(std::move(other.error_)) {
    other.curl_ = nullptr;
    other.resp_headers_ = nullptr;
    std::memcpy(errbuf_, other.errbuf_, sizeof(errbuf_));
}

WebSocketClient& WebSocketClient::operator=(WebSocketClient&& other) noexcept {
    if (this != &other) {
        Close();
        curl_ = other.curl_;
        resp_headers_ = other.resp_headers_;
        error_ = std::move(other.error_);
        std::memcpy(errbuf_, other.errbuf_, sizeof(errbuf_));
        other.curl_ = nullptr;
        other.resp_headers_ = nullptr;
    }
    return *this;
}

bool WebSocketClient::Connect(const std::string& url, struct curl_slist* headers) {
    Close();

    curl_ = curl_easy_init();
    if (!curl_) {
        error_ = "curl_easy_init failed";
        LOG_ERROR("WebSocket: {}", error_);
        return false;
    }

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);
    ApplyProxyFromEnv(curl_);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl_, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, errbuf_);
    if (headers) {
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    }
    errbuf_[0] = 0;

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        error_ = std::string(curl_easy_strerror(res));
        LOG_ERROR("WebSocket: connect failed: {} (errbuf: {})", error_, errbuf_);
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
        return false;
    }

    LOG_DEBUG("WebSocket: connected to {}", url);
    return true;
}

bool WebSocketClient::Send(const uint8_t* data, size_t len, bool binary) {
    if (!curl_) {
        error_ = "not connected";
        return false;
    }
    size_t sent = 0;
    CURLcode res = curl_ws_send(curl_, data, len, &sent, 0,
                                 binary ? CURLWS_BINARY : CURLWS_TEXT);
    if (res != CURLE_OK) {
        error_ = std::string(curl_easy_strerror(res));
        LOG_ERROR("WebSocket: send failed: {} (errbuf: {})", error_, errbuf_);
        return false;
    }
    return true;
}

bool WebSocketClient::SendText(const std::string& msg) {
    return Send(reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), false);
}

CURLcode WebSocketClient::Recv(std::vector<uint8_t>& out, int* flags,
                               int timeout_ms, curl_off_t* bytesleft) {
    if (!curl_) return CURLE_UNSUPPORTED_PROTOCOL;

    constexpr size_t kBufSize = 16384;
    std::vector<uint8_t> buf(kBufSize);

    int waited = 0;
    constexpr int kSleepMs = 50;
    while (waited < timeout_ms) {
        size_t received = 0;
        const struct curl_ws_frame* meta = nullptr;
        CURLcode res = curl_ws_recv(curl_, buf.data(), buf.size(), &received, &meta);
        if (res == CURLE_AGAIN) {
            waited += kSleepMs;
            if (waited < timeout_ms) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
            }
            continue;
        }
        if (res != CURLE_OK) {
            error_ = std::string(curl_easy_strerror(res));
            return res;
        }
        if (received == 0) continue;

        out.assign(buf.data(), buf.data() + received);
        if (flags && meta) {
            *flags = meta->flags;
        }
        if (bytesleft && meta) {
            *bytesleft = meta->bytesleft;
        }
        return CURLE_OK;
    }

    return CURLE_AGAIN;
}

void WebSocketClient::Close() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

}  // namespace prosophor

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <curl/curl.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct curl_slist;

namespace prosophor {

/// RAII wrapper around libcurl WebSocket (CURLOPT_CONNECT_ONLY=2L).
///
/// Usage:
///   WebSocketClient ws;
///   if (!ws.Connect(url, headers)) return;
///   ws.Send(text_data, false);
///   auto [ok, data, flags] = ws.Recv(timeout_ms);
class WebSocketClient {
   public:
    WebSocketClient();
    ~WebSocketClient();

    // Move-only
    WebSocketClient(WebSocketClient&& other) noexcept;
    WebSocketClient& operator=(WebSocketClient&& other) noexcept;
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    /// Connect to a WebSocket URL (wss:// or ws://).
    /// @param url        The WebSocket endpoint URL.
    /// @param headers    Optional HTTP headers for the upgrade request (may be null).
    /// @return true on success, false on failure (call last_error() for details).
    bool Connect(const std::string& url, struct curl_slist* headers = nullptr);

    /// Send a TEXT or BINARY frame.
    /// @param data     Pointer to the data to send.
    /// @param len      Data length in bytes.
    /// @param binary   true = CURLWS_BINARY, false = CURLWS_TEXT.
    /// @return true on success.
    bool Send(const uint8_t* data, size_t len, bool binary);

    /// Convenience: send a std::string as TEXT.
    bool SendText(const std::string& msg);

    /// Receive a frame with retry on CURLE_AGAIN.
    /// @param out       Receives the frame payload.
    /// @param flags     Receives the frame type flags (CURLWS_BINARY, CURLWS_TEXT, etc.).
    /// @param timeout_ms  Max time to wait for data (0 = no wait, single attempt).
    /// @param bytesleft  Optional: receives the number of payload bytes still
    ///                   pending in the current frame; 0 means the chunk just
    ///                   received is the end of the frame/message.
    /// @return CURLE_OK on success, CURLE_AGAIN if no data within timeout,
    ///         or another CURLcode on error.
    CURLcode Recv(std::vector<uint8_t>& out, int* flags, int timeout_ms = 5000,
                  curl_off_t* bytesleft = nullptr);

    /// Disconnect and release the curl handle.
    void Close();

    /// Check if the connection is active.
    bool IsConnected() const { return curl_ != nullptr; }

    /// Get the last error message.
    const std::string& last_error() const { return error_; }

   private:
    CURL* curl_ = nullptr;
    struct curl_slist* resp_headers_ = nullptr;
    char errbuf_[CURL_ERROR_SIZE] = {};
    std::string error_;
};

}  // namespace prosophor

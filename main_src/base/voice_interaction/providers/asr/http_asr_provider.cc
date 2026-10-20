// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/asr/http_asr_provider.h"
#include "common/log_wrapper.h"
#include "network/curl_client.h"

#include <cstring>
#include <vector>

namespace prosophor {

HttpAsrProvider::HttpAsrProvider(std::string server_url)
    : server_url_(std::move(server_url)) {
    // Strip trailing slash
    if (!server_url_.empty() && server_url_.back() == '/') {
        server_url_.pop_back();
    }
}

AsrResponse HttpAsrProvider::AsrProcess(const AsrRequest& request) {
    AsrResponse result;
    if (server_url_.empty()) {
        result.error_msg = "ASR server_url 未配置";
        return result;
    }
    if (request.pcm == nullptr || request.sample_size == 0) {
        result.error_msg = "empty PCM";
        return result;
    }

    std::string url = server_url_ + "/v1/transcribe";

    HttpRequest req;
    req.url = url;
    req.body.assign(reinterpret_cast<const char*>(request.pcm),
                    request.sample_size * sizeof(int16_t));
    req.timeout_seconds = 60;

    // Set content type for raw PCM
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    req.headers = headers;

    auto resp = HttpClient::Instance().Post(req);
    if (headers) curl_slist_free_all(headers);

    if (!resp.success()) {
        LOG_ERROR("HttpAsrProvider: HTTP {}: {}", resp.status_code, resp.error_msg);
        result.error_msg = "HTTP " + std::to_string(resp.status_code) + ": " + resp.error_msg;
        return result;
    }

    // Parse JSON response
    try {
        const auto j = nlohmann::json::parse(resp.body);
        if (!j.contains("text") || !j["text"].is_string()) {
            result.error_msg = "JSON response missing string field 'text'";
            return result;
        }
        result.text = j["text"].get<std::string>();
        if (j.contains("confidence") && j["confidence"].is_number()) {
            result.confidence = j["confidence"].get<float>();
        }
        result.success = true;   // 后端调用成功；空 text = 未识别到语音
        LOG_INFO("HttpAsrProvider: recognized {} chars", result.text.size());
    } catch (const std::exception& e) {
        LOG_ERROR("HttpAsrProvider: JSON parse failed: {}", e.what());
        result.error_msg = std::string("JSON parse failed: ") + e.what();
    }
    return result;
}

}  // namespace prosophor

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/llm_provider.h"

#include "managers/token_tracker.h"
#include "common/curl_client.h"

namespace prosophor {

// Token usage tracking
void RecordTokenUsage(const std::string& model, const TokenUsageSchema& usage) {
    TokenTracker::GetInstance().RecordUsage(model, usage.prompt_tokens, usage.completion_tokens);
}

HttpResponse LLMProvider::ExecuteStream(const ChatRequest& request,
    void* stream_handler,
    int default_timeout) const {
    HttpRequest stream_req;
    stream_req.url = request.base_url;
    stream_req.post_data = Serialize(request);
    stream_req.timeout_seconds = request.timeout > 0 ? request.timeout : default_timeout;
    stream_req.low_speed_limit = 1;
    stream_req.low_speed_time = 60;

    HeaderList headers = CreateHeaders(request);
    stream_req.headers = headers.get();
    stream_req.write_data = stream_handler;

    return HttpClient::Post(stream_req);
}

ChatResponse LLMProvider::Chat(const ChatRequest& request) {
    HttpRequest http_request;
    http_request.url = request.base_url;
    http_request.timeout_seconds = request.timeout > 0 ? request.timeout : 60;

    HeaderList headers = CreateHeaders(request);
    http_request.headers = headers.get();

    PrintRequestLog(request);

    http_request.post_data = Serialize(request);

    HttpResponse http_response = HttpClient::Post(http_request);

    LOG_DEBUG("=== Received response ===");

    if (http_response.failed()) {
        std::string error_msg = http_response.error.empty() ? http_response.body : http_response.error;
        LOG_ERROR("{} API HTTP {}: {}", GetProviderName(), http_response.status_code, error_msg.substr(0, 256));
        throw std::runtime_error(GetProviderName() + " API error (HTTP " +
                                 std::to_string(http_response.status_code) + "): " +
                                 error_msg);
    }

    ChatResponse response = Deserialize(http_response.body);

    if (response.usage.total_tokens > 0) {
        RecordTokenUsage(request.model, response.usage);
        LOG_INFO("Token usage: {} prompt, {} completion, {} total",
                 response.usage.prompt_tokens, response.usage.completion_tokens,
                 response.usage.total_tokens);
    }

    return response;
}

}  // namespace prosophor

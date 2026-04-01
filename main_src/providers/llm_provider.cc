#include "llm_provider.h"

#include "managers/token_tracker.h"

namespace aicode {

// Internal helper functions for Qwen serialization (used by QwenProvider)
std::pair<std::string, nlohmann::json> SerializeMessagesToAnthropic(
    const std::vector<MessageSchema>& messages);
int ThinkingBudgetTokens(const std::string& level);
void ApplyThinkingParams(nlohmann::json& payload_json,
     const ChatRequest& request);

// Token usage tracking
void RecordTokenUsage(const std::string& model, const TokenUsageSchema& usage) {
    TokenTracker::GetInstance().RecordUsage(model, usage.prompt_tokens, usage.completion_tokens);
}

}  // namespace aicode

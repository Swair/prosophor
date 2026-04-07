#include "llm_provider.h"

#include "managers/token_tracker.h"

namespace aicode {

// Token usage tracking
void RecordTokenUsage(const std::string& model, const TokenUsageSchema& usage) {
    TokenTracker::GetInstance().RecordUsage(model, usage.prompt_tokens, usage.completion_tokens);
}

}  // namespace aicode

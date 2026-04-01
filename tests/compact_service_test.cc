// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "core/compact_service.h"
#include "providers/llm_provider.h"

namespace aicode {

class CompactServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_ = &CompactService::GetInstance();
        config_ = CompactConfig::Default();
        config_.max_messages = 10;
        config_.keep_recent = 3;
        config_.max_tokens = 1000;
        service_->SetConfig(config_);
    }

    void TearDown() override {
        // Reset to default
        service_->SetConfig(CompactConfig::Default());
    }

    CompactService* service_;
    CompactConfig config_;
};

TEST_F(CompactServiceTest, EstimateTokens) {
    std::vector<MessageSchema> messages;

    // Empty messages should have 0 tokens
    EXPECT_EQ(service_->EstimateTokens(messages), 0);

    // Add a simple text message
    MessageSchema msg;
    msg.role = "user";
    msg.AddTextContent("Hello, world!");

    messages.push_back(msg);

    // Should estimate some tokens for the text
    int tokens = service_->EstimateTokens(messages);
    EXPECT_GT(tokens, 0);
    EXPECT_LT(tokens, 100);  // Should be reasonable
}

TEST_F(CompactServiceTest, NeedsCompaction_MessageCount) {
    std::vector<MessageSchema> messages;

    // Create messages under the limit
    for (int i = 0; i < 5; i++) {
        MessageSchema msg;
        msg.role = "user";
        msg.AddTextContent("MessageSchema " + std::to_string(i));
        messages.push_back(msg);
    }

    // Should not need compaction (5 < 10)
    EXPECT_FALSE(service_->NeedsCompaction(messages));

    // Add more messages to exceed limit
    for (int i = 5; i < 15; i++) {
        MessageSchema msg;
        msg.role = "user";
        msg.AddTextContent("MessageSchema " + std::to_string(i));
        messages.push_back(msg);
    }

    // Should need compaction (15 > 10)
    EXPECT_TRUE(service_->NeedsCompaction(messages));
}

TEST_F(CompactServiceTest, NeedsCompaction_TokenCount) {
    std::vector<MessageSchema> messages;

    // Create a message with lots of content
    MessageSchema msg;
    msg.role = "user";
    std::string large_content(5000, 'x');  // 5000 characters
    msg.AddTextContent(large_content);
    messages.push_back(msg);

    // Should need compaction due to token count
    EXPECT_TRUE(service_->NeedsCompaction(messages));
}

TEST_F(CompactServiceTest, KeepRecentMessages) {
    std::vector<MessageSchema> messages;

    // Create 10 messages
    for (int i = 0; i < 10; i++) {
        MessageSchema msg;
        msg.role = "user";
        msg.AddTextContent("MessageSchema " + std::to_string(i));
        messages.push_back(msg);
    }

    // Keep only last 3
    auto recent = service_->KeepRecentMessages(messages, 3);

    EXPECT_EQ(recent.size(), 3u);
    EXPECT_EQ(recent[0].text(), "MessageSchema 7");
    EXPECT_EQ(recent[1].text(), "MessageSchema 8");
    EXPECT_EQ(recent[2].text(), "MessageSchema 9");
}

TEST_F(CompactServiceTest, Compact_EmptyMessages) {
    std::vector<MessageSchema> messages;

    auto result = service_->Compact(messages, nullptr);

    EXPECT_EQ(result.messages_removed, 0);
    EXPECT_EQ(result.tokens_saved, 0);
    EXPECT_TRUE(result.kept_messages.empty());
}

TEST_F(CompactServiceTest, Compact_BelowThreshold) {
    std::vector<MessageSchema> messages;

    // Create messages below threshold
    for (int i = 0; i < 5; i++) {
        MessageSchema msg;
        msg.role = "user";
        msg.AddTextContent("MessageSchema " + std::to_string(i));
        messages.push_back(msg);
    }

    auto result = service_->Compact(messages, nullptr);

    // Should not compact when below threshold
    EXPECT_EQ(result.messages_removed, 0);
}

TEST_F(CompactServiceTest, SummaryGeneration) {
    std::vector<MessageSchema> messages;

    // Create enough messages to exceed the threshold (max_messages=10, keep_recent=3)
    for (int i = 0; i < 15; i++) {
        messages.emplace_back("user", "Question " + std::to_string(i));
        messages.emplace_back("assistant", "Answer " + std::to_string(i));
    }

    // Mock LLM callback
    auto llm_callback = [](const std::string& prompt) -> std::string {
        (void)prompt;
        return "Summary: User asked multiple questions and received answers.";
    };

    auto result = service_->Compact(messages, llm_callback);

    // Should generate a summary
    EXPECT_FALSE(result.summary.empty());
    EXPECT_GT(result.summary.length(), 10u);
    EXPECT_GT(result.messages_removed, 0);
}

}  // namespace aicode

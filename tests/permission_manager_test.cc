// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "managers/permission_manager.h"

namespace aicode {

class PermissionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = &PermissionManager::GetInstance();
        manager_->ClearRules();
        manager_->SetMode("default");
    }

    void TearDown() override {
        manager_->ClearRules();
    }

    PermissionManager* manager_;
};

TEST_F(PermissionManagerTest, SingletonInstance) {
    auto* instance1 = &PermissionManager::GetInstance();
    auto* instance2 = &PermissionManager::GetInstance();
    EXPECT_EQ(instance1, instance2);
}

TEST_F(PermissionManagerTest, DefaultMode) {
    EXPECT_EQ(manager_->GetMode(), "default");

    manager_->SetMode("auto");
    EXPECT_EQ(manager_->GetMode(), "auto");

    manager_->SetMode("bypass");
    EXPECT_EQ(manager_->GetMode(), "bypass");

    manager_->SetMode("default");
    EXPECT_EQ(manager_->GetMode(), "default");
}

TEST_F(PermissionManagerTest, BypassModeAllowsAll) {
    manager_->SetMode("bypass");

    nlohmann::json input = nlohmann::json::object();
    auto result = manager_->CheckPermission("bash", input);

    EXPECT_EQ(result.level, PermissionLevel::Allow);
}

TEST_F(PermissionManagerTest, AutoModeAllowsSafeCommands) {
    manager_->SetMode("auto");

    nlohmann::json input = nlohmann::json::object();
    input["command"] = "ls -la";

    auto result = manager_->CheckPermission("bash", input);
    EXPECT_EQ(result.level, PermissionLevel::Allow);
}

TEST_F(PermissionManagerTest, AutoModeDeniesDangerousCommands) {
    manager_->SetMode("auto");

    std::vector<std::string> dangerous_cmds = {
        "rm -rf /",
        "sudo rm -rf",
        "mkfs.ext4 /dev/sda",
        "dd if=/dev/zero"
    };

    for (const auto& cmd : dangerous_cmds) {
        nlohmann::json input = nlohmann::json::object();
        input["command"] = cmd;

        auto result = manager_->CheckPermission("bash", input);
        EXPECT_EQ(result.level, PermissionLevel::Deny) << "Command: " << cmd;
    }
}

TEST_F(PermissionManagerTest, AllowRules) {
    PermissionRule rule;
    rule.tool_name = "read_file";
    rule.default_level = PermissionLevel::Allow;
    manager_->AddAllowRule(rule);

    nlohmann::json input = nlohmann::json::object();
    input["path"] = "test.txt";

    auto result = manager_->CheckPermission("read_file", input);
    EXPECT_EQ(result.level, PermissionLevel::Allow);
}

TEST_F(PermissionManagerTest, DenyRules) {
    PermissionRule rule;
    rule.tool_name = "bash";
    rule.command_pattern = "rm *";
    rule.default_level = PermissionLevel::Deny;
    manager_->AddDenyRule(rule);

    nlohmann::json input = nlohmann::json::object();
    input["command"] = "rm -rf test";

    auto result = manager_->CheckPermission("bash", input);
    EXPECT_EQ(result.level, PermissionLevel::Deny);
}

TEST_F(PermissionManagerTest, AskRules) {
    PermissionRule rule;
    rule.tool_name = "write_file";
    rule.default_level = PermissionLevel::Ask;
    manager_->AddAskRule(rule);

    nlohmann::json input = nlohmann::json::object();
    input["path"] = "test.txt";

    auto result = manager_->CheckPermission("write_file", input);
    EXPECT_EQ(result.level, PermissionLevel::Ask);
}

TEST_F(PermissionManagerTest, PatternMatching_Exact) {
    PermissionRule rule;
    rule.tool_name = "bash";
    rule.command_pattern = "git status";
    rule.default_level = PermissionLevel::Allow;
    manager_->AddAllowRule(rule);

    // Exact match should work
    nlohmann::json input = nlohmann::json::object();
    input["command"] = "git status";

    auto result = manager_->CheckPermission("bash", input);
    EXPECT_EQ(result.level, PermissionLevel::Allow);
}

TEST_F(PermissionManagerTest, PatternMatching_Wildcard) {
    PermissionRule rule;
    rule.tool_name = "bash";
    rule.command_pattern = "git *";
    rule.default_level = PermissionLevel::Allow;
    manager_->AddAllowRule(rule);

    std::vector<std::string> git_cmds = {
        "git status",
        "git diff",
        "git log",
        "git commit -m 'test'"
    };

    for (const auto& cmd : git_cmds) {
        nlohmann::json input = nlohmann::json::object();
        input["command"] = cmd;

        auto result = manager_->CheckPermission("bash", input);
        EXPECT_EQ(result.level, PermissionLevel::Allow) << "Command: " << cmd;
    }
}

TEST_F(PermissionManagerTest, PathPatternMatching) {
    PermissionRule rule;
    rule.tool_name = "read_file";
    rule.path_pattern = "*.txt";
    rule.default_level = PermissionLevel::Allow;
    manager_->AddAllowRule(rule);

    nlohmann::json input = nlohmann::json::object();
    input["path"] = "test.txt";

    auto result = manager_->CheckPermission("read_file", input);
    EXPECT_EQ(result.level, PermissionLevel::Allow);
}

TEST_F(PermissionManagerTest, DenialTracking) {
    // Record multiple denials
    manager_->RecordDenial("bash", nlohmann::json::object());
    manager_->RecordDenial("bash", nlohmann::json::object());
    manager_->RecordDenial("bash", nlohmann::json::object());

    // Should track 3 denials
    EXPECT_EQ(manager_->GetDenialCount("bash"), 3);

    // Different tool should have 0 denials
    EXPECT_EQ(manager_->GetDenialCount("read_file"), 0);
}

TEST_F(PermissionManagerTest, FallbackToAllow) {
    // Record denials up to threshold
    for (int i = 0; i < 3; i++) {
        manager_->RecordDenial("test_tool", nlohmann::json::object());
    }

    // Should fallback to allow after 3 denials
    EXPECT_TRUE(manager_->ShouldFallbackToAllow("test_tool"));
}

TEST_F(PermissionManagerTest, GetRules) {
    PermissionRule allow_rule, deny_rule, ask_rule;
    allow_rule.tool_name = "read_file";
    deny_rule.tool_name = "bash";
    ask_rule.tool_name = "write_file";

    manager_->AddAllowRule(allow_rule);
    manager_->AddDenyRule(deny_rule);
    manager_->AddAskRule(ask_rule);

    EXPECT_EQ(manager_->GetAllowRules().size(), 1u);
    EXPECT_EQ(manager_->GetDenyRules().size(), 1u);
    EXPECT_EQ(manager_->GetAskRules().size(), 1u);
}

TEST_F(PermissionManagerTest, ClearRules) {
    PermissionRule rule;
    rule.tool_name = "test";
    manager_->AddAllowRule(rule);
    manager_->AddDenyRule(rule);
    manager_->AddAskRule(rule);

    manager_->RecordDenial("test", nlohmann::json::object());

    manager_->ClearRules();

    EXPECT_EQ(manager_->GetAllowRules().size(), 0u);
    EXPECT_EQ(manager_->GetDenyRules().size(), 0u);
    EXPECT_EQ(manager_->GetAskRules().size(), 0u);
    EXPECT_EQ(manager_->GetDenialCount("test"), 0);
}

}  // namespace aicode

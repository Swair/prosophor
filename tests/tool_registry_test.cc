// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <fstream>

#include "tools/tool_registry.h"

namespace aicode {

class ToolRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_ = std::make_unique<ToolRegistry>();
        registry_->RegisterBuiltinTools();
    }

    std::unique_ptr<ToolRegistry> registry_;
};

TEST_F(ToolRegistryTest, ToolRegistration) {
    auto schemas = registry_->GetToolSchemas();

    // Should have many built-in tools
    EXPECT_GT(schemas.size(), 10u);

    // Check for common tools
    std::vector<std::string> expected_tools = {
        "read_file", "write_file", "edit_file",
        "bash", "glob", "grep",
        "task_create", "task_list", "task_update",
        "cron_create", "cron_list"
    };

    for (const auto& expected : expected_tools) {
        bool found = false;
        for (const auto& schema : schemas) {
            if (schema.name == expected) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected tool not found: " << expected;
    }
}

TEST_F(ToolRegistryTest, HasTool) {
    EXPECT_TRUE(registry_->HasTool("read_file"));
    EXPECT_TRUE(registry_->HasTool("bash"));
    EXPECT_TRUE(registry_->HasTool("task_create"));
    EXPECT_FALSE(registry_->HasTool("nonexistent_tool"));
}

TEST_F(ToolRegistryTest, ToolSchemaStructure) {
    auto schemas = registry_->GetToolSchemas();

    for (const auto& schema : schemas) {
        EXPECT_FALSE(schema.name.empty());
        EXPECT_FALSE(schema.description.empty());
        EXPECT_TRUE(schema.input_schema.is_object());
    }
}

TEST_F(ToolRegistryTest, ExecuteReadFile) {
    // Create a test file
    std::string test_path = "/tmp/aicode_test.txt";
    std::ofstream ofs(test_path);
    ofs << "Hello, World!";
    ofs.close();

    nlohmann::json params = nlohmann::json::object();
    params["path"] = test_path;

    std::string result = registry_->ExecuteTool("read_file", params);

    EXPECT_NE(result.find("Hello, World!"), std::string::npos);

    // Cleanup
    std::remove(test_path.c_str());
}

TEST_F(ToolRegistryTest, ExecuteReadFile_NotFound) {
    nlohmann::json params = nlohmann::json::object();
    params["path"] = "/nonexistent/path/file.txt";

    std::string result = registry_->ExecuteTool("read_file", params);

    EXPECT_NE(result.find("Error"), std::string::npos);
}

TEST_F(ToolRegistryTest, ExecuteBash_Echo) {
    nlohmann::json params = nlohmann::json::object();
    params["command"] = "echo 'Hello, World!'";

    std::string result = registry_->ExecuteTool("bash", params);

    EXPECT_NE(result.find("Hello, World!"), std::string::npos);
}

TEST_F(ToolRegistryTest, ExecuteBash_ExitCode) {
    nlohmann::json params = nlohmann::json::object();
    params["command"] = "exit 42";

    std::string result = registry_->ExecuteTool("bash", params);

    // Should contain error information
    EXPECT_NE(result.find("exit"), std::string::npos);
}

TEST_F(ToolRegistryTest, ExecuteTaskCreate) {
    nlohmann::json params = nlohmann::json::object();
    params["subject"] = "Test Task";
    params["description"] = "Test Description";

    std::string result = registry_->ExecuteTool("task_create", params);

    EXPECT_NE(result.find("\"success\""), std::string::npos);
    EXPECT_NE(result.find("\"task_id\""), std::string::npos);
}

TEST_F(ToolRegistryTest, ExecuteTaskList) {
    nlohmann::json params = nlohmann::json::object();
    std::string result = registry_->ExecuteTool("task_list", params);

    EXPECT_NE(result.find("\"tasks\""), std::string::npos);
    EXPECT_NE(result.find("\"count\""), std::string::npos);
}

TEST_F(ToolRegistryTest, ExecuteTaskUpdate) {
    // First create a task
    nlohmann::json create_params = nlohmann::json::object();
    create_params["subject"] = "Update Test";
    create_params["description"] = "To be updated";
    std::string create_result = registry_->ExecuteTool("task_create", create_params);

    nlohmann::json create_json = nlohmann::json::parse(create_result);
    std::string task_id = create_json["task_id"].get<std::string>();

    // Update the task
    nlohmann::json update_params = nlohmann::json::object();
    update_params["task_id"] = task_id;
    update_params["status"] = "in_progress";

    std::string result = registry_->ExecuteTool("task_update", update_params);

    EXPECT_NE(result.find("\"success\""), std::string::npos);
}

TEST_F(ToolRegistryTest, ExecuteGlob) {
    nlohmann::json params = nlohmann::json::object();
    params["pattern"] = "*.cc";
    params["path"] = "/tmp";

    std::string result = registry_->ExecuteTool("glob", params);

    // Should return JSON with files array
    EXPECT_NE(result.find("\"files\""), std::string::npos);
}

TEST_F(ToolRegistryTest, WorkspacePath) {
    std::string test_path = "/tmp/test_workspace";
    registry_->SetWorkspace(test_path);
    // Workspace path is set, verification would require internal access
}

}  // namespace aicode

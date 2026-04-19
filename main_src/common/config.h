// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

#include <nlohmann/json.hpp>

namespace aicode {

// Forward declarations
struct AgentConfig;
struct ModelCost;
struct ModelDefinition;
struct ProviderConfig;
struct ToolConfig;
struct SkillEntryConfig;
struct SkillsLoadConfig;
struct SkillsConfig;
struct SecurityConfig;
struct AiCodeConfig;

/// Cost information for a model
struct ModelCost {
    double input = 0;
    double output = 0;
    double cache_read = 0;
    double cache_write = 0;

    static ModelCost FromJson(const nlohmann::json& json);
};

/// Definition of an LLM model
struct ModelDefinition {
    std::string id;
    std::string name;
    bool reasoning = false;
    std::vector<std::string> input = {"text"};
    ModelCost cost;
    int context_window = 0;
    int max_tokens = 0;

    static ModelDefinition FromJson(const nlohmann::json& json);
};

/// Agent behavior configuration
struct AgentConfig {
    std::string name = "default";
    std::string model = "claude-sonnet-4-6";
    double temperature = 0.7;
    int max_tokens = 8192;
    int context_window = 128000;
    std::string thinking = "off";
    bool use_tools = true;
    bool enable_streaming = true;  // Whether to use streaming for responses

    // Auto-compaction
    bool auto_compact = true;
    int compact_max_messages = 100;
    int compact_keep_recent = 20;
    int compact_max_tokens = 100000;

    static AgentConfig FromJson(const nlohmann::json& json);
    int DynamicMaxIterations() const;
};

/// Configuration for an LLM provider
/// Format: provider_name: { apiKey, baseUrl, apiType, timeout, agents: { name: { model, ... } } }
struct ProviderConfig {
    std::string api_key;
    std::string base_url;
    std::string api_type;  // "anthropic-messages", "openai-completions"
    int timeout = 30;

    // Multiple agent configurations per provider
    std::unordered_map<std::string, AgentConfig> agents;

    std::vector<ModelDefinition> models;

    static ProviderConfig FromJson(const nlohmann::json& json);

    // Get default agent
    const AgentConfig& GetDefaultAgent() const;
};

/// Tool configuration settings
struct ToolConfig {
    bool enabled = true;
    std::vector<std::string> allowed_paths;
    std::vector<std::string> denied_paths;
    std::vector<std::string> allowed_cmds;
    std::vector<std::string> denied_cmds;
    int timeout = 60;

    static ToolConfig FromJson(const nlohmann::json& json);
};

/// Configuration for a single skill
struct SkillEntryConfig {
    bool enabled = true;

    static SkillEntryConfig FromJson(const nlohmann::json& json);
};

/// Configuration for loading skills
struct SkillsLoadConfig {
    std::vector<std::string> extra_dirs;

    static SkillsLoadConfig FromJson(const nlohmann::json& json);
};

/// Configuration for all skills
struct SkillsConfig {
    std::string path;
    std::vector<std::string> auto_approve;
    SkillsLoadConfig load;
    std::unordered_map<std::string, SkillEntryConfig> entries;
    nlohmann::json configs;

    static SkillsConfig FromJson(const nlohmann::json& json);
};

/// Security configuration settings
struct SecurityConfig {
    std::string permission_level = "auto";
    bool allow_local_execute = true;

    static SecurityConfig FromJson(const nlohmann::json& json) {
        SecurityConfig c;
        c.permission_level = json.value("permission_level", json.value("permissionLevel", "auto"));
        c.allow_local_execute = json.value("allow_local_execute", json.value("allowLocalExecute", true));
        return c;
    }
};

/// Top-level AiCode configuration
struct AiCodeConfig {
    std::string log_level = "info";
    std::string default_role = "default";  // Default role to use when not specified
    bool show_buddy = true;

    SecurityConfig security;
    std::unordered_map<std::string, ProviderConfig> providers;
    ToolConfig tools;
    SkillsConfig skills;

    /// Get singleton instance
    static AiCodeConfig& GetInstance();

    /// Get current agent config from default provider
    const AgentConfig& GetAgentConfig() const;

    /// Get provider config
    const ProviderConfig& GetProvider(const std::string& name = "anthropic") const;

    /// Load config from file
    static AiCodeConfig FromJson(const nlohmann::json& json);
    static AiCodeConfig LoadFromFile(const std::string& filepath);
    static std::string ExpandHome(const std::string& path);
    static std::string DefaultConfigPath();
    static std::filesystem::path BaseDir();
    static void CreateDefaultConfig(const std::string& filepath = DefaultConfigPath());

    /// Save config to file
    void SaveToFile(const std::string& filepath = DefaultConfigPath()) const;

    /// Convert to JSON
    nlohmann::json ToJson() const;

 private:
    static std::string config_path_override_;
    static AiCodeConfig* instance_;
};

}  // namespace aicode

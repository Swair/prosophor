// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "common/constants.h"
#include "common/log_wrapper.h"
#include "common/file_utils.h"
#include "managers/agent_role_loader.h"

namespace prosophor {

std::string ProsophorConfig::config_path_override_;
ProsophorConfig* ProsophorConfig::instance_ = nullptr;

ProsophorConfig& ProsophorConfig::GetInstance() {
    static ProsophorConfig instance;
    static bool initialized = false;

    if (!initialized) {
        std::string config_path = DefaultConfigPath();
        CreateDefaultConfig(config_path);

        try {
            instance = LoadFromFile(config_path);
        } catch (const std::exception& e) {
            LOG_WARN("Failed to load config, using defaults: {}", e.what());
        }
        initialized = true;
    }

    return instance;
}

const AgentConfig& ProsophorConfig::GetAgentConfig() const {
    // Load default role to get its provider and agent config
    std::string role_path = "config/.prosophor/roles/" + default_role + ".md";
    if (std::filesystem::exists(role_path)) {
        auto& loader = AgentRoleLoader::GetInstance();
        try {
            AgentRole role = loader.LoadRole(role_path);
            // Find the agent config from the role's provider
            auto prov_it = providers.find(role.provider_prot);
            if (prov_it != providers.end()) {
                auto& agent_map = prov_it->second.agents;
                // Try 1: role.model as key (agent name)
                auto agent_it = agent_map.find(role.model);
                if (agent_it == agent_map.end()) {
                    // Try 2: provider_name/model_name key
                    std::string full_key = role.provider_prot + "/" + role.model;
                    agent_it = agent_map.find(full_key);
                }
                if (agent_it != agent_map.end()) {
                    return agent_it->second;
                }
                // Fall back to provider's default agent
                return prov_it->second.GetDefaultAgent();
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to load default role '{}', using fallback: {}", default_role, e.what());
        }
    }

    // Fallback: use first provider's default agent
    if (!providers.empty()) {
        return providers.begin()->second.GetDefaultAgent();
    }

    static AgentConfig fallback_agent;
    return fallback_agent;
}

const ProviderConfig& ProsophorConfig::GetProvider(const std::string& name) const {
    auto it = providers.find(name);
    if (it != providers.end()) {
        return it->second;
    }
    static ProviderConfig default_provider;
    return default_provider;
}

const AgentConfig& ProviderConfig::GetDefaultAgent() const {
    auto it = agents.find("default");
    if (it != agents.end()) {
        return it->second;
    }
    static AgentConfig default_agent;
    return default_agent;
}

bool ProviderConfig::FindEntryForModel(const std::string& provider_name,
                                        const std::string& model,
                                        std::string& out_base_url,
                                        std::string& out_api_key,
                                        int& out_timeout) const {
    for (const auto& entry : entries) {
        auto it = entry.agents.find(model);
        if (it == entry.agents.end()) {
            // Also try searching by model name in agent.config.model
            for (const auto& [k, v] : entry.agents) {
                if (v.model == model) {
                    it = entry.agents.find(k);
                    break;
                }
            }
        }
        if (it != entry.agents.end()) {
            out_base_url = entry.base_url;
            out_api_key = entry.api_key;
            out_timeout = entry.timeout;
            return true;
        }
    }
    // Fallback: search by full key provider_name/model
    std::string full_key = provider_name + "/" + model;
    auto agent_it = agents.find(full_key);
    if (agent_it != agents.end()) {
        // Find matching entry
        for (const auto& entry : entries) {
            for (const auto& [k, v] : entry.agents) {
                if (v.model == model || k == model) {
                    out_base_url = entry.base_url;
                    out_api_key = entry.api_key;
                    out_timeout = entry.timeout;
                    return true;
                }
            }
        }
    }
    return false;
}

// Substitutes environment variables in the form ${VAR_NAME}
static std::string SubstituteEnvVars(const std::string& input) {
    static const std::regex env_re(R"(\$\{([^}]+)\})");
    std::string result;
    auto begin = std::sregex_iterator(input.begin(), input.end(), env_re);
    auto end = std::sregex_iterator();

    size_t last_pos = 0;
    for (auto it = begin; it != end; ++it) {
        auto& match = *it;
        result.append(input, last_pos, match.position() - last_pos);
        std::string var_name = match[1].str();
        const char* env_val = std::getenv(var_name.c_str());
        if (env_val) {
            result.append(env_val);
        }
        last_pos = match.position() + match.length();
    }
    result.append(input, last_pos, std::string::npos);
    return result;
}

// Strips comments and trailing commas from JSON5
static std::string StripJson5(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    size_t i = 0;
    const size_t len = input.size();

    while (i < len) {
        if (input[i] == '"') {
            out += '"';
            ++i;
            while (i < len && input[i] != '"') {
                if (input[i] == '\\' && i + 1 < len) {
                    out += input[i];
                    out += input[i + 1];
                    i += 2;
                } else {
                    out += input[i];
                    ++i;
                }
            }
            if (i < len) {
                out += '"';
                ++i;
            }
            continue;
        }

        if (i + 1 < len && input[i] == '/' && input[i + 1] == '/') {
            i += 2;
            while (i < len && input[i] != '\n') ++i;
            continue;
        }

        if (i + 1 < len && input[i] == '/' && input[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(input[i] == '*' && input[i + 1] == '/')) ++i;
            if (i + 1 < len) i += 2;
            continue;
        }

        out += input[i];
        ++i;
    }

    std::string result;
    result.reserve(out.size());
    for (size_t j = 0; j < out.size(); ++j) {
        if (out[j] == ',') {
            size_t k = j + 1;
            while (k < out.size() && (out[k] == ' ' || out[k] == '\t' ||
                                      out[k] == '\n' || out[k] == '\r')) {
                ++k;
            }
            if (k < out.size() && (out[k] == '}' || out[k] == ']')) {
                continue;
            }
        }
        result += out[j];
    }

    return result;
}

// Expands environment variables in JSON
static void ExpandEnvInJson(nlohmann::json& j) {
    if (j.is_string()) {
        auto& s = j.get_ref<std::string&>();
        if (s.find("${") != std::string::npos) {
            s = SubstituteEnvVars(s);
        }
    } else if (j.is_object()) {
        for (auto& [key, value] : j.items()) {
            ExpandEnvInJson(value);
        }
    } else if (j.is_array()) {
        for (auto& element : j) {
            ExpandEnvInJson(element);
        }
    }
}

AgentConfig AgentConfig::FromJson(const nlohmann::json& json) {
    AgentConfig config;
    config.name = json.value("name", "default");
    config.model = json.value("model", "claude-sonnet-4-6");
    config.temperature = json.value("temperature", kDefaultTemperature);
    config.max_tokens = json.value("max_tokens", json.value("maxTokens", kDefaultMaxTokens));
    config.context_window = json.value("context_window", json.value("contextWindow", kDefaultContextWindow));
    config.thinking = json.value("thinking", false);
    config.use_tools = json.value("use_tools", json.value("useTools", true));
    config.enable_streaming = json.value("enable_streaming", json.value("enableStreaming", true));
    config.auto_compact = json.value("auto_compact", json.value("autoCompact", true));
    config.compact_max_messages = json.value("compact_max_messages", json.value("compactMaxMessages", kDefaultCompactMaxMessages));
    config.compact_keep_recent = json.value("compact_keep_recent", json.value("compactKeepRecent", kDefaultCompactKeepRecent));
    config.compact_max_tokens = json.value("compact_max_tokens", json.value("compactMaxTokens", kDefaultCompactMaxTokens));
    return config;
}

ModelCost ModelCost::FromJson(const nlohmann::json& json) {
    ModelCost c;
    c.input = json.value("input", 0.0);
    c.output = json.value("output", 0.0);
    c.cache_read = json.value("cacheRead", json.value("cache_read", 0.0));
    c.cache_write = json.value("cacheWrite", json.value("cache_write", 0.0));
    return c;
}

ModelDefinition ModelDefinition::FromJson(const nlohmann::json& json) {
    ModelDefinition m;
    m.id = json.value("id", "");
    m.name = json.value("name", "");
    m.reasoning = json.value("reasoning", false);
    m.input = json.value("input", std::vector<std::string>{"text"});
    if (json.contains("cost") && json["cost"].is_object()) {
        m.cost = ModelCost::FromJson(json["cost"]);
    }
    m.context_window = json.value("contextWindow", json.value("context_window", 0));
    m.max_tokens = json.value("maxTokens", json.value("max_tokens", 0));
    return m;
}

ProviderConfig ProviderConfig::FromJson(const nlohmann::json& json) {
    ProviderConfig config;
    config.api_key = json.value("api_key", json.value("apiKey", ""));
    config.base_url = json.value("base_url", json.value("baseUrl", ""));
    config.timeout = json.value("timeout", kDefaultProviderTimeoutSec);

    // Parse agents - support both "agents" and "agent" field names
    const auto& agents_json_key = json.contains("agents") ? "agents" : "agent";
    if (json.contains(agents_json_key) && json[agents_json_key].is_object()) {
        const auto& agents_json = json[agents_json_key];
        for (const auto& [key, value] : agents_json.items()) {
            AgentConfig agent = AgentConfig::FromJson(value);
            agent.name = key;
            // Also support tools_use → use_tools
            if (json.contains("tools_use")) {
                agent.use_tools = json.value("tools_use", true);
            }
            config.agents[key] = agent;
        }
    }

    if (json.contains("models") && json["models"].is_array()) {
        for (const auto& m : json["models"]) {
            config.models.push_back(ModelDefinition::FromJson(m));
        }
    }
    return config;
}

LocalModelConfig LocalModelConfig::FromJson(const nlohmann::json& json) {
    LocalModelConfig config;
    config.model_path = json.value("model_path", "");
    config.port = json.value("port", 8080);
    config.n_gpu_layers = json.value("n_gpu_layers", json.value("nGpuLayers", -1));
    config.n_threads = json.value("n_threads", json.value("nThreads", 0));
    config.auto_start = json.value("auto_start", json.value("autoStart", true));
    config.start_timeout_ms = json.value("start_timeout_ms", json.value("startTimeoutMs", 60000));
    config.server_path = json.value("server_path", json.value("serverPath", ""));
    return config;
}

nlohmann::json LocalModelConfig::ToJson() const {
    nlohmann::json j;
    j["model_path"] = model_path;
    j["port"] = port;
    j["n_gpu_layers"] = n_gpu_layers;
    j["n_threads"] = n_threads;
    j["auto_start"] = auto_start;
    if (start_timeout_ms != 60000) j["start_timeout_ms"] = start_timeout_ms;
    if (!server_path.empty()) j["server_path"] = server_path;
    return j;
}

ToolConfig ToolConfig::FromJson(const nlohmann::json& json) {
    ToolConfig config;
    config.enabled = json.value("enabled", true);
    config.allowed_paths = json.value("allowed_paths", std::vector<std::string>{});
    config.denied_paths = json.value("denied_paths", std::vector<std::string>{});
    config.allowed_cmds = json.value("allowed_cmds", std::vector<std::string>{});
    config.denied_cmds = json.value("denied_cmds", std::vector<std::string>{});
    config.timeout = json.value("timeout", kDefaultToolTimeoutSec);
    return config;
}

SkillEntryConfig SkillEntryConfig::FromJson(const nlohmann::json& json) {
    SkillEntryConfig config;
    config.enabled = json.value("enabled", true);
    return config;
}

SkillsLoadConfig SkillsLoadConfig::FromJson(const nlohmann::json& json) {
    SkillsLoadConfig config;
    config.extra_dirs = json.value("extraDirs", std::vector<std::string>{});
    return config;
}

SkillsConfig SkillsConfig::FromJson(const nlohmann::json& json) {
    SkillsConfig config;
    config.path = json.value("path", "");
    config.auto_approve = json.value("auto_approve", json.value("autoApprove", std::vector<std::string>{}));
    if (json.contains("configs") && json["configs"].is_object()) {
        config.configs = json["configs"];
    }
    if (json.contains("load") && json["load"].is_object()) {
        config.load = SkillsLoadConfig::FromJson(json["load"]);
    }
    if (!config.path.empty() && config.load.extra_dirs.empty()) {
        config.load.extra_dirs.push_back(config.path);
    }
    if (json.contains("entries") && json["entries"].is_object()) {
        for (const auto& [key, value] : json["entries"].items()) {
            config.entries[key] = SkillEntryConfig::FromJson(value);
        }
    }
    return config;
}

ProsophorConfig ProsophorConfig::FromJson(const nlohmann::json& json) {
    nlohmann::json expanded = json;
    ExpandEnvInJson(expanded);

    ProsophorConfig config;
    config.log_level = json.value("log_level", json.value("logLevel", "info"));
    config.default_role = json.value("default_role", json.value("defaultRole", "default"));

    if (json.contains("providers") && json["providers"].is_object()) {
        for (const auto& [key, value] : json["providers"].items()) {
            if (value.is_array()) {
                // Array format: keep all entries, key agents by provider_name/model_name
                ProviderConfig merged_config;
                bool first = true;
                for (const auto& entry : value) {
                    ProviderConfig entry_config = ProviderConfig::FromJson(entry);

                    if (first) {
                        merged_config = entry_config;
                        first = false;
                    }

                    // Store entry for lookup by FindEntryForModel
                    ProviderEntryConfig e;
                    e.api_key = entry_config.api_key;
                    e.base_url = entry_config.base_url;
                    e.timeout = entry_config.timeout;
                    e.agents = entry_config.agents;
                    merged_config.entries.push_back(std::move(e));
                    // Key agents as provider_name/model_name
                    for (auto& [agent_name, agent_config] : entry_config.agents) {
                        std::string agent_key = key + "/" + agent_config.model;
                        merged_config.agents[agent_key] = agent_config;
                    }
                    // Merge models
                    for (auto& model : entry_config.models) {
                        merged_config.models.push_back(model);
                    }
                }
                config.providers[key] = merged_config;
            } else {
                // Object format: use directly
                config.providers[key] = ProviderConfig::FromJson(value);
            }
        }
    }
    if (json.contains("security") && json["security"].is_object()) {
        config.security = SecurityConfig::FromJson(json["security"]);
    }
    if (json.contains("tools") && json["tools"].is_object()) {
        config.tools = ToolConfig::FromJson(json["tools"]);
    }
    if (json.contains("skills") && json["skills"].is_object()) {
        config.skills = SkillsConfig::FromJson(json["skills"]);
    }
    if (json.contains("local_models") && json["local_models"].is_array()) {
        for (const auto& m : json["local_models"]) {
            config.local_models.push_back(LocalModelConfig::FromJson(m));
        }
    }
    return config;
}

ProsophorConfig ProsophorConfig::LoadFromFile(const std::string& filepath) {
    std::string expanded_path = ExpandHome(filepath);

    if (!std::filesystem::exists(expanded_path)) {
        throw std::runtime_error("Config file not found: " + expanded_path);
    }

    std::ifstream file(expanded_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + expanded_path);
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    std::string clean = StripJson5(content);
    nlohmann::json json = nlohmann::json::parse(clean);

    return FromJson(json);
}

std::string ProsophorConfig::ExpandHome(const std::string& path) {
    std::string expanded = path;
    if (expanded.size() >= 2 && expanded.substr(0, 2) == "~/") {
        std::string home = GetHomeDir();
        if (!home.empty()) {
            expanded = home + expanded.substr(1);
        }
    }
    return expanded;
}

std::string ProsophorConfig::DefaultConfigPath() {
    if (!config_path_override_.empty()) {
        return config_path_override_;
    }
    // Support environment variable override: export PROSOPHOR_CONFIG="/path/to/config.json"
    const char* env_path = std::getenv("PROSOPHOR_CONFIG");
    if (env_path != nullptr && env_path[0] != '\0') {
        return env_path;
    }
    // Cross-platform: use user home directory
    return ExpandHome("~/.prosophor/settings.json");
}

std::filesystem::path ProsophorConfig::BaseDir() {
    // Support environment variable override
    const char* env_path = std::getenv("PROSOPHOR_CONFIG");
    if (env_path != nullptr && env_path[0] != '\0') {
        return std::filesystem::path(env_path).parent_path();
    }
    // Cross-platform: use user home directory
    return ExpandHome("~/.prosophor");
}

// Creates a default config file with documentation comments
void ProsophorConfig::CreateDefaultConfig(const std::string& filepath) {
    std::string expanded_path = ExpandHome(filepath);

    // Create directory if it doesn't exist
    std::filesystem::path parent_dir = std::filesystem::path(expanded_path).parent_path();
    std::filesystem::create_directories(parent_dir);

    // Don't overwrite existing config
    if (std::filesystem::exists(expanded_path)) {
        return;
    }

    // Check if there's a demo config at config/.prosophor/settings.json
    std::string demo_config_path = "config/.prosophor/settings.json";
    if (std::filesystem::exists(demo_config_path)) {
        // Copy demo config to ~/.prosophor/settings.json
        try {
            std::filesystem::copy_file(demo_config_path, expanded_path,
                                        std::filesystem::copy_options::overwrite_existing);
            LOG_DEBUG("Created config from demo: {}", demo_config_path);
            return;
        } catch (const std::exception& e) {
            LOG_WARN("Failed to copy demo config: {}", e.what());
            // Fall through to create default config
        }
    }

    std::ofstream file(expanded_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create config file: " + expanded_path);
    }

    // Write config template with comments
    file << R"(// Prosophor Configuration File
// Path: ~/.prosophor/settings.json
//
// Structure:
//   providers.<provider_name>.agents.<agent_name> = { model, temperature, ... }
//   Roles are defined in config/.prosophor/roles/*.md
//
// Example:
//   providers.anthropic.agents.default.model = "qwen3.5-plus"
//   providers.deepseek.agents.pro.model = "deepseek-v4-pro"

{
  "default_role": "default",          // Default role to use when not specified
  "log_level": "info",

  // Provider configuration (array format supports multiple instances)
  "providers": {
    "anthropic": [
      {
        "api_key": "${ANTHROPIC_API_KEY}",
        "base_url": "https://api.anthropic.com",
        "timeout": 60,

        // Multiple agent configurations
        "agents": {
          "default": {
            "model": "claude-sonnet-4-6",
            "temperature": 0.7,
            "max_tokens": 8192,
            "context_window": 128000,
            "use_tools": true,
            "thinking": false,
            "enable_streaming": true
          }
        }
      }
    ],

    // DeepSeek (OpenAI-compatible protocol)
    "deepseek": [
      {
        "api_key": "${DEEPSEEK_API_KEY}",
        "base_url": "https://api.deepseek.com/chat/completions",
        "timeout": 60,
        "agents": {
          "default": {
            "model": "deepseek-chat",
            "temperature": 0.7,
            "max_tokens": 8192,
            "context_window": 128000,
            "use_tools": true
          }
        }
      }
    ]
  },

  "security": {
    "permission_level": "auto",
    "allow_local_execute": true
  },

  "tools": {
    "enabled": true,
    "timeout": 60
  },

  "skills": {
    "path": "./skills",
    "auto_approve": ["read_file", "grep"]
  }
}
)";

    file.close();
}

int AgentConfig::DynamicMaxIterations() const {
    if (context_window <= kContextWindow32K) return kMinMaxIterations;
    if (context_window >= kContextWindow200K) return kMaxMaxIterations;

    double ratio = static_cast<double>(context_window - kContextWindow32K) /
                   (kContextWindow200K - kContextWindow32K);
    return kMinMaxIterations +
           static_cast<int>(ratio * (kMaxMaxIterations - kMinMaxIterations));
}

// ==================== ProsophorConfig JSON Serialization ====================

nlohmann::json ProsophorConfig::ToJson() const {
    nlohmann::json json = nlohmann::json::object();

    json["log_level"] = log_level;
    json["default_role"] = default_role;

    // Serialize providers
    nlohmann::json providers_json = nlohmann::json::object();
    for (const auto& [name, config] : providers) {
        nlohmann::json provider_json = nlohmann::json::object();
        provider_json["api_key"] = config.api_key;
        provider_json["base_url"] = config.base_url;
        provider_json["timeout"] = config.timeout;

        // Serialize agents
        nlohmann::json agents_json = nlohmann::json::object();
        for (const auto& [agent_name, agent_config] : config.agents) {
            nlohmann::json agent_json = nlohmann::json::object();
            agent_json["model"] = agent_config.model;
            agent_json["temperature"] = agent_config.temperature;
            agent_json["max_tokens"] = agent_config.max_tokens;
            agent_json["context_window"] = agent_config.context_window;
            agent_json["enable_streaming"] = agent_config.enable_streaming;
            agents_json[agent_name] = agent_json;
        }
        provider_json["agents"] = agents_json;

        providers_json[name] = provider_json;
    }
    json["providers"] = providers_json;

    // Serialize security
    nlohmann::json security_json = nlohmann::json::object();
    security_json["permission_level"] = security.permission_level;
    security_json["allow_local_execute"] = security.allow_local_execute;
    json["security"] = security_json;

    // Serialize tools
    nlohmann::json tools_json = nlohmann::json::object();
    tools_json["enabled"] = tools.enabled;
    tools_json["timeout"] = tools.timeout;
    if (!tools.allowed_paths.empty()) tools_json["allowed_paths"] = tools.allowed_paths;
    if (!tools.denied_paths.empty()) tools_json["denied_paths"] = tools.denied_paths;
    if (!tools.allowed_cmds.empty()) tools_json["allowed_cmds"] = tools.allowed_cmds;
    if (!tools.denied_cmds.empty()) tools_json["denied_cmds"] = tools.denied_cmds;
    json["tools"] = tools_json;

    // Serialize local models
    if (!local_models.empty()) {
        nlohmann::json models_json = nlohmann::json::array();
        for (const auto& m : local_models) {
            models_json.push_back(m.ToJson());
        }
        json["local_models"] = models_json;
    }

    return json;
}

void ProsophorConfig::SaveToFile(const std::string& filepath) const {
    auto json = ToJson();
    prosophor::WriteJson(filepath, json, 2);
    LOG_DEBUG("Configuration saved to {}", filepath);
}

}  // namespace prosophor

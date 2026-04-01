// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/plugin_manager.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

#include "common/log_wrapper.h"
#include "common/constants.h"

namespace aicode {

Plugin Plugin::FromJson(const nlohmann::json& json, const std::string& path) {
    Plugin plugin;
    plugin.path = path;
    plugin.name = json.value("name", "");
    plugin.version = json.value("version", "1.0.0");
    plugin.description = json.value("description", "");
    plugin.enabled = json.value("enabled", true);
    plugin.config = json.value("config", nlohmann::json::object());

    if (json.contains("commands") && json["commands"].is_array()) {
        plugin.commands = json["commands"].get<std::vector<std::string>>();
    }
    if (json.contains("agents") && json["agents"].is_array()) {
        plugin.agents = json["agents"].get<std::vector<std::string>>();
    }
    if (json.contains("skills") && json["skills"].is_array()) {
        plugin.skills = json["skills"].get<std::vector<std::string>>();
    }

    return plugin;
}

nlohmann::json Plugin::ToJson() const {
    nlohmann::json json;
    json["name"] = name;
    json["version"] = version;
    json["description"] = description;
    json["path"] = path;
    json["enabled"] = enabled;
    json["commands"] = commands;
    json["agents"] = agents;
    json["skills"] = skills;
    json["config"] = config;
    return json;
}

PluginManager& PluginManager::GetInstance() {
    static PluginManager instance;
    return instance;
}

void PluginManager::Initialize(const std::string& plugins_dir) {
    plugins_dir_ = ExpandHome(plugins_dir);
    config_path_ = plugins_dir_ + "/plugins.json";

    LOG_INFO("PluginManager initialized: {}", plugins_dir_);

    LoadConfig();
    LoadAllPlugins();
}

std::string PluginManager::ExpandHome(const std::string& path) const {
    if (path.find("~") == 0) {
        const char* home = getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

std::string PluginManager::FindPluginManifest(const std::string& plugin_dir) const {
    // Look for plugin.json or .claude-plugin/plugin.json
    std::string manifest = plugin_dir + "/plugin.json";
    if (std::filesystem::exists(manifest)) {
        return manifest;
    }

    manifest = plugin_dir + "/.claude-plugin/plugin.json";
    if (std::filesystem::exists(manifest)) {
        return manifest;
    }

    manifest = plugin_dir + "/.claude-plugin/manifest.json";
    if (std::filesystem::exists(manifest)) {
        return manifest;
    }

    return "";
}

nlohmann::json PluginManager::LoadManifest(const std::string& manifest_path) const {
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        return nlohmann::json::object();
    }

    try {
        nlohmann::json json;
        file >> json;
        return json;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load manifest {}: {}", manifest_path, e.what());
        return nlohmann::json::object();
    }
}

void PluginManager::DiscoverPluginContents(Plugin& plugin, const std::string& plugin_dir) {
    // Discover commands
    std::string commands_dir = plugin_dir + "/commands";
    if (std::filesystem::exists(commands_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(commands_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                plugin.commands.push_back(entry.path().string());
            }
        }
    }

    // Discover agents
    std::string agents_dir = plugin_dir + "/agents";
    if (std::filesystem::exists(agents_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(agents_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                plugin.agents.push_back(entry.path().string());
            }
        }
    }

    // Discover skills
    std::string skills_dir = plugin_dir + "/skills";
    if (std::filesystem::exists(skills_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(skills_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                plugin.skills.push_back(entry.path().string());
            }
        }
    }
}

void PluginManager::LoadAllPlugins() {
    if (!std::filesystem::exists(plugins_dir_)) {
        LOG_INFO("Plugins directory does not exist: {}", plugins_dir_);
        return;
    }

    int loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(plugins_dir_)) {
        if (entry.is_directory()) {
            if (LoadPlugin(entry.path().string())) {
                loaded++;
            }
        }
    }

    LOG_INFO("Loaded {} plugins", loaded);
}

bool PluginManager::LoadPlugin(const std::string& plugin_dir) {
    std::string manifest_path = FindPluginManifest(plugin_dir);
    if (manifest_path.empty()) {
        LOG_DEBUG("No manifest found in {}", plugin_dir);
        return false;
    }

    nlohmann::json manifest = LoadManifest(manifest_path);
    if (manifest.empty()) {
        return false;
    }

    Plugin plugin = Plugin::FromJson(manifest, plugin_dir);
    DiscoverPluginContents(plugin, plugin_dir);

    // Check if plugin is disabled in config
    if (plugins_.count(plugin.name) && !plugins_[plugin.name].enabled) {
        plugin.enabled = false;
    }

    plugins_[plugin.name] = plugin;
    LOG_INFO("Loaded plugin: {} v{}", plugin.name, plugin.version);

    return true;
}

bool PluginManager::UnloadPlugin(const std::string& plugin_name) {
    auto it = plugins_.find(plugin_name);
    if (it == plugins_.end()) {
        return false;
    }

    plugins_.erase(it);
    LOG_INFO("Unloaded plugin: {}", plugin_name);
    return true;
}

const Plugin* PluginManager::GetPlugin(const std::string& name) const {
    auto it = plugins_.find(name);
    return (it != plugins_.end()) ? &it->second : nullptr;
}

std::vector<Plugin> PluginManager::GetAllPlugins() const {
    std::vector<Plugin> result;
    for (const auto& [name, plugin] : plugins_) {
        result.push_back(plugin);
    }
    return result;
}

std::vector<std::string> PluginManager::GetAllCommands() const {
    std::vector<std::string> result;
    for (const auto& [name, plugin] : plugins_) {
        if (!plugin.enabled) continue;
        for (const auto& cmd : plugin.commands) {
            // Extract command name from file path
            std::filesystem::path p(cmd);
            result.push_back(p.stem().string());
        }
    }
    return result;
}

std::vector<std::string> PluginManager::GetAllAgents() const {
    std::vector<std::string> result;
    for (const auto& [name, plugin] : plugins_) {
        if (!plugin.enabled) continue;
        for (const auto& agent : plugin.agents) {
            std::filesystem::path p(agent);
            result.push_back(p.stem().string());
        }
    }
    return result;
}

std::vector<std::string> PluginManager::GetAllSkills() const {
    std::vector<std::string> result;
    for (const auto& [name, plugin] : plugins_) {
        if (!plugin.enabled) continue;
        for (const auto& skill : plugin.skills) {
            std::filesystem::path p(skill);
            result.push_back(p.stem().string());
        }
    }
    return result;
}

bool PluginManager::EnablePlugin(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }

    it->second.enabled = true;
    SaveConfig();
    LOG_INFO("Enabled plugin: {}", name);
    return true;
}

bool PluginManager::DisablePlugin(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }

    it->second.enabled = false;
    SaveConfig();
    LOG_INFO("Disabled plugin: {}", name);
    return true;
}

bool PluginManager::IsPluginEnabled(const std::string& name) const {
    auto it = plugins_.find(name);
    return (it != plugins_.end()) ? it->second.enabled : false;
}

void PluginManager::ReloadPlugins() {
    plugins_.clear();
    LoadAllPlugins();
}

void PluginManager::LoadConfig() {
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        return;
    }

    try {
        nlohmann::json json;
        file >> json;

        if (json.contains("disabled_plugins") && json["disabled_plugins"].is_array()) {
            for (const auto& name : json["disabled_plugins"]) {
                if (plugins_.count(name)) {
                    plugins_[name].enabled = false;
                }
            }
        }

        LOG_DEBUG("Loaded plugin config");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load plugin config: {}", e.what());
    }
}

void PluginManager::SaveConfig() {
    nlohmann::json json;
    json["disabled_plugins"] = nlohmann::json::array();

    for (const auto& [name, plugin] : plugins_) {
        if (!plugin.enabled) {
            json["disabled_plugins"].push_back(name);
        }
    }

    // Ensure directory exists
    std::filesystem::path dir = config_path_;
    std::filesystem::create_directories(dir.parent_path());

    std::ofstream file(config_path_);
    if (file.is_open()) {
        file << json.dump(2);
        file.close();
        LOG_DEBUG("Saved plugin config");
    }
}

}  // namespace aicode

// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>

namespace aicode {

/// Plugin definition
struct Plugin {
    std::string name;
    std::string version;
    std::string description;
    std::string path;  // Plugin directory path
    std::vector<std::string> commands;  // Command files
    std::vector<std::string> agents;    // Agent definition files
    std::vector<std::string> skills;    // Skill files
    nlohmann::json config;
    bool enabled = true;

    static Plugin FromJson(const nlohmann::json& json, const std::string& path);
    nlohmann::json ToJson() const;
};

/// Plugin manager for loading and managing plugins
class PluginManager {
public:
    static PluginManager& GetInstance();

    /// Initialize plugin manager
    void Initialize(const std::string& plugins_dir = "~/.aicode/plugins");

    /// Load all plugins from plugins directory
    void LoadAllPlugins();

    /// Load a single plugin from directory
    bool LoadPlugin(const std::string& plugin_dir);

    /// Unload a plugin
    bool UnloadPlugin(const std::string& plugin_name);

    /// Get a plugin by name
    const Plugin* GetPlugin(const std::string& name) const;

    /// Get all loaded plugins
    std::vector<Plugin> GetAllPlugins() const;

    /// Get all commands from all plugins
    std::vector<std::string> GetAllCommands() const;

    /// Get all agents from all plugins
    std::vector<std::string> GetAllAgents() const;

    /// Get all skills from all plugins
    std::vector<std::string> GetAllSkills() const;

    /// Enable a plugin
    bool EnablePlugin(const std::string& name);

    /// Disable a plugin
    bool DisablePlugin(const std::string& name);

    /// Check if plugin is enabled
    bool IsPluginEnabled(const std::string& name) const;

    /// Get plugin directory
    const std::string& GetPluginsDir() const { return plugins_dir_; }

    /// Reload all plugins
    void ReloadPlugins();

    /// Save plugin configuration
    void SaveConfig();

    /// Load plugin configuration
    void LoadConfig();

private:
    PluginManager() = default;
    ~PluginManager() = default;

    std::string plugins_dir_;
    std::unordered_map<std::string, Plugin> plugins_;
    std::string config_path_;

    /// Expand home directory
    std::string ExpandHome(const std::string& path) const;

    /// Find plugin manifest file
    std::string FindPluginManifest(const std::string& plugin_dir) const;

    /// Load plugin manifest
    nlohmann::json LoadManifest(const std::string& manifest_path) const;

    /// Discover plugin contents
    void DiscoverPluginContents(Plugin& plugin, const std::string& plugin_dir);
};

}  // namespace aicode

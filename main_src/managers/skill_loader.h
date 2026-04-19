// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"

#include "common/config.h"

namespace aicode {

/// Install method for skill auto-install
struct SkillInstallInfo {
    std::string method;    // "node", "go", "uv", "download", "apt", "brew"
    std::string formula;   // package/URL to install
    std::string binary;    // expected binary after install

    // OpenClaw extended fields
    std::string kind;               // alias for method
    std::string id;                 // optional install spec identifier
    std::string label;              // human-readable label
    std::string package;            // npm/go package name
    std::string module;             // node module name
    std::string url;                // download URL
    std::vector<std::string> bins;  // expected binaries
    std::vector<std::string> os;    // OS restriction

    std::string EffectiveMethod() const {
        return kind.empty() ? method : kind;
    }

    std::string EffectiveFormula() const {
        if (!formula.empty()) return formula;
        if (!package.empty()) return package;
        if (!module.empty()) return module;
        if (!url.empty()) return url;
        return "";
    }

    std::string EffectiveBinary() const {
        if (!binary.empty()) return binary;
        if (!bins.empty()) return bins.front();
        return "";
    }
};

/// Slash command defined in a skill
struct SkillCommand {
    std::string name;         // command name (no leading /)
    std::string description;
    std::string tool_name;    // tool to invoke
    std::string arg_mode;     // "freeform", "json", "none"
};

/// Metadata for a skill
struct SkillMetadata {
    std::string id;                 // Skill ID (from metadata or filename)
    std::string name;
    std::string description;
    std::vector<std::string> required_bins;
    std::vector<std::string> required_envs;
    std::vector<std::string> any_bins;      // at least one must exist
    std::vector<std::string> config_files;  // required config files
    std::vector<std::string> os_restrict;   // ["linux", "darwin", "win32"]
    bool always = false;                    // skip all gating
    std::string primary_env;                // primary environment variable
    std::string emoji;                      // display emoji
    std::string homepage;                   // skill homepage URL
    std::string skill_key;                  // alternative skill key
    std::string content;

    // Resource info
    std::filesystem::path root_dir;
    std::vector<SkillInstallInfo> installs;
    std::vector<SkillCommand> commands;

    // Resource directories (absolute paths if they exist)
    std::string scripts_dir;
    std::string references_dir;
    std::string assets_dir;
};

/// Loads and manages skills from SKILL.md files
class SkillLoader {
 public:
    static SkillLoader& GetInstance();

    explicit SkillLoader();

    /// Load skills from a directory
    std::vector<SkillMetadata> LoadSkillsFromDirectory(
      const std::filesystem::path& skills_dir);

    /// Multi-directory loading with dedup and config filtering
    std::vector<SkillMetadata> LoadSkills(const SkillsConfig& skills_config,
                    const std::filesystem::path& workspace_path);

    /// Check if skill can be loaded based on environment (gating)
    bool CheckSkillGating(const SkillMetadata& skill);

    /// Get skill context for LLM
    std::string GetSkillContext(const std::vector<SkillMetadata>& skills) const;

    /// Install a skill's dependencies
    bool InstallSkill(const SkillMetadata& skill);

    /// Get all slash commands from loaded skills
    std::vector<SkillCommand> GetAllCommands(
      const std::vector<SkillMetadata>& skills) const;

    /// Get all skill IDs (for default role with "*")
    std::vector<std::string> GetAllSkillIds() const;

    /// Parse YAML frontmatter (public for testing)
    nlohmann::json ParseYamlFrontmatter(const std::string& yaml_str) const;

    /// Get current OS identifier (public for testing)
    std::string GetCurrentOs() const;

    /// Check OS restriction (public for testing)
    bool CheckOsRestriction(const std::vector<std::string>& os_list) const;

 private:
    /// Parse SKILL.md file
    SkillMetadata ParseSkillFile(const std::filesystem::path& skill_file) const;

    /// Check if binary exists in PATH
    bool IsBinaryAvailable(const std::string& binary_name) const;

    /// Check if environment variable exists
    bool IsEnvVarAvailable(const std::string& env_var) const;
};

}  // namespace aicode

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/skill_loader.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <stack>
#include <unordered_set>

#include "common/log_wrapper.h"

namespace prosophor {

SkillLoader& SkillLoader::GetInstance() {
    static SkillLoader instance;
    return instance;
}

SkillLoader::SkillLoader() {
    LOG_DEBUG("SkillLoader initialized");
}

std::vector<SkillMetadata> SkillLoader::LoadSkillsFromDirectory(
    const std::filesystem::path& skills_dir) {
    std::vector<SkillMetadata> skills;

    if (!std::filesystem::exists(skills_dir)) {
        LOG_DEBUG("Skills directory does not exist: {}", skills_dir.string());
        return skills;
    }

    LOG_INFO("Loading skills from: {}", skills_dir.string());

    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(skills_dir)) {
        if (entry.is_regular_file() && entry.path().filename() == "SKILL.md") {
            try {
                auto skill = ParseSkillFile(entry.path());
                if (CheckSkillGating(skill)) {
                    LOG_DEBUG("Loaded skill: {}", skill.name);
                    skills.push_back(std::move(skill));
                } else {
                    LOG_DEBUG("Skipped skill (gating failed): {}", skill.name);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to load skill from {}: {}",
                               entry.path().string(), e.what());
            }
        }
    }

    LOG_DEBUG("Loaded skills", skills.size());
    return skills;
}

bool SkillLoader::CheckSkillGating(const SkillMetadata& skill) {
    if (skill.always) {
        return true;
    }

    if (!skill.os_restrict.empty()) {
        if (!CheckOsRestriction(skill.os_restrict)) {
            LOG_DEBUG("Skill gating failed: OS not in allowed list");
            return false;
        }
    }

    for (const auto& binary : skill.required_bins) {
        if (!IsBinaryAvailable(binary)) {
            LOG_DEBUG("Skill gating failed: binary '{}' not available", binary);
            return false;
        }
    }

    if (!skill.any_bins.empty()) {
        bool found = false;
        for (const auto& binary : skill.any_bins) {
            if (IsBinaryAvailable(binary)) {
                found = true;
                break;
            }
        }
        if (!found) {
            LOG_DEBUG("Skill gating failed: none of anyBins available");
            return false;
        }
    }

    for (const auto& env_var : skill.required_envs) {
        if (!IsEnvVarAvailable(env_var)) {
            LOG_DEBUG("Skill gating failed: env '{}' not available", env_var);
            return false;
        }
    }

    for (const auto& config_file : skill.config_files) {
        std::string expanded = config_file;
        if (expanded.size() >= 2 && expanded.substr(0, 2) == "~/") {
            const char* home = std::getenv("HOME");
            if (home) {
                expanded = std::string(home) + expanded.substr(1);
            }
        }
        if (!std::filesystem::exists(expanded)) {
            LOG_DEBUG("Skill gating failed: config '{}' not found", config_file);
            return false;
        }
    }

    return true;
}

std::string SkillLoader::GetSkillContext(
    const std::vector<SkillMetadata>& skills) const {
    std::ostringstream context;

    for (const auto& skill : skills) {
        if (!skill.emoji.empty()) {
            context << skill.emoji << " ";
        }
        context << "### " << skill.name << "\n";
        if (!skill.description.empty()) {
            context << skill.description << "\n\n";
        }
        context << skill.content << "\n";

        bool has_resources = false;
        if (!skill.scripts_dir.empty() || !skill.references_dir.empty() ||
            !skill.assets_dir.empty()) {
            context << "\n**Resources:**\n";
            has_resources = true;
        }
        if (!skill.scripts_dir.empty()) {
            context << "- Scripts: `" << skill.scripts_dir << "`\n";
        }
        if (!skill.references_dir.empty()) {
            context << "- References: `" << skill.references_dir << "`\n";
        }
        if (!skill.assets_dir.empty()) {
            context << "- Assets: `" << skill.assets_dir << "`\n";
        }

        if (!skill.commands.empty()) {
            if (!has_resources) context << "\n";
            context << "**Commands:**\n";
            for (const auto& cmd : skill.commands) {
                context << "`/" << cmd.name << "`";
                if (!cmd.description.empty()) {
                    context << " - " << cmd.description;
                }
                context << "\n";
            }
        }

        context << "\n";
    }

    return context.str();
}

bool SkillLoader::InstallSkill(const SkillMetadata& skill) {
    if (skill.installs.empty()) {
        LOG_INFO("Skill '{}' has no install instructions", skill.name);
        return true;
    }

    bool all_ok = true;
    for (const auto& inst : skill.installs) {
        std::string eff_binary = inst.EffectiveBinary();
        std::string eff_method = inst.EffectiveMethod();
        std::string eff_formula = inst.EffectiveFormula();

        if (!eff_binary.empty() && IsBinaryAvailable(eff_binary)) {
            LOG_DEBUG("Skill '{}': {} already installed", skill.name,
                           eff_binary);
            continue;
        }

        std::string cmd;
        if (eff_method == "node") {
            cmd = "npm install -g " + eff_formula;
        } else if (eff_method == "go") {
            cmd = "go install " + eff_formula;
        } else if (eff_method == "uv") {
            cmd = "uv pip install " + eff_formula;
        } else if (eff_method == "apt") {
            cmd = "sudo apt-get install -y " + eff_formula;
        } else if (eff_method == "brew") {
            cmd = "brew install " + eff_formula;
        } else if (eff_method == "download") {
            const char* home = std::getenv("HOME");
            std::string bin_dir =
                std::string(home ? home : "/tmp") + "/.prosophor/bin";
            std::filesystem::create_directories(bin_dir);
            std::string dest =
                bin_dir + "/" + (eff_binary.empty() ? "downloaded" : eff_binary);
            cmd =
                "curl -fsSL -o " + dest + " " + eff_formula + " && chmod +x " + dest;
        } else {
            LOG_WARN("Skill '{}': unknown install method '{}'", skill.name,
                          eff_method);
            continue;
        }

        LOG_INFO("Installing skill '{}' via {}: {}", skill.name, eff_method,
                      cmd);
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            LOG_ERROR("Skill '{}' install failed (exit {})", skill.name, ret);
            all_ok = false;
        }
    }
    return all_ok;
}

std::vector<SkillCommand> SkillLoader::GetAllCommands(
    const std::vector<SkillMetadata>& skills) const {
    std::vector<SkillCommand> commands;
    for (const auto& skill : skills) {
        for (const auto& cmd : skill.commands) {
            commands.push_back(cmd);
        }
    }
    return commands;
}

std::vector<std::string> SkillLoader::GetAllSkillIds() const {
    std::vector<std::string> ids;

    // 从默认目录加载所有技能 ID
    std::vector<std::filesystem::path> dirs = {
        std::filesystem::current_path() / "skills",
        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "/tmp") / ".prosophor" / "skills"
    };

    std::unordered_set<std::string> seen;
    for (const auto& dir : dirs) {
        if (!std::filesystem::exists(dir)) continue;

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().filename() == "SKILL.md") {
                try {
                    auto skill = ParseSkillFile(entry.path());
                    if (seen.find(skill.id) == seen.end()) {
                        ids.push_back(skill.id);
                        seen.insert(skill.id);
                    }
                } catch (...) {
                    // 跳过无法解析的技能
                }
            }
        }
    }

    return ids;
}

std::vector<SkillMetadata> SkillLoader::LoadSkills(
    const SkillsConfig& skills_config,
    const std::filesystem::path& workspace_path) {
    std::vector<std::filesystem::path> dirs;
    dirs.push_back(workspace_path / "skills");

    std::string home_str;
    const char* home = std::getenv("HOME");
    if (home) home_str = home;
    dirs.push_back(std::filesystem::path(home_str.empty() ? "/tmp" : home_str) /
                   ".prosophor" / "skills");

    for (const auto& extra : skills_config.load.extra_dirs) {
        dirs.push_back(std::filesystem::path(extra));
    }

    std::unordered_set<std::string> seen_names;
    std::vector<SkillMetadata> result;

    for (const auto& dir : dirs) {
        auto skills = LoadSkillsFromDirectory(dir);
        for (auto& skill : skills) {
            if (seen_names.count(skill.name)) continue;

            auto it = skills_config.entries.find(skill.name);
            if (it != skills_config.entries.end() && !it->second.enabled) {
                LOG_DEBUG("Skill '{}' disabled via config", skill.name);
                continue;
            }

            seen_names.insert(skill.name);
            result.push_back(std::move(skill));
        }
    }

    LOG_DEBUG("Loaded skills from {} directories", result.size(),
                  dirs.size());
    return result;
}

SkillMetadata SkillLoader::ParseSkillFile(
    const std::filesystem::path& skill_file) const {
    std::ifstream file(skill_file);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open skill file: " +
                                 skill_file.string());
    }

    std::ostringstream content;
    content << file.rdbuf();
    file.close();

    std::string file_content = content.str();
    SkillMetadata skill;

    // Set skill ID from filename or metadata
    skill.id = skill_file.parent_path().filename().string();

    std::regex frontmatter_regex(R"(^---\s*\n([\s\S]*?)\n---\s*\n)");
    std::smatch matches;

    if (std::regex_search(file_content, matches, frontmatter_regex)) {
        std::string frontmatter = matches[1].str();

        try {
            nlohmann::json metadata = ParseYamlFrontmatter(frontmatter);

            if (metadata.contains("id")) {
                skill.id = metadata["id"].get<std::string>();
            }
            if (metadata.contains("name")) {
                skill.name = metadata["name"].get<std::string>();
            }
            if (metadata.contains("description")) {
                skill.description = metadata["description"].get<std::string>();
            }
            if (metadata.contains("emoji")) {
                skill.emoji = metadata["emoji"].get<std::string>();
            }
            if (metadata.contains("always")) {
                if (metadata["always"].is_boolean()) {
                    skill.always = metadata["always"].get<bool>();
                } else {
                    skill.always = metadata["always"].get<std::string>() == "true";
                }
            }
            if (metadata.contains("os")) {
                skill.os_restrict = metadata["os"].get<std::vector<std::string>>();
            }

            auto extract_requires = [&](const nlohmann::json& reqs) {
                if (reqs.contains("bins")) {
                    skill.required_bins = reqs["bins"].get<std::vector<std::string>>();
                }
                if (reqs.contains("env")) {
                    skill.required_envs = reqs["env"].get<std::vector<std::string>>();
                }
                if (reqs.contains("envs")) {
                    skill.required_envs = reqs["envs"].get<std::vector<std::string>>();
                }
                if (reqs.contains("anyBins")) {
                    skill.any_bins = reqs["anyBins"].get<std::vector<std::string>>();
                }
                if (reqs.contains("config")) {
                    skill.config_files = reqs["config"].get<std::vector<std::string>>();
                }
            };

            if (metadata.contains("requires") && metadata["requires"].is_object()) {
                extract_requires(metadata["requires"]);
            }

            if (metadata.contains("install") && metadata["install"].is_object()) {
                for (auto it = metadata["install"].begin();
                     it != metadata["install"].end(); ++it) {
                    SkillInstallInfo info;
                    info.method = it.key();
                    info.kind = it.key();
                    if (it.value().is_string()) {
                        info.formula = it.value().get<std::string>();
                    } else if (it.value().is_object()) {
                        info.formula = it.value().value("formula", "");
                        info.binary = it.value().value("binary", "");
                    }
                    skill.installs.push_back(std::move(info));
                }
            }

            if (metadata.contains("commands") && metadata["commands"].is_array()) {
                const auto& commands_arr = metadata["commands"];
                for (const auto& cmd_val : commands_arr) {
                    if (!cmd_val.is_object()) continue;
                    SkillCommand sc;
                    sc.name = cmd_val.value("name", "");
                    sc.description = cmd_val.value("description", "");
                    if (!sc.name.empty()) {
                        skill.commands.push_back(std::move(sc));
                    }
                }
            }

        } catch (const std::exception& e) {
            LOG_WARN("Failed to parse skill frontmatter: {}", e.what());
        }
    }

    if (skill.name.empty()) {
        skill.name = skill_file.parent_path().filename().string();
    }

    skill.root_dir = skill_file.parent_path();
    auto check_resource_dir = [&](const std::string& subdir) -> std::string {
        auto p = skill.root_dir / subdir;
        if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
            return p.string();
        }
        return "";
    };
    skill.scripts_dir = check_resource_dir("scripts");
    skill.references_dir = check_resource_dir("references");
    skill.assets_dir = check_resource_dir("assets");

    std::string content_part = file_content;
    if (!matches.empty()) {
        content_part = file_content.substr(matches[0].length());
    }
    skill.content = content_part;

    return skill;
}

bool SkillLoader::IsBinaryAvailable(const std::string& binary_name) const {
#ifdef _WIN32
    std::string command = "where " + binary_name + " > nul 2>&1";
#else
    std::string command = "which " + binary_name + " > /dev/null 2>&1";
#endif
    int result = std::system(command.c_str());
    return result == 0;
}

bool SkillLoader::IsEnvVarAvailable(const std::string& env_var) const {
    const char* value = std::getenv(env_var.c_str());
    return value != nullptr && std::strlen(value) > 0;
}

bool SkillLoader::CheckOsRestriction(
    const std::vector<std::string>& os_list) const {
    std::string current = GetCurrentOs();
    for (const auto& os : os_list) {
        std::string normalized = os;
        if (normalized == "macos") normalized = "darwin";
        if (normalized == current) return true;
    }
    return false;
}

std::string SkillLoader::GetCurrentOs() const {
#ifdef __linux__
    return "linux";
#elif defined(__APPLE__)
    return "darwin";
#elif defined(_WIN32)
    return "win32";
#else
    return "unknown";
#endif
}

nlohmann::json SkillLoader::ParseYamlFrontmatter(
    const std::string& yaml_str) const {
    nlohmann::json root = nlohmann::json::object();

    std::istringstream stream(yaml_str);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;

        int indent = 0;
        for (char c : line) {
            if (c == ' ')
                ++indent;
            else if (c == '\t')
                indent += 2;
            else
                break;
        }

        std::string trimmed = line.substr(line.find_first_not_of(" \t"));
        while (!trimmed.empty() &&
               (trimmed.back() == ' ' || trimmed.back() == '\t' ||
                trimmed.back() == '\r')) {
            trimmed.pop_back();
        }

        if (trimmed.empty()) continue;

        if (trimmed[0] == '-') {
            std::string val = trimmed.substr(1);
            val.erase(0, val.find_first_not_of(" \t"));
            if (val.empty()) continue;

            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }

            for (auto it = root.begin(); it != root.end(); ++it) {
                if (it->is_array()) {
                    it->push_back(val);
                    break;
                }
            }
            continue;
        }

        size_t colon_pos = trimmed.find(':');
        if (colon_pos == std::string::npos) continue;

        std::string key = trimmed.substr(0, colon_pos);
        std::string value = trimmed.substr(colon_pos + 1);

        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
            key.pop_back();
        value.erase(0, value.find_first_not_of(" \t"));
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.pop_back();

        if (value.empty()) {
            root[key] = "";
        } else if (value == "true") {
            root[key] = true;
        } else if (value == "false") {
            root[key] = false;
        } else {
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            root[key] = value;
        }
    }

    return root;
}

}  // namespace prosophor

// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <filesystem>

#include "core/agent_role.h"

namespace aicode {

/// AgentRoleLoader: 加载 AgentRole 配置文件
class AgentRoleLoader {
public:
    static AgentRoleLoader& GetInstance();

    /// 从配置文件加载角色
    AgentRole LoadRole(const std::string& role_path);

    /// 从目录加载所有角色
    std::vector<AgentRole> LoadAllRoles(const std::string& roles_dir);

    /// 解析 Markdown 前端 matter
    AgentRole ParseMarkdownFile(const std::filesystem::path& file) const;

private:
    AgentRoleLoader() = default;

    /// 解析 YAML frontmatter
    nlohmann::json ParseYamlFrontmatter(const std::string& yaml_str) const;

    /// 提取 frontmatter 之间的内容
    std::string ExtractFrontmatter(const std::string& content,
                                   std::string& body) const;
};

}  // namespace aicode

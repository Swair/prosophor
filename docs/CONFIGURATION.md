# AiCode 配置指南

## 配置文件位置

默认配置文件位于 `~/.aicode/config.json`

---

## 完整配置示例

```json
{
  "log_level": "info",
  "default_provider": "anthropic",
  "default_agent": "default",
  
  "security": {
    "permission_level": "auto",
    "allow_local_execute": true
  },
  
  "providers": {
    "anthropic": {
      "api_key": "YOUR_API_KEY",
      "base_url": "https://api.anthropic.com",
      "api_type": "anthropic-messages",
      "timeout": 30,
      "models": [
        {
          "id": "claude-sonnet-4-6",
          "name": "Claude Sonnet 4.6",
          "cost": {
            "input": 0.000003,
            "output": 0.000015
          },
          "context_window": 200000,
          "max_tokens": 8192
        }
      ],
      "agents": {
        "default": {
          "model": "claude-sonnet-4-6",
          "temperature": 0.7,
          "max_tokens": 8192,
          "context_window": 128000,
          "thinking": "off",
          "use_tools": true,
          "auto_compact": true,
          "compact_max_messages": 100,
          "compact_keep_recent": 20,
          "compact_max_tokens": 100000
        }
      }
    },
    "qwen": {
      "api_key": "YOUR_API_KEY",
      "base_url": "https://dashscope.aliyuncs.com",
      "api_type": "openai-completions",
      "timeout": 60,
      "agents": {
        "default": {
          "model": "qwen-max",
          "temperature": 0.7,
          "max_tokens": 8192
        }
      }
    }
  },
  
  "tools": {
    "enabled": true,
    "allowed_paths": [
      "/home/user/projects/myproject"
    ],
    "denied_paths": [
      "/etc",
      "/root"
    ],
    "allowed_cmds": [
      "git",
      "npm",
      "yarn",
      "cargo",
      "go"
    ],
    "denied_cmds": [
      "rm -rf",
      "sudo",
      "curl | bash"
    ],
    "timeout": 60
  },
  
  "skills": {
    "path": "~/.aicode/skills",
    "auto_approve": [
      "git",
      "search"
    ],
    "load": {
      "extra_dirs": [
        "/opt/aicode/skills"
      ]
    }
  }
}
```

---

## 配置项详解

### 顶层配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `log_level` | string | "info" | 日志级别：debug/info/warn/error |
| `default_provider` | string | "anthropic" | 默认 LLM 提供者 |
| `default_agent` | string | "default" | 默认 Agent 配置名 |

### Security 配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `permission_level` | string | "auto" | 权限模式：auto/default/bypass |
| `allow_local_execute` | boolean | true | 是否允许本地命令执行 |

### Provider 配置

每个提供者可配置：

| 字段 | 类型 | 说明 |
|------|------|------|
| `api_key` | string | API 密钥 |
| `base_url` | string | API 基础 URL |
| `api_type` | string | API 类型 |
| `timeout` | int | 请求超时（秒） |
| `models` | array | 支持的模型列表 |
| `agents` | object | 多个 Agent 配置 |

### Agent 配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `model` | string | "claude-sonnet-4-6" | 使用的模型 |
| `temperature` | float | 0.7 | 温度参数 |
| `max_tokens` | int | 8192 | 最大输出 Token 数 |
| `context_window` | int | 128000 | 上下文窗口大小 |
| `thinking` | string | "off" | Thinking 模式：off/low/medium/high |
| `use_tools` | boolean | true | 是否启用工具 |
| `auto_compact` | boolean | true | 是否自动压缩上下文 |
| `compact_max_messages` | int | 100 | 触发压缩的最大消息数 |
| `compact_keep_recent` | int | 20 | 保留的最近消息数 |
| `compact_max_tokens` | int | 100000 | 最大 Token 数限制 |

### Tools 配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | boolean | true | 是否启用工具 |
| `allowed_paths` | array | [] | 允许访问的路径白名单 |
| `denied_paths` | array | [] | 禁止访问的路径黑名单 |
| `allowed_cmds` | array | [] | 允许执行的命令白名单 |
| `denied_cmds` | array | [] | 禁止执行的命令黑名单 |
| `timeout` | int | 60 | 工具执行超时（秒） |

### Skills 配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `path` | string | "~/.aicode/skills" | 技能目录 |
| `auto_approve` | array | [] | 自动批准的技能列表 |
| `load.extra_dirs` | array | [] | 额外的技能加载目录 |

---

## 环境变量覆盖

可通过以下环境变量覆盖配置：

| 变量 | 说明 |
|------|------|
| `AICODE_CONFIG_PATH` | 自定义配置文件路径 |
| `AICODE_LOG_LEVEL` | 日志级别 |
| `ANTHROPIC_API_KEY` | Anthropic API 密钥 |
| `QWEN_API_KEY` | 通义千问 API 密钥 |

---

## 权限规则配置

权限规则用于精细控制工具调用：

```json
{
  "permission_rules": [
    {
      "tool_name": "bash",
      "command_pattern": "git *",
      "default_level": "allow"
    },
    {
      "tool_name": "bash",
      "command_pattern": "npm test",
      "default_level": "allow"
    },
    {
      "tool_name": "read_file",
      "path_pattern": "/etc/*",
      "default_level": "deny"
    },
    {
      "tool_name": "write_file",
      "path_pattern": "*.md",
      "default_level": "allow"
    }
  ]
}
```

### 规则字段

| 字段 | 说明 |
|------|------|
| `tool_name` | 工具名称模式（支持通配符） |
| `command_pattern` | Bash 命令模式（用于 bash/exec 工具） |
| `path_pattern` | 文件路径模式（用于文件工具） |
| `default_level` | 默认权限级别：allow/deny/ask |
| `allowed_args` | 允许的参数值列表 |
| `denied_args` | 禁止的参数值列表 |

---

## MCP 服务器配置

MCP 服务器配置文件位于 `~/.aicode/mcp_config.json`：

```json
{
  "servers": [
    {
      "name": "filesystem",
      "type": "stdio",
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user/projects"],
      "enabled": true
    },
    {
      "name": "github",
      "type": "stdio",
      "command": "node",
      "args": ["/opt/mcp/github-server.js"],
      "env": {
        "GITHUB_TOKEN": "YOUR_TOKEN"
      },
      "enabled": true
    },
    {
      "name": "slack",
      "type": "sse",
      "url": "http://localhost:8080/sse",
      "enabled": false
    }
  ]
}
```

### MCP 服务器配置字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 服务器名称 |
| `type` | string | 连接类型：stdio/sse/websocket |
| `command` | string | stdio 类型的执行命令 |
| `args` | array | 命令参数 |
| `url` | string | SSE/WebSocket 的 URL |
| `env` | object | 环境变量 |
| `enabled` | boolean | 是否启用 |

---

## LSP 服务器配置

LSP 服务器配置在 `~/.aicode/lsp_config.json`：

```json
{
  "servers": [
    {
      "name": "clangd",
      "command": "clangd",
      "args": ["--background-index"],
      "file_patterns": ["*.c", "*.cpp", "*.h", "*.hpp"],
      "initialization_options": {}
    },
    {
      "name": "rust-analyzer",
      "command": "rust-analyzer",
      "args": [],
      "file_patterns": ["*.rs"],
      "initialization_objects": {}
    },
    {
      "name": "gopls",
      "command": "gopls",
      "args": [],
      "file_patterns": ["*.go"],
      "initialization_objects": {
        "staticcheck": true
      }
    },
    {
      "name": "ts-language-server",
      "command": "typescript-language-server",
      "args": ["--stdio"],
      "file_patterns": ["*.ts", "*.tsx", "*.js", "*.jsx"],
      "initialization_objects": {}
    }
  ]
}
```

---

## 技能配置示例

SKILL.md 文件格式：

```markdown
---
name: git
description: Git version control operations
required_bins: [git]
required_envs: []
any_bins: []
config_files: []
os_restrict: []
always: false
primary_env: ""
emoji: 🦆
homepage: https://github.com
skill_key: git-operations
---

# Git Skill

You are a git expert. Help users with:

1. Writing commit messages
2. Understanding git status
3. Resolving merge conflicts
4. Rebasing and cherry-picking
5. Managing branches

## Common Commands

- `git status` - Show working tree status
- `git diff` - Show changes
- `git log` - Show commit history
- `git add <file>` - Stage changes
- `git commit -m "message"` - Commit changes
```

### 技能元数据字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 技能名称 |
| `description` | string | 技能描述 |
| `required_bins` | array | 必需的二进制文件 |
| `required_envs` | array | 必需的环境变量 |
| `any_bins` | array | 至少需要一个 |
| `config_files` | array | 必需的配置文件 |
| `os_restrict` | array | OS 限制：linux/darwin/win32 |
| `always` | boolean | 是否跳过门控检查 |
| `primary_env` | string | 主要环境变量 |
| `emoji` | string | 显示用的 emoji |
| `homepage` | string | 技能主页 |
| `skill_key` | string | 替代技能键 |

---

## 最佳实践

### 1. 安全配置

```json
{
  "security": {
    "permission_level": "default",
    "allow_local_execute": true
  },
  "tools": {
    "denied_paths": ["/etc", "/root", "/proc"],
    "denied_cmds": ["sudo", "rm -rf /", "curl | bash"]
  }
}
```

### 2. 性能优化

```json
{
  "agents": {
    "default": {
      "auto_compact": true,
      "compact_max_messages": 50,
      "compact_keep_recent": 15,
      "compact_max_tokens": 80000
    }
  }
}
```

### 3. 多模型配置

```json
{
  "providers": {
    "anthropic": {
      "agents": {
        "fast": {
          "model": "claude-haiku-4-5",
          "temperature": 0.5,
          "max_tokens": 4096
        },
        "smart": {
          "model": "claude-opus-4-6",
          "temperature": 0.7,
          "max_tokens": 8192
        }
      }
    }
  }
}
```

### 4. 开发环境配置

```json
{
  "tools": {
    "allowed_paths": ["/home/user/projects"],
    "allowed_cmds": ["git", "npm", "yarn", "cargo", "go", "make", "cmake"]
  },
  "skills": {
    "auto_approve": ["git", "search", "lsp"]
  }
}
```

---

## 故障排查

### 配置文件验证

运行以下命令检查配置是否有效：

```bash
aicode --check-config
```

### 常见问题

1. **API 密钥错误**
   - 检查 `api_key` 是否正确
   - 确认环境变量优先级

2. **工具执行失败**
   - 检查 `allowed_cmds` 白名单
   - 确认路径在 `allowed_paths` 内

3. **技能未加载**
   - 检查 `required_bins` 是否存在
   - 确认 `os_restrict` 匹配当前系统

4. **LSP 无法启动**
   - 确认语言服务器已安装
   - 检查 `command` 路径是否正确

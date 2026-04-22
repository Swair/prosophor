# AiCode

<div align="center">

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)](https://en.cppreference.com/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

</div>

---

## 🎯 项目概述

> **核心创新**：AiCode 是**业界首个基于插件化主动触发架构的 Agent CLI 工具**，实现了从"被动响应"到"主动预判交互"的范式升级。

AiCode 是一个**基于 C++ 开发的主动式多模态交互 Agent CLI 工具**，采用 REACT 范式（思考 - 行动 - 观察闭环），通过**业界首个插件化主动触发架构**实现**主动感知、主动预判、主动交互**的智能体能力。

基于《插件化主动式多模态交互触发架构白皮书》设计，填补了"被动响应式 Agent"与"固定规则引擎"之间的空白。

![unix展示图](docs/demo.png)
![windows展示图](docs/win_demo.png)
### 核心定位

| 特性 | 说明 |
|------|------|
| **本质** | 主动式多模态交互智能体（Proactive Agentic CLI） |
| **运行环境** | C++ 原生编译，无运行时依赖 |
| **核心能力** | 主动感知、自主规划、工具调用、环境反馈、迭代验证 |
| **交互范式** | 被动响应 + 主动触发双模式 |
| **支持的 LLM** | Anthropic (Claude)、Qwen (通义千问)、Ollama (本地模型) |

### 架构分层

```
┌─────────────────────────────────────────────────────────────┐
│ CLI 交互层 → 原生终端输入输出、信号处理（Ctrl+C）             │
├─────────────────────────────────────────────────────────────┤
│ 意图解析层 → 指令解析、上下文加载（文件/目录/Git）            │
├─────────────────────────────────────────────────────────────┤
│ Agent 调度层 → 理解 → 规划 → 工具调用 → 观察 → 验证 → 迭代    │
├─────────────────────────────────────────────────────────────┤
│ 工具执行层 → 文件系统、Shell 命令、网络搜索 (40+ 工具)         │
├─────────────────────────────────────────────────────────────┤
│ 主动触发层 → 插件扫描、三模式触发、空闲检测、优先级调度        │
├─────────────────────────────────────────────────────────────┤
│ 环境反馈层 → 捕获命令输出、错误、状态更新                     │
├─────────────────────────────────────────────────────────────┤
│ 输出验证层 → 代码校验、测试执行、任务终止                     │
└─────────────────────────────────────────────────────────────┘
```

---

## 🆚 相比 Claude Code 的优势

| 特性 | AiCode | Claude Code |
|:-----|:------:|:------------:|
| **多 LLM 支持** | ✅ Anthropic、Qwen、Ollama | ❌ 仅 Claude |
| **本地部署** | ✅ 完全本地运行，数据可控 | ❌ 依赖云端 API |
| **开源许可** | ✅ Apache 2.0 | ❌ 专有许可 |
| **运行时** | ✅ C++ 高性能 | ⚠️ Node.js |
| **主动式交互** | ✅ 插件化主动触发框架 | ❌ 无 |
### 核心优势详解

#### 1. 多 LLM 提供者支持 🔄
AiCode 不绑定单一模型，支持：
- **Anthropic (Claude)** - 通过官方 API 或兼容接口
- **Qwen (通义千问)** - 阿里云 DashScope
- **Ollama** - 本地模型部署
- 易于扩展新的提供者（继承 `LLMProvider` 接口即可）

#### 2. 主动式交互架构 🎯

**业界首个面向主动交互场景的插件化框架**，填补了"被动响应式 Agent"与"固定规则引擎"之间的空白。

| 维度 | 传统 Agent | 规则引擎工具 | AiCode 架构 |
|------|-----------|------------|-----------|
| 触发方式 | 被动响应（用户指令） | 固定规则（if-then） | **插件化主动触发** |
| 扩展能力 | 修改内核/配置 | 修改脚本/规则 | **插件热插拔** |
| 响应方式 | 大模型生成 | 执行预设动作 | **大模型场景化响应** |
| 生态支撑 | Skills/MCP（被动型） | 无 | **主动交互插件社区** |

**核心创新**：
- **插件化框架**：触发逻辑与内核解耦，支持插件热插拔
- **主动式交互**：从"被动响应"升级为"主动预判"
- **生态闭环**：插件社区支撑"开发→审核→分发→反馈→迭代"

**插件示例结构**：
```
active/
└── cpu_temperature_monitor/
    ├── trigger          # 触发脚本（任意语言，返回 true/false）
    ├── trigger_mode.cfg # 模式配置（periodic/idle/idle_once）
    └── ACTIVE.md        # 交互配置（大模型联动）
```

**触发流程**：
```
trigger 返回 true → 读取 ACTIVE.md → 调用大模型 → 生成响应 → 通知用户/执行任务
```

**三模式触发**：
- **周期性强制触发**（periodic）：按配置的 interval 周期定时触发，适配严重级事件（硬件故障等）
- **用户空闲触发**（idle）：与大模型无交互达到阈值时触发，适配普通级事件（空闲提醒等）
- **单次空闲触发**（idle_once）：与大模型无交互达到阈值时触发一次，触发后自动禁用（一次性引导）

#### 3. 完全本地可控 🔒
- 配置文件存储在 `~/.aicode/config.json`，完全可控
- 支持自定义 API 端点（包括本地代理）
- 可配置路径白名单/黑名单，精细控制文件访问
- 可配置命令白名单/黑名单，精细控制命令执行

#### 4. 灵活的权限系统 ⚖️
```json
{
  "permission_rules": [
    {
      "tool_name": "bash",
      "command_pattern": "git *",
      "default_level": "allow"
    },
    {
      "tool_name": "read_file",
      "path_pattern": "/etc/*",
      "default_level": "deny"
    }
  ]
}
```

#### 5. 可扩展技能系统 🧩
通过 `SKILL.md` 文件定义技能，支持：
- 环境门控（二进制、环境变量、OS 限制）
- 自动安装依赖
- 自定义命令注册

#### 6. 高性能 C++ 实现 ⚡
- 原生编译，无 Node.js 运行时依赖
- 更低的内存占用
- 更快的启动速度

#### 7. 完善的 LSP 支持 💻
- 多语言服务器并发管理
- 诊断、跳转定义、查找引用、悬停信息
- 文档符号、工作区符号、格式化

#### 8. 开源许可 📄
- Apache License 2.0 - 可商用、可修改、可分发
- 社区驱动，透明开发

---

## ✨ 核心特性

### 🎯 主动式交互架构 - 业界首个主动交互插件化框架

AiCode 提出了**业界首个面向主动交互场景的插件化架构**，填补了"被动响应式 Agent"与"固定规则引擎"之间的空白。

**传统方案的局限**：
- **被动响应式 Agent**（ChatGPT、Claude）：等待用户指令后触发 Agent Loop，无法主动感知场景变化
- **固定规则引擎**（Hazel、AutoHotkey）：if-then 规则固化，无法扩展，无智能响应

**AiCode 的主动式交互架构**：

```
┌─────────────────────────────────────────────────────────────┐
│              插件社区（生态支撑，独立于运行时）               │
│         插件上传、安全检测、审核、分发、更新                  │
└─────────────────────────────────────────────────────────────┘
                            │ 下载/更新
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    运行时架构（三层）                        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │         插件层：触发逻辑与内核解耦                     │   │
│  │     active/目录，trigger + trigger_mode.cfg + ACTIVE.md│   │
│  └─────────────────────────────────────────────────────┘   │
│                            │                                │
│                            ▼                                │
│  ┌─────────────────────────────────────────────────────┐   │
│  │     调度层：三模式分级触发、主动感知与调度             │   │
│  │    插件扫描、分类调用、优先级调度、大模型联动          │   │
│  └─────────────────────────────────────────────────────┘   │
│                            │                                │
│                            ▼                                │
│  ┌─────────────────────────────────────────────────────┐   │
│  │    执行层：复用 Agent 框架的工具执行能力               │   │
│  │         场景化交互、多模态输出                         │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

**插件触发流程**：
```
trigger 返回 true → 读取 ACTIVE.md → 调用大模型 → 生成响应 → 通知用户/执行任务
```

**三模式触发**：
- **周期性强制触发**（periodic）：按配置的 interval 周期定时触发，适配严重级事件（硬件故障等）
- **用户空闲触发**（idle）：与大模型无交互达到阈值时触发，适配普通级事件（空闲提醒等）
- **单次空闲触发**（idle_once）：与大模型无交互达到阈值时触发一次，触发后自动禁用（一次性引导）

> 详见：[基于插件化的主动式多模态交互触发架构白皮书](./基于插件化的主动式多模态交互触发架构白皮书.md)

### 🏃 REACT 范式 - "思考 - 行动 - 观察"闭环

```
理解 → 规划 → 工具调用 → 观察 → 验证 → 迭代/终止
```

AiCode 采用 **REACT（Reason + Act）范式**，自主生成任务序列（如：读文件 → 改代码 → 运行测试 → 修复报错），支持多轮迭代。

### 🛠️ 工具系统 - 40+ 内置工具

| 类别 | 工具 |
|------|------|
| **文件操作** | `read_file`、`write_file`、`edit_file` |
| **Shell 执行** | `exec`、`bash` |
| **搜索** | `glob`、`grep` |
| **Git** | `git_status`、`git_diff`、`git_log`、`git_commit`、`git_add`、`git_branch` |
| **LSP** | `lsp_diagnostics`、`lsp_go_to_definition`、`lsp_find_references`、`lsp_get_hover`、`lsp_document_symbols`、`lsp_workspace_symbols`、`lsp_format_document` |
| **Web** | `web_search`、`web_fetch` |
| **交互** | `ask_user_question`、`todo_write` |
| **MCP** | `mcp_list_tools`、`mcp_call_tool`、`mcp_read_resource` |
| **Agent** | `agent` (子任务分解与委派) |
| **计划** | `plan_mode`、`task_tool` |
| **定时任务** | `cron_scheduler` |
| **工作树** | `worktree_tool` |
| **Token** | `token_count`、`token_usage` |

### 🧠 技能系统

技能通过 `SKILL.md` 文件定义，支持环境门控和自动安装：

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
emoji: 🦆
---

# Git Skill

You are a git expert. Help users with git operations...
```

### 🔐 权限管理

三种权限级别：
- **Allow** - 自动批准
- **Deny** - 自动拒绝
- **Ask** - 需要用户确认

规则匹配支持工具名、命令模式、路径模式，失败回退逻辑（3 次拒绝后自动批准）。

### 💾 上下文压缩

三种压缩策略：
- **Summary** - 生成旧消息的 AI 摘要
- **Truncate** - 仅保留最近 N 条消息
- **Hybrid** - 摘要 + 保留最近消息（默认）

### 🧩 MCP 协议

支持 MCP (Model Context Protocol) 服务器集成：
- stdio 传输
- SSE/WebSocket 传输
- 工具发现和执行
- 资源读取

---

## 🚀 快速开始

### 环境要求

| 组件 | 要求 | 说明 |
|------|------|------|
| 编译器 | C++17 或更高版本 | 支持现代 C++ 特性 |
| CMake | 3.20+ | 构建系统 |
| 依赖 | nlohmann/json、spdlog、libcurl | 自动下载 |

### 构建

```bash
# 克隆仓库
git clone https://github.com/your-repo/aicode.git
cd aicode

# 创建构建目录
mkdir -p build && cd build

# 配置
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译
make -j8

# 安装
make install
```

### 运行

```bash
# 使用 Makefile
make run

# 或直接运行
./build/install/bin/aicode
```

### 配置

首次运行时，配置文件将生成在 `~/.aicode/config.json`：

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
      "agents": {
        "default": {
          "model": "claude-sonnet-4-6",
          "temperature": 0.7,
          "max_tokens": 8192,
          "context_window": 128000,
          "thinking": "off",
          "use_tools": true,
          "auto_compact": true
        }
      }
    },
    "qwen": {
      "api_key": "YOUR_DASHSCOPE_KEY",
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
    },
    "ollama": {
      "base_url": "http://localhost:11434",
      "api_type": "openai-completions",
      "timeout": 120,
      "agents": {
        "default": {
          "model": "qwen2.5-coder:32b",
          "temperature": 0.7,
          "max_tokens": 8192
        }
      }
    }
  },
  
  "tools": {
    "enabled": true,
    "allowed_paths": [],
    "denied_paths": [],
    "allowed_cmds": [],
    "denied_cmds": [],
    "timeout": 60
  },
  
  "skills": {
    "path": "~/.aicode/skills",
    "auto_approve": ["read_file", "grep"],
    "load": {
      "extra_dirs": []
    }
  }
}
```

### 配置项详解

#### 顶层配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `log_level` | string | "info" | 日志级别：debug/info/warn/error |
| `default_provider` | string | "anthropic" | 默认 LLM 提供者 |
| `default_agent` | string | "default" | 默认 Agent 配置名 |

#### Security 配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `permission_level` | string | "auto" | 权限模式：auto/default/bypass |
| `allow_local_execute` | boolean | true | 是否允许本地命令执行 |

#### Provider 配置

每个提供者可配置：

| 字段 | 类型 | 说明 |
|------|------|------|
| `api_key` | string | API 密钥 (Ollama 不需要) |
| `base_url` | string | API 基础 URL |
| `api_type` | string | API 类型：anthropic-messages / openai-completions |
| `timeout` | int | 请求超时（秒） |
| `agents` | object | 多个 Agent 配置 |

#### Agent 配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `model` | string | "claude-sonnet-4-6" | 使用的模型 |
| `temperature` | float | 0.7 | 温度参数 |
| `max_tokens` | int | 8192 | 最大输出 Token 数 |
| `context_window` | int | 128000 | 上下文窗口大小 |
| `thinking` | string | "off" | Thinking 模式：off/low/medium/high |
| `use_tools` | boolean | true | 是否启用工具 |
| `auto_compact` | boolean | true | 是否自动压缩上下文 |

#### Tools 配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | boolean | true | 是否启用工具 |
| `allowed_paths` | array | [] | 允许访问的路径白名单 |
| `denied_paths` | array | [] | 禁止访问的路径黑名单 |
| `allowed_cmds` | array | [] | 允许执行的命令白名单 |
| `denied_cmds` | array | [] | 禁止执行的命令黑名单 |
| `timeout` | int | 60 | 工具执行超时（秒） |

#### Skills 配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `path` | string | "~/.aicode/skills" | 技能目录 |
| `auto_approve` | array | [] | 自动批准的技能列表 |
| `load.extra_dirs` | array | [] | 额外的技能加载目录 |

### 环境变量

可通过以下环境变量覆盖配置：

| 变量 | 说明 |
|------|------|
| `AICODE_CONFIG_PATH` | 自定义配置文件路径 |
| `AICODE_LOG_LEVEL` | 日志级别 |
| `ANTHROPIC_API_KEY` | Anthropic API 密钥 |
| `QWEN_API_KEY` | 通义千问 API 密钥 |

---

## 🔧 配置参考

### MCP 服务器配置

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
    }
  ]
}
```

### LSP 服务器配置

LSP 服务器配置在 `~/.aicode/lsp_config.json`：

```json
{
  "servers": [
    {
      "name": "clangd",
      "command": "clangd",
      "args": ["--background-index"],
      "file_patterns": ["*.c", "*.cpp", "*.h", "*.hpp"]
    },
    {
      "name": "rust-analyzer",
      "command": "rust-analyzer",
      "args": [],
      "file_patterns": ["*.rs"]
    }
  ]
}
```

### 身份文件

工作空间根目录支持以下身份文件：

| 文件 | 描述 |
|------|------|
| `SOUL.md` | AI 助手的身份/个性定义 |
| `USER.md` | 用户偏好和上下文 |
| `MEMORY.md` | 持久化记忆索引 |
| `AGENTS.md` | 行为指令 |
| `TOOLS.md` | 工具使用指南 |

---

## 📋 使用方式

### 基本交互

启动 AiCode 后，可以直接输入自然语言指令：

```
$ aicode
AiCode v1.0.0

> 帮我查看当前目录的文件结构
> 创建一个计算斐波那契数列的函数
> 运行测试并修复失败的用例
```

### 内置命令

| 命令 | 功能 |
|------|------|
| `/help` | 显示帮助信息 |
| `/clear` | 清除对话历史 |
| `/plan` | 进入/退出计划模式 |
| `/compact` | 手动压缩上下文 |
| `/model` | 切换使用的模型 |
| `/provider` | 切换 LLM 提供者 |
| `/session` | 管理会话（保存/加载/列表） |
| `/mcp` | MCP 服务器管理 |
| `/skill` | 技能管理 |
| `/cron` | 定时任务管理 |
| `/worktree` | Git worktree 管理 |
| `/task` | 任务管理 |
| `/buddy` | 伴生宠物系统 |
| `/token` | 查看 Token 使用情况 |
| `/exit` | 退出程序 |

### 会话管理

```bash
# 保存当前会话
/session save my-session

# 列出所有会话
/session list

# 加载会话
/session load my-session

# 删除会话
/session delete my-session
```

---

## 🏗️ 架构设计

### 系统架构图

```
┌─────────────────────────────────────────────────────────┐
│                    CLI Layer (cli/)                      │
│   CommandRegistry  │  InputHandler  │  UI               │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                 AgentCommander (core/)                   │
│           用户交互、命令处理、Agent 执行编排               │
└─────────────────────────────────────────────────────────┘
                            │
            ┌───────────────┼───────────────┐
            ▼               ▼               ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│   AgentCore     │ │ MemoryManager   │ │  SkillLoader    │
│ 消息处理/工具   │ │ 工作空间文件/   │ │ 技能加载/解析   │
│ 执行/LLM 交互   │ │ 会话管理        │ │                 │
└─────────────────┘ └─────────────────┘ └─────────────────┘
            │               │               │
            ▼               ▼               ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│  ToolRegistry   │ │ LspManager      │ │ McpClient       │
│  工具注册/执行  │ │ LSP 语言服务    │ │ MCP 协议客户端  │
└─────────────────┘ └─────────────────┘ └─────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│              LLM Providers (providers/)                  │
│   AnthropicProvider  │  QwenProvider  │  LLMProvider   │
└─────────────────────────────────────────────────────────┘
```

### 核心模块

| 模块 | 职责 |
|------|------|
| `AgentCommander` | 系统入口，编排用户交互和 Agent 执行（单例模式） |
| `AgentCore` | 消息处理、工具执行、LLM 交互的核心循环 |
| `MemoryManager` | 工作空间文件管理、每日记忆存储、文件变化监听 |
| `SkillLoader` | SKILL.md 解析、技能门控检查、自动安装依赖 |
| `ToolRegistry` | 工具注册、Schema 生成、执行路由 |
| `LLMProvider` | LLM API 抽象接口（Anthropic/Qwen/Ollama） |
| `CompactService` | 上下文压缩，管理长对话历史（单例模式） |
| `PermissionManager` | 工具调用授权（allow/deny/ask，单例模式） |
| `PlanModeManager` | 结构化任务规划和跟踪（单例模式） |
| `LspManager` | LSP 语言服务器管理、JSON-RPC 通信（单例模式） |
| `McpClient` | MCP 协议客户端，支持外部工具集成（单例模式） |
| `SessionManager` | 会话创建/保存/加载/恢复 |
| `TokenTracker` | Token 使用追踪和成本计算 |
| `ReferenceParser` | 文件引用解析 (@filename:line) |
| `BuddyManager` | 伴生宠物系统（ASCII 艺术渲染） |
| `WorktreeManager` | Git worktree 管理 |
| `CronScheduler` | 定时任务调度 |
| `BackgroundTaskManager` | 后台任务管理 |

### 设计模式

| 模式 | 应用 |
|------|------|
| **单例模式** | `AgentCommander`、`CompactService`、`PermissionManager`、`PlanModeManager`、`LspManager`、`McpClient`、`SessionManager` |
| **策略模式** | `LLMProvider` 接口（`AnthropicProvider`、`QwenProvider`） |
| **工厂模式** | `ToolRegistry` 注册和创建工具执行器、`SkillLoader` 解析和实例化技能 |
| **观察者模式** | `SessionOutputCallback`（会话状态变化通知）、`FileChangeCallback`（文件变化监听） |

---

## 📁 项目结构

```
AiCode/
├── main_src/
│   ├── cli/                    # CLI 交互层
│   │   ├── command_registry.*  # 命令注册和执行
│   │   └── input_handler.*     # 终端输入处理
│   ├── common/                 # 通用工具
│   │   ├── config.*            # 配置系统
│   │   ├── constants.h         # 常量定义
│   │   ├── curl_client.*       # HTTP 客户端
│   │   ├── log_wrapper.h       # 日志封装
│   │   ├── file_utils.*        # 文件工具
│   │   ├── time_wrapper.*      # 时间工具
│   │   └── string_utils.*      # 字符串工具
│   ├── core/                   # 核心业务逻辑
│   │   ├── agent_commander.*   # 主入口/编排器
│   │   ├── agent_core.*        # Agent 核心循环
│   │   ├── compact_service.*   # 上下文压缩
│   │   ├── memory_manager.*    # 文件/记忆管理
│   │   ├── permission_manager.*# 权限管理
│   │   ├── plan_mode.*         # 计划模式
│   │   ├── skill_loader.*      # 技能加载
│   │   ├── system_prompt.*     # 系统提示构建
│   │   ├── session_manager.*   # 会话管理
│   │   ├── reference_parser.*  # 文件引用解析
│   │   └── messages_schema.*   # 消息数据结构
│   ├── providers/              # LLM 提供者
│   │   ├── llm_provider.*      # 抽象接口
│   │   ├── anthropic_provider.*# Anthropic API
│   │   ├── qwen_provider.*     # Qwen API
│   │   └── ollama_provider.*   # Ollama API
│   ├── tools/                  # 工具实现
│   │   ├── tool_registry.*     # 工具注册表
│   │   ├── glob_tool.*
│   │   ├── grep_tool.*
│   │   ├── lsp_tool.*
│   │   ├── agent_tool.*        # 子 Agent 工具
│   │   ├── task_tool.*         # 任务工具
│   │   ├── worktree_tool.*     # Worktree 工具
│   │   ├── cron_tool.*         # 定时任务工具
│   │   ├── background_run_tool.* # 后台运行工具
│   │   └── ...
│   ├── mcp/                    # MCP 协议
│   │   └── mcp_client.*        # MCP 客户端
│   ├── managers/               # 管理器
│   │   ├── buddy_manager.*     # Buddy 系统
│   │   ├── token_tracker.*     # Token 追踪
│   │   ├── worktree_manager.*  # Worktree 管理
│   │   └── background_task_manager.* # 后台任务管理
│   ├── agents/                 # Agent 相关
│   │   ├── plan_mode.*         # 计划模式
│   │   ├── task_manager.*      # 任务管理
│   │   └── subagent_coordinator.* # 子 Agent 协调器
│   └── services/               # 外部服务
│       ├── lsp_manager.*       # LSP 管理
│       └── cron_scheduler.*    # 定时任务
├── config/                     # 配置文件
├── docs/                       # 技术文档
├── tests/                      # 测试代码
├── CMakeLists.txt              # 构建配置
└── Makefile                    # 快捷构建脚本
```

---

## 📖 文档

| 文档 | 描述 |
|------|------|
| [docs/README.md](./docs/README.md) | 文档索引 |
| [docs/ARCHITECTURE.md](./docs/ARCHITECTURE.md) | 系统架构设计文档 |
| [docs/CONFIGURATION.md](./docs/CONFIGURATION.md) | 配置指南 |
| [主动式交互架构白皮书](./基于插件化的主动式多模态交互触发架构白皮书.md) | 主动式交互架构设计与创新 (中文) |
| [Proactive Interaction Whitepaper](./Whitepaper.md) | Whitepaper (English) |
| [README (English)](./README_en.md) | Project README (English) |

---

## 🔧 故障排查

### 常见问题

**1. API 密钥错误**
- 检查 `api_key` 是否正确
- 确认环境变量优先级

**2. 工具执行失败**
- 检查 `allowed_cmds` 白名单
- 确认路径在 `allowed_paths` 内

**3. 技能未加载**
- 检查 `required_bins` 是否存在
- 确认 `os_restrict` 匹配当前系统

**4. LSP 无法启动**
- 确认语言服务器已安装
- 检查 `command` 路径是否正确

---

## 📄 许可证

Licensed under the Apache License, Version 2.0.

See [LICENSE](./LICENSE) for details.

---

## 🙏 致谢

- **Anthropic** - Claude 大模型的创造者
- **C++ 社区** - 优秀的系统编程语言和工具链
- **所有贡献者** - 感谢每一位为项目做出贡献的开发者

---

<div align="center">

**Made with ❤️ and C++ 🦾**

如果这个项目对你有帮助，请给一个 ⭐️ Star 支持一下！

</div>

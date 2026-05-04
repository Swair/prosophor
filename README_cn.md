# Prosophor

<div align="center">

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)](https://en.cppreference.com/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

</div>

---

## 项目概述

Prosophor 是一个**基于 C++ 开发的主动式 Agent CLI 工具**，采用 REACT 范式（思考 - 行动 - 观察闭环），通过**插件化主动触发架构**实现主动感知、主动预判、主动交互的智能体能力。

| 特性 | 说明 |
|------|------|
| **运行环境** | C++ 原生编译，无运行时依赖 |
| **核心能力** | 主动感知、自主规划、工具调用、环境反馈 |
| **交互范式** | 被动响应 + 主动触发双模式 |
| **支持的 LLM** | Anthropic (Claude)、Ollama (本地模型)、OpenAI 兼容接口、llama.cpp (本地 GGUF) |

---

## 主动触发架构 — 核心创新

传统工具等待命令，Prosophor 通过插件化三层架构主动感知和响应：

```
Plugin Community (Upload → Audit → Distribute → Update)
                          │
                          ▼
Plugin Layer ─ trigger script + mode config + ACTIVE.md
Scheduling   ─ periodic / idle / idle_once · priority
Execution    ─ AgentCore + ToolRegistry + LLM linkage
```

**触发模式**：

| 模式 | 触发条件 | 用途 |
|------|---------|------|
| `periodic` | 每隔 N 秒 | 关键告警（硬件温度、错误日志） |
| `idle` | 空闲 N 秒后 | 提醒、建议 |
| `idle_once` | 每次空闲触发一次 | 一次性引导 |

---

## 核心特性

### REACT Agent 循环

理解 → 规划 → 工具调用 → 观察 → 验证 → 迭代/终止

### 工具系统

| 类别 | 工具 |
|------|------|
| **文件操作** | `read_file`、`write_file`、`edit_file`、`file_search` |
| **Shell 执行** | `bash`、`background_run` |
| **搜索** | `web_search`、`web_fetch` |
| **Git** | `git_status`、`git_diff`、`git_log`、`git_commit`、`git_add`、`git_branch`、`git_checkout`、`git_push` |
| **MCP** | `mcp_list_tools`、`mcp_call_tool`、`mcp_read_resource` |
| **Agent** | `agent`（子任务分解与委派） |
| **计划** | `plan`、`task` |
| **Token** | `token_usage` |

### 本地模型支持（llama.cpp）

内置 `llama-server` 生命周期管理：

| 命令 | 功能 |
|------|------|
| `/server start` | 启动本地模型服务 |
| `/server stop` | 停止服务 |
| `/server status` | 查看服务状态 |
| `/setup` | 自动检测硬件 + 扫描 GGUF + 配置 |

支持 `auto_start: true` 配置项实现启动时自动拉起。自动检测 NVIDIA GPU、Apple Silicon 和 CPU 线程数以推荐最优参数。

### 技能系统

技能通过 `SKILL.md` 文件定义，支持环境门控：

```markdown
---
name: git
description: Git version control operations
required_bins: [git]
---
```

### 权限管理

三级权限：Allow / Deny / Ask。支持按工具名、命令模式、路径模式匹配。

### MCP 协议

支持 MCP (Model Context Protocol) 服务器集成：stdio、SSE/WebSocket 传输。

---

## 系统架构

```
命令行 / SDL UI
       │
       ▼
 AgentCommander — 命令分发，Agent 编排
       │
       ▼
 AgentCore — 消息处理、工具执行、LLM 循环
       │
 ┌─────┼──────┬──────┬──────┐
 │     │      │      │      │
 ▼     ▼      ▼      ▼      ▼
Tools  MCP   Skills Memory  LSP
       │
       ▼
 LLM Providers (Anthropic / Ollama / OpenAI / llama.cpp)
```

### 模块结构

```
main_src/
├── agents/         # Agent 实现（TaskManager, PlanMode）
├── cli/            # 终端交互、命令注册
├── common/         # 通用工具（file_utils, time_wrapper, string_utils）
├── components/     # UI 组件（ChatPanel, InputPanel）
├── core/           # 核心逻辑（AgentCore, AgentCommander）
├── managers/       # 管理器（Session, Memory, Plugin, Permission, LocalModel）
├── mcp/            # MCP 协议客户端
├── media_engine/   # SDL/ImGui 渲染引擎
├── platform/       # 平台抽象层（跨平台 API、终端输入处理）
├── providers/      # LLM 提供者（Anthropic, Ollama, OpenAI）
├── scene/          # UI 场景（SDL app, home screen, galgame mode）
├── services/       # 外部服务（LSP, Cron）
└── tools/          # 工具实现
```

---

## 快速开始

### 环境要求

| 组件 | 要求 |
|------|------|
| 编译器 | C++17 或更高 |
| CMake | 3.20+ |
| 依赖 | spdlog、nlohmann/json、libcurl、OpenSSL（自动下载） |

### 构建

```bash
git clone https://github.com/Swair/prosophor.git
cd prosophor
make build
```

或手动：

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) && make install
```

跳过 llama.cpp 编译：

```bash
cmake .. -DPROSOPHOR_BUILD_LLAMA=OFF
```

### 运行

```bash
./build/install/bin/prosophor
```

### 配置

首次运行时配置文件生成在 `~/.prosophor/config.json`：

```json
{
  "default_provider": "anthropic",
  "providers": {
    "anthropic": {
      "api_key": "YOUR_API_KEY",
      "api_type": "anthropic-messages",
      "agents": {
        "default": { "model": "claude-sonnet-4-6", "temperature": 0.7 }
      }
    },
    "openai": {
      "api_key": "YOUR_API_KEY",
      "base_url": "https://api.openai.com/v1",
      "agents": { "default": { "model": "gpt-4o" } }
    },
    "ollama": {
      "base_url": "http://localhost:11434",
      "api_type": "openai-completions",
      "agents": { "default": { "model": "qwen2.5-coder:32b" } }
    }
  },
  "local_models": [
    {
      "model_path": "path/to/model.gguf",
      "port": 8080,
      "auto_start": false
    }
  ]
}
```

### 内置命令

| 命令 | 功能 | 命令 | 功能 |
|------|------|------|------|
| `/help` | 帮助 | `/clear` | 清除历史 |
| `/plan` | 计划模式 | `/compact` | 压缩上下文 |
| `/model` | 切换模型 | `/provider` | 切换 LLM |
| `/session` | 会话管理 | `/server` | 本地模型服务 |
| `/mcp` | MCP 服务器 | `/skill` | 技能管理 |
| `/setup` | 自动配置本地模型 | `/token` | Token 用量 |
| `/exit` | 退出 | | |

---

## 许可证

Apache-2.0 · [LICENSE](./LICENSE)

---

<div align="center">

**Made with C++**

</div>

# Prosophor

### The Proactive Agentic CLI — from passive response to proactive interaction

<div align="center">

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)](https://en.cppreference.com/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

</div>

![Star History](https://gitmemo.com/sharts/Swair/prosophor)

---

## 🎯 Overview

Prosophor is a **proactive Agentic CLI** built with C++. Beyond passive command-response, it features a **plugin-based proactive trigger architecture** that perceives context, predicts needs, and initiates interaction — bridging the gap between "passive response Agents" and "fixed rule engines."

| Dimension | Traditional CLI | Prosophor |
|-----------|----------------|-----------|
| **Interaction** | Passive response | **Proactive trigger** (periodic / idle / idle_once) |
| **Architecture** | Monolithic | **Plugin-based** (hot-swappable trigger plugins) |
| **LLM** | Single provider | Multi-LLM (Claude, Qwen, Ollama) |
| **Runtime** | Node.js / interpreted | **Native C++** (zero runtime dependency) |
| **License** | Proprietary | **Apache 2.0** |

### Core Positioning

| Aspect | Description |
|--------|-------------|
| **Nature** | Proactive Multimodal Interaction Agent (Proactive Agentic CLI) |
| **Runtime** | C++ native, zero runtime dependencies |
| **Core Capabilities** | Proactive perception, autonomous planning, tool invocation, environment feedback, iterative verification |
| **Interaction** | Passive response + Proactive trigger dual mode |
| **Supported LLMs** | Anthropic (Claude), Qwen (通义千问), Ollama (local models) |

---

## 🧠 Proactive Trigger Architecture — Core Innovation

Traditional tools wait for commands. Prosophor proactively perceives and responds through a **three-layer plugin architecture**:

```
┌─────────────────────────────────────────────────────────────┐
│              Plugin Community (Independent)                  │
│         Upload → Audit → Distribute → Update                │
└─────────────────────────────────────────────────────────────┘
                              │ Download/Update
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Plugin Layer ─ trigger script + mode config + ACTIVE.md    │
│  Scheduling   ─ periodic / idle / idle_once · priority      │
│  Execution    ─ AgentCore + 40+ tools + LLM linkage         │
└─────────────────────────────────────────────────────────────┘
```

**Trigger modes**:

| Mode | Trigger | Use Case |
|------|---------|----------|
| `periodic` | Every N seconds | Critical alerts (hardware temp, errors) |
| `idle` | After N seconds idle | Reminders, suggestions |
| `idle_once` | Once per idle session | One-time guidance |

**Plugin trigger flow**:
```
trigger returns true → Read ACTIVE.md → Invoke LLM → Generate response → Notify user
```

**Plugin example**:
```
active/cpu_temperature_monitor/
├── trigger          # Trigger script (any language, returns true/false)
├── trigger_mode.cfg # mode=periodic, interval=60, priority=10
└── ACTIVE.md        # LLM interaction script
```

**Active Interaction Manager** — session-based proactive interaction built into the core, providing proactive suggestions without plugin configuration.

> Full design: [Whitepaper (English)](./Whitepaper%20about%20Plugin-Based%20Proactive%20Multimodal%20Interaction%20Trigger%20Architecture.md) · [白皮书 (中文)](./基于插件化的主动式多模态交互触发架构白皮书.md)

---

## ✨ Core Features

### REACT Agent Loop

Understand → Plan → Tool Invocation → Observe → Verify → Iterate / Terminate

Autonomously generates task sequences (read file → modify code → run tests → fix errors), supporting multi-round iteration.

### Tool System — 40+ Built-in Tools

| Category | Tools |
|----------|-------|
| **File Operations** | `read_file`, `write_file`, `edit_file` |
| **Shell Execution** | `exec`, `bash` |
| **Search** | `glob`, `grep` |
| **Git** | `git_status`, `git_diff`, `git_log`, `git_commit`, `git_add`, `git_branch` |
| **LSP** | `lsp_diagnostics`, `lsp_go_to_definition`, `lsp_find_references`, `lsp_get_hover`, `lsp_document_symbols`, `lsp_workspace_symbols`, `lsp_format_document` |
| **Web** | `web_search`, `web_fetch` |
| **Interaction** | `ask_user_question`, `todo_write` |
| **MCP** | `mcp_list_tools`, `mcp_call_tool`, `mcp_read_resource` |
| **Agent** | `agent` (sub-task decomposition and delegation) |
| **Planning** | `plan_mode`, `task_tool` |
| **Scheduling** | `cron_scheduler` |
| **Worktree** | `worktree_tool` |
| **Token** | `token_count`, `token_usage` |

### Skill System

Skills are defined via `SKILL.md` frontmatter with environment gating (binaries, env vars, OS):

```markdown
---
name: git
description: Git version control operations
required_bins: [git]
---
```

### Permission Management

Allow / Deny / Ask rules matched by tool name, command pattern, path pattern. Fallback logic: auto-approve after 3 rejections.

```json
{
  "permission_rules": [
    { "tool_name": "bash", "command_pattern": "git *", "default_level": "allow" },
    { "tool_name": "read_file", "path_pattern": "/etc/*", "default_level": "deny" }
  ]
}
```

### Context Compression

Summary / Truncate / Hybrid strategies for long conversation history.

### LSP & MCP

- **LSP**: Multi-language server management — diagnostics, go-to-definition, find references, hover, symbols, formatting
- **MCP**: Model Context Protocol client — stdio / SSE / WebSocket transports, tool discovery and execution

### Session & Cron & Worktree

- **Session**: save / load / list / delete
- **Cron**: scheduled task management
- **Worktree**: Git worktree management

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    CLI Layer (cli/)                      │
│   CommandRegistry  │  InputHandler  │  UI               │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                 AgentCommander (core/)                   │
│           User interaction, command handling,            │
│           Agent execution orchestration                  │
└─────────────────────────────────────────────────────────┘
                            │
            ┌───────────────┼───────────────┐
            ▼               ▼               ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│   AgentCore     │ │ MemoryManager   │ │  SkillLoader    │
│ Message/Timeline│ │ Workspace files/│ │ Skill loading/  │
│ Tool execution  │ │ Session mgmt    │ │ parsing         │
└─────────────────┘ └─────────────────┘ └─────────────────┘
            │               │               │
            ▼               ▼               ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│  ToolRegistry   │ │ LspManager      │ │ McpClient       │
│ Tool registry/  │ │ LSP language    │ │ MCP protocol    │
│ execution       │ │ servers         │ │ client          │
└─────────────────┘ └─────────────────┘ └─────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│              LLM Providers (providers/)                  │
│   AnthropicProvider  │  QwenProvider  │  LLMProvider   │
└─────────────────────────────────────────────────────────┘
```

### Core Modules

| Module | Responsibility |
|--------|---------------|
| `AgentCommander` | System entry, orchestrates user interaction and Agent execution |
| `AgentCore` | Message processing, tool execution, LLM interaction core loop |
| `ActiveTriggerManager` | Plugin-based proactive trigger scheduling (periodic/idle/idle_once) |
| `ActiveInteractionManager` | Session-based proactive interaction |
| `MemoryManager` | Workspace file management, daily memory, file change monitoring |
| `SkillLoader` | SKILL.md parsing, environment gating, auto-dependency install |
| `ToolRegistry` | Tool registration, schema generation, execution routing |
| `LLMProvider` | LLM API abstraction (Anthropic / Qwen / Ollama) |
| `CompactService` | Context compression for long conversation history |
| `PermissionManager` | Tool invocation authorization (allow / deny / ask) |
| `LspManager` | LSP language server management, JSON-RPC |
| `McpClient` | MCP protocol client for external tool integration |
| `SessionManager` | Session save / load / restore |
| `CronScheduler` | Scheduled task scheduling |
| `TokenTracker` | Token usage tracking and cost calculation |

---

## 🚀 Quick Start

### Requirements

| Component | Requirement |
|-----------|-------------|
| Compiler | C++17 or later |
| CMake | 3.20+ |
| Dependencies | nlohmann/json, spdlog, libcurl (auto-downloaded) |

### Build

```bash
git clone https://github.com/Swair/prosophor.git
cd prosophor
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8 && make install
```

### Run

```bash
./build/install/bin/prosophor
```

### Configure

Config generated at `~/.prosophor/config.json` on first run:

```json
{
  "default_provider": "anthropic",
  "providers": {
    "anthropic": {
      "api_key": "YOUR_API_KEY",
      "api_type": "anthropic-messages",
      "agents": {
        "default": { "model": "claude-sonnet-4-6", "temperature": 0.7, "max_tokens": 8192 }
      }
    },
    "qwen": {
      "api_key": "YOUR_DASHSCOPE_KEY",
      "api_type": "openai-completions",
      "agents": { "default": { "model": "qwen-max" } }
    },
    "ollama": {
      "base_url": "http://localhost:11434",
      "api_type": "openai-completions",
      "agents": { "default": { "model": "qwen2.5-coder:32b" } }
    }
  }
}
```

**Environment variables**: `PROSOPHOR_CONFIG_PATH`, `PROSOPHOR_LOG_LEVEL`, `ANTHROPIC_API_KEY`, `QWEN_API_KEY`

### Built-in Commands

| Command | Function | Command | Function |
|---------|----------|---------|----------|
| `/help` | Help | `/clear` | Clear history |
| `/plan` | Plan mode | `/compact` | Compress context |
| `/model` | Switch model | `/provider` | Switch LLM |
| `/session` | Session mgmt | `/cron` | Scheduled tasks |
| `/mcp` | MCP servers | `/skill` | Skill mgmt |
| `/worktree` | Git worktree | `/task` | Task mgmt |
| `/token` | Token usage | `/exit` | Exit |

---

## 📁 Project Structure

```
Prosophor/
├── main_src/
│   ├── cli/                     # CLI interaction layer
│   ├── common/                  # Common utilities (file_utils, time_wrapper, log_wrapper)
│   ├── core/                    # Core business logic (AgentCommander, AgentCore, MemoryManager)
│   ├── providers/               # LLM providers (Anthropic, Qwen, Ollama)
│   ├── tools/                   # 40+ tool implementations
│   ├── mcp/                     # MCP protocol client
│   ├── managers/                # Managers (Token, Session, Worktree, ActiveTrigger, ActiveInteraction)
│   └── services/                # External services (LSP, Cron)
├── active/                      # Active trigger plugin directory
├── config/                      # Configuration files
├── docs/                        # Technical documentation
├── tests/                       # Test code
├── CMakeLists.txt
└── Makefile
```

---

## 📖 Documentation

| Document | Description |
|----------|-------------|
| [docs/CORE_ARCHITECTURE.md](./docs/CORE_ARCHITECTURE.md) | System architecture design |
| [docs/FEATURES.md](./docs/FEATURES.md) | Feature overview |

Whitepapers: [CN](./基于插件化的主动式多模态交互触发架构白皮书.md) · [EN](./Whitepaper%20about%20Plugin-Based%20Proactive%20Multimodal%20Interaction%20Trigger%20Architecture.md)
Theory: [CN](./主动式交互%20Agent%20理论建设.md) · [EN](./Theoretical%20Foundations%20of%20Proactive%20Interactive%20Agents.md)

---

## 🔧 Troubleshooting

| Issue | Check |
|-------|-------|
| **API Key Errors** | `api_key` correct? Env var priority? |
| **Tool Execution Failures** | `allowed_cmds` whitelist? Path in `allowed_paths`? |
| **Skills Not Loading** | `required_bins` exist? `os_restrict` matches? |
| **LSP Fails to Start** | Language server installed? `command` path correct? |

---

## 📄 License

Apache-2.0 · [LICENSE](./LICENSE)

## 📑 Citation

- Whitepaper: [10.5281/zenodo.19762803](https://doi.org/10.5281/zenodo.19762803) · CC BY 4.0
- Theory: [10.5281/zenodo.19762639](https://doi.org/10.5281/zenodo.19762639) · CC BY 4.0
- Source: [Apache-2.0](./LICENSE)

---

<div align="center">

**Made with C++** · If this helps, give a ⭐️ Star!

</div>

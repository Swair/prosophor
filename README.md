# AiCode

<div align="center">

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)](https://en.cppreference.com/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

</div>

---

## 🎯 Overview

> **Core Innovation**: AiCode is the **industry's first Agent CLI tool based on plugin-based proactive trigger architecture**, achieving a paradigm shift from "passive response" to "proactive predictive interaction."

AiCode is a **proactive multimodal interaction Agent CLI tool** developed in C++, adopting the REACT paradigm (Think-Act-Observe loop), achieving **proactive perception, proactive prediction, and proactive interaction** agent capabilities through the **industry's first plugin-based proactive trigger architecture**.

Designed based on the "Plugin-Based Proactive Multimodal Interaction Trigger Architecture" whitepaper, filling the gap between "passive response Agents" and "fixed rule engines."

### Core Positioning

| Feature | Description |
|---------|-------------|
| **Nature** | Proactive Multimodal Interaction Agent (Proactive Agentic CLI) |
| **Runtime** | Native C++ compilation, no runtime dependencies |
| **Core Capabilities** | Proactive perception, autonomous planning, tool invocation, environment feedback, iterative verification |
| **Interaction Paradigm** | Passive response + Proactive trigger dual mode |
| **Supported LLMs** | Anthropic (Claude), Qwen (Tongyi Qianwen), Ollama (local models) |

### Architecture Layers

```
┌─────────────────────────────────────────────────────────────┐
│ CLI Layer → Native terminal I/O, signal handling (Ctrl+C)   │
├─────────────────────────────────────────────────────────────┤
│ Intent Parsing → Command parsing, context loading           │
├─────────────────────────────────────────────────────────────┤
│ Agent Scheduling → Understand → Plan → Tool → Observe → Loop│
├─────────────────────────────────────────────────────────────┤
│ Tool Execution → File system, Shell commands, Web (40+ tools)│
├─────────────────────────────────────────────────────────────┤
│ Active Trigger → Plugin scan, 3-mode trigger, idle detection│
├─────────────────────────────────────────────────────────────┤
│ Environment Feedback → Capture output, errors, status       │
├─────────────────────────────────────────────────────────────┤
│ Output Validation → Code verification, test execution       │
└─────────────────────────────────────────────────────────────┘
```

---

## 🆚 Advantages over Claude Code

| Feature | AiCode | Claude Code |
|:--------|:------:|:-----------:|
| **Multi-LLM Support** | ✅ Anthropic, Qwen, Ollama | ❌ Claude only |
| **Local Deployment** | ✅ Fully local, data controllable | ❌ Cloud API dependent |
| **Open Source License** | ✅ Apache 2.0 | ❌ Proprietary |
| **Runtime** | ✅ C++ High Performance | ⚠️ Node.js |
| **Proactive Interaction** | ✅ Plugin-based proactive trigger (periodic/idle/idle_once) | ❌ Passive response only |

### Core Advantages

#### 1. Multi-LLM Provider Support 🔄
AiCode is not bound to a single model, supporting:
- **Anthropic (Claude)** - Via official API or compatible interface
- **Qwen (Tongyi Qianwen)** - Alibaba Cloud DashScope
- **Ollama** - Local model deployment
- Easy to extend new providers (inherit `LLMProvider` interface)

#### 2. Proactive Interaction Architecture 🎯

**The industry's first plugin-based framework for proactive interaction scenarios**, filling the gap between "passive response Agents" and "fixed rule engines."

| Dimension | Traditional Agent | Rule Engine Tools | AiCode Architecture |
|-----------|------------------|-------------------|---------------------|
| Trigger Mode | Passive response (user command) | Fixed rules (if-then) | **Plugin-based proactive trigger** |
| Extension | Modify kernel/config | Modify script/rules | **Plugin hot-swapping** |
| Response | LLM generation | Execute preset actions | **LLM scenario-based response** |
| Ecosystem | Skills/MCP (passive) | None | **Proactive interaction plugin community** |

**Core Innovations**:
- **Plugin Framework**: Trigger logic decoupled from kernel, supports plugin hot-swapping
- **Proactive Interaction**: Upgrade from "passive response" to "proactive prediction"
- **Ecosystem Loop**: Plugin community supports "development→review→distribution→feedback→iteration"

**Plugin Example Structure**:
```
active/
└── cpu_temperature_monitor/
    ├── trigger          # Trigger script (any language, returns true/false)
    ├── trigger_mode.cfg # Mode configuration (periodic/idle/idle_once)
    └── ACTIVE.md        # Interaction configuration (LLM linkage)
```

**Trigger Flow**:
```
trigger returns true → Read ACTIVE.md → Invoke LLM → Generate response → Notify user/Execute task
```

**Three-Mode Triggering**:
- **Periodic Mandatory Trigger** (periodic): Trigger periodically by configured interval, for critical events (hardware failures, etc.)
- **User Idle Trigger** (idle): Trigger when no interaction with LLM reaches threshold, for normal events (idle reminders, etc.)
- **Single Idle Trigger** (idle_once): Trigger once when idle threshold reached, auto-disable after trigger (one-time guidance)

#### 2. Fully Local Controllable 🔒
- Configuration file stored at `~/.aicode/config.json`, fully controllable
- Supports custom API endpoints (including local proxies)
- Configurable path whitelist/blacklist for fine-grained file access control
- Configurable command whitelist/blacklist for fine-grained command execution control

#### 3. Flexible Permission System ⚖️
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

#### 4. Extensible Skill System 🧩
Define skills via `SKILL.md` files, supporting:
- Environment gating (binaries, environment variables, OS restrictions)
- Automatic dependency installation
- Custom command registration

#### 5. High-Performance C++ Implementation ⚡
- Native compilation, no Node.js runtime dependencies
- Lower memory footprint
- Faster startup speed

#### 6. Complete LSP Support 💻
- Multi-language server concurrent management
- Diagnostics, go-to-definition, find-references, hover information
- Document symbols, workspace symbols, formatting

#### 7. Open Source License 📄
- Apache License 2.0 - Commercial use, modification, distribution
- Community-driven, transparent development

---

## ✨ Core Features

### 🎯 Proactive Interaction Architecture

AiCode proposes **the industry's first plugin-based architecture for proactive interaction scenarios**, filling the gap between "passive response Agents" and "fixed rule engines."

**Limitations of Traditional Solutions**:
- **Passive Response Agents** (ChatGPT, Claude): Trigger Agent Loop after waiting for user commands, unable to actively perceive scene changes
- **Fixed Rule Engines** (Hazel, AutoHotkey): if-then rules solidified, cannot extend, no intelligent response

**AiCode's Proactive Interaction Architecture**:

```
┌─────────────────────────────────────────────────────────────┐
│           Plugin Community (Ecosystem, Independent)          │
│           Plugin Upload, Security Check, Review,            │
│           Distribution, Update                              │
└─────────────────────────────────────────────────────────────┘
                            │ Download/Update
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                  Runtime Architecture (Three Layers)         │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Plugin Layer: Trigger logic decoupled from kernel  │   │
│  │     active/ directory, trigger + config + ACTIVE    │   │
│  └─────────────────────────────────────────────────────┘   │
│                            │                                │
│                            ▼                                │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Scheduling Layer: Three-mode graded triggering,     │   │
│  │     active perception & scheduling, LLM linkage     │   │
│  └─────────────────────────────────────────────────────┘   │
│                            │                                │
│                            ▼                                │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Execution Layer: Reuses Agent framework tools      │   │
│  │     Scenario interaction, multimodal output         │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

**Plugin Trigger Flow**:
```
trigger returns true → Read ACTIVE.md → Invoke LLM → Generate response → Notify user/Execute task
```

**Three-Mode Triggering**:
- **Periodic Mandatory Trigger** (periodic): Trigger periodically by configured interval, for critical events (hardware failures, etc.)
- **User Idle Trigger** (idle): Trigger when no interaction with LLM reaches threshold, for normal events (idle reminders, etc.)
- **Single Idle Trigger** (idle_once): Trigger once when idle threshold reached, auto-disable after trigger (one-time guidance)

> See: [Whitepaper](./Whitepaper%20abount%20Plugin-Based%20Proactive%20Multimodal%20Interaction%20Trigger%20Architecture.md)

### 🏃 REACT Paradigm - "Think-Act-Observe" Loop

```
Understand → Plan → Tool Invocation → Observe → Verify → Iterate/Terminate
```

AiCode adopts the **REACT (Reason + Act) paradigm**, autonomously generating task sequences (e.g., read file → modify code → run tests → fix errors), supporting multi-round iteration.

### 🛠️ Tool System - 40+ Built-in Tools

| Category | Tools |
|----------|-------|
| **File Operations** | `read_file`, `write_file`, `edit_file` |
| **Shell Execution** | `exec`, `bash` |
| **Search** | `glob`, `grep` |
| **Git** | `git_status`, `git_diff`, `git_log`, `git_commit`, `git_add`, `git_branch` |
| **LSP** | `lsp_diagnostics`, `lsp_go_to_definition`, `lsp_find_references`, `lsp_get_hover`, etc. |
| **Web** | `web_search`, `web_fetch` |
| **Interaction** | `ask_user_question`, `todo_write` |
| **MCP** | `mcp_list_tools`, `mcp_call_tool`, `mcp_read_resource` |
| **Agent** | `agent` (sub-task decomposition and delegation) |
| **Planning** | `plan_mode`, `task_tool` |
| **Scheduling** | `cron_scheduler` |
| **Worktree** | `worktree_tool` |
| **Token** | `token_count`, `token_usage` |

### 🧠 Skill System

Skills are defined via `SKILL.md` files, supporting environment gating and automatic installation:

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

### 🔐 Permission Management

Three permission levels:
- **Allow** - Auto-approve
- **Deny** - Auto-reject
- **Ask** - Requires user confirmation

Rule matching supports tool name, command pattern, path pattern, with fallback logic (auto-approve after 3 rejections).

### 💾 Context Compression

Three compression strategies:
- **Summary** - Generate AI summary of old messages
- **Truncate** - Keep only the last N messages
- **Hybrid** - Summary + keep recent messages (default)

### 🧩 MCP Protocol

Supports MCP (Model Context Protocol) server integration:
- stdio transport
- SSE/WebSocket transport
- Tool discovery and execution
- Resource reading

---

## 🚀 Quick Start

### Requirements

| Component | Requirement | Description |
|-----------|-------------|-------------|
| Compiler | C++17 or later | Supports modern C++ features |
| CMake | 3.20+ | Build system |
| Dependencies | nlohmann/json, spdlog, libcurl | Auto-downloaded |

### Build

```bash
# Clone repository
git clone https://github.com/your-repo/aicode.git
cd aicode

# Create build directory
mkdir -p build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j8

# Install
make install
```

### Run

```bash
# Using Makefile
make run

# Or run directly
./build/install/bin/aicode
```

### Configuration

On first run, configuration file will be generated at `~/.aicode/config.json`:

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
  }
}
```

### Environment Variables

Configuration can be overridden via environment variables:

| Variable | Description |
|----------|-------------|
| `AICODE_CONFIG_PATH` | Custom configuration file path |
| `AICODE_LOG_LEVEL` | Log level |
| `ANTHROPIC_API_KEY` | Anthropic API key |
| `QWEN_API_KEY` | Qwen API key |

---

## 📋 Usage

### Basic Interaction

After starting AiCode, you can input natural language commands:

```
$ aicode
AiCode v1.0.0

> Show me the file structure of the current directory
> Create a function to calculate Fibonacci sequence
> Run tests and fix failing cases
```

### Built-in Commands

| Command | Function |
|---------|----------|
| `/help` | Show help information |
| `/clear` | Clear conversation history |
| `/plan` | Enter/Exit plan mode |
| `/compact` | Manually compress context |
| `/model` | Switch model |
| `/provider` | Switch LLM provider |
| `/session` | Manage sessions (save/load/list) |
| `/mcp` | MCP server management |
| `/skill` | Skill management |
| `/cron` | Scheduled task management |
| `/worktree` | Git worktree management |
| `/task` | Task management |
| `/buddy` | Companion pet system |
| `/token` | View token usage |
| `/exit` | Exit program |

### Session Management

```bash
# Save current session
/session save my-session

# List all sessions
/session list

# Load session
/session load my-session

# Delete session
/session delete my-session
```

---

## 🏗️ Architecture

### System Architecture Diagram

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
| `AgentCommander` | System entry point, orchestrates user interaction and Agent execution (Singleton) |
| `AgentCore` | Message processing, tool execution, LLM interaction core loop |
| `MemoryManager` | Workspace file management, daily memory storage, file change monitoring |
| `SkillLoader` | SKILL.md parsing, skill gating checks, automatic dependency installation |
| `ToolRegistry` | Tool registration, Schema generation, execution routing |
| `LLMProvider` | LLM API abstract interface (Anthropic/Qwen/Ollama) |
| `CompactService` | Context compression, manages long conversation history (Singleton) |
| `PermissionManager` | Tool invocation authorization (allow/deny/ask, Singleton) |
| `PlanModeManager` | Structured task planning and tracking (Singleton) |
| `LspManager` | LSP language server management, JSON-RPC communication (Singleton) |
| `McpClient` | MCP protocol client, supports external tool integration (Singleton) |
| `SessionManager` | Session creation/save/load/restore |
| `TokenTracker` | Token usage tracking and cost calculation |
| `ReferenceParser` | File reference parsing (@filename:line) |
| `BuddyManager` | Companion pet system (ASCII art rendering) |
| `WorktreeManager` | Git worktree management |
| `CronScheduler` | Scheduled task scheduling |
| `BackgroundTaskManager` | Background task management |
| `ActiveTriggerManager` | Active trigger manager (plugin-based proactive interaction, Singleton) |
| `ActiveInteractionManager` | Active interaction manager (session-based proactive interaction) |

### Design Patterns

| Pattern | Application |
|---------|-------------|
| **Singleton** | `AgentCommander`, `CompactService`, `PermissionManager`, `PlanModeManager`, `LspManager`, `McpClient`, `SessionManager`, `ActiveTriggerManager` |
| **Strategy** | `LLMProvider` interface (`AnthropicProvider`, `QwenProvider`) |
| **Factory** | `ToolRegistry` registers and creates tool executors, `SkillLoader` parses and instantiates skills |
| **Observer** | `SessionOutputCallback` (session state change notification), `FileChangeCallback` (file change monitoring) |

---

## 📁 Project Structure

```
AiCode/
├── main_src/
│   ├── cli/                    # CLI interaction layer
│   │   ├── command_registry.*  # Command registration and execution
│   │   └── input_handler.*     # Terminal input handling
│   ├── common/                 # Common utilities
│   │   ├── config.*            # Configuration system
│   │   ├── constants.h         # Constants definition
│   │   ├── curl_client.*       # HTTP client
│   │   ├── log_wrapper.h       # Log wrapper
│   │   ├── file_utils.*        # File utilities
│   │   ├── time_wrapper.*      # Time utilities
│   │   └── string_utils.*      # String utilities
│   ├── core/                   # Core business logic
│   │   ├── agent_commander.*   # Main entry/orchestrator
│   │   ├── agent_core.*        # Agent core loop
│   │   ├── compact_service.*   # Context compression
│   │   ├── memory_manager.*    # File/memory management
│   │   ├── permission_manager.*# Permission management
│   │   ├── plan_mode.*         # Plan mode
│   │   ├── skill_loader.*      # Skill loading
│   │   ├── system_prompt.*     # System prompt builder
│   │   ├── session_manager.*   # Session management
│   │   ├── reference_parser.*  # File reference parsing
│   │   └── messages_schema.*   # Message data schema
│   ├── providers/              # LLM providers
│   │   ├── llm_provider.*      # Abstract interface
│   │   ├── anthropic_provider.*# Anthropic API
│   │   ├── qwen_provider.*     # Qwen API
│   │   └── ollama_provider.*   # Ollama API
│   ├── tools/                  # Tool implementations
│   │   ├── tool_registry.*     # Tool registry
│   │   ├── glob_tool.*
│   │   ├── grep_tool.*
│   │   ├── lsp_tool.*
│   │   ├── agent_tool.*        # Sub-agent tool
│   │   ├── task_tool.*         # Task tool
│   │   ├── worktree_tool.*     # Worktree tool
│   │   ├── cron_tool.*         # Cron tool
│   │   ├── background_run_tool.* # Background run tool
│   │   └── ...
│   ├── mcp/                    # MCP protocol
│   │   └── mcp_client.*        # MCP client
│   ├── managers/               # Managers
│   │   ├── buddy_manager.*     # Buddy system
│   │   ├── token_tracker.*     # Token tracking
│   │   ├── worktree_manager.*  # Worktree management
│   │   ├── background_task_manager.* # Background task management
│   │   ├── active_trigger_manager.* # Active trigger manager (plugin-based)
│   │   └── active_interaction_manager.* # Active interaction manager (session-based)
│   ├── common/                 # Common utilities (continued)
│   │   └── subprocess_wrapper.* # Subprocess execution with timeout
├── active/                     # Active trigger plugins directory
│   ├── cpu_temperature_monitor/
│   ├── file_organizer/
│   └── new_user_guide/
├── config/                     # Configuration files
├── docs/                       # Technical documentation
├── tests/                      # Test code
├── CMakeLists.txt              # Build configuration
└── Makefile                    # Quick build script
```

---

## 📖 Documentation

| Document | Description |
|----------|-------------|
| [docs/README.md](./docs/README.md) | Documentation index |
| [docs/ARCHITECTURE.md](./docs/ARCHITECTURE.md) | System architecture design |
| [docs/FEATURES.md](./docs/FEATURES.md) | Core features list |
| [docs/CONFIGURATION.md](./docs/CONFIGURATION.md) | Configuration guide |
| [Whitepaper (Chinese)](./基于插件化的主动式多模态交互触发架构白皮书.md) | Proactive interaction architecture whitepaper (Chinese) |
| [Whitepaper (English)](./Whitepaper%20abount%20Plugin-Based%20Proactive%20Multimodal%20Interaction%20Trigger%20Architecture.md) | Proactive interaction architecture whitepaper (English) |

---

## 🔧 Troubleshooting

### Common Issues

**1. API Key Errors**
- Check if `api_key` is correct
- Confirm environment variable priority

**2. Tool Execution Failures**
- Check `allowed_cmds` whitelist
- Confirm paths are within `allowed_paths`

**3. Skills Not Loading**
- Check if `required_bins` exist
- Confirm `os_restrict` matches current system

**4. LSP Fails to Start**
- Confirm language server is installed
- Check if `command` path is correct

---

## 📄 License

Licensed under the Apache License, Version 2.0.

See [LICENSE](./LICENSE) for details.

---

## 🙏 Acknowledgments

- **Anthropic** - Creators of Claude large language models
- **C++ Community** - Excellent system programming language and toolchain
- **All Contributors** - Thank you to every developer who has contributed to the project

---

<div align="center">

**Made with ❤️ and C++ 🦾**

If this project helps you, please give a ⭐️ Star to support!

</div>

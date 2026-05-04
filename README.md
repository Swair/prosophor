# Prosophor

### The Proactive Agentic CLI — from passive response to proactive interaction

<div align="center">

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)](https://en.cppreference.com/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

</div>

---

## Overview

Prosophor is a **proactive Agentic CLI** built with C++. Beyond passive command-response, it features a **plugin-based proactive trigger architecture** that perceives context, predicts needs, and initiates interaction.

| Dimension | Traditional CLI | Prosophor |
|-----------|----------------|-----------|
| **Interaction** | Passive response | **Proactive trigger** (periodic / idle / idle_once) |
| **Architecture** | Monolithic | **Plugin-based** (hot-swappable trigger plugins) |
| **LLM** | Single provider | Multi-LLM (Claude, Ollama, OpenAI-compatible, local GGUF) |
| **Runtime** | Node.js / interpreted | **Native C++** (zero runtime dependency) |
| **License** | Proprietary | **Apache 2.0** |

### Core Positioning

| Aspect | Description |
|--------|-------------|
| **Runtime** | C++ native, zero runtime dependencies |
| **Core Capabilities** | Proactive perception, autonomous planning, tool invocation, environment feedback |
| **Interaction** | Passive response + Proactive trigger dual mode |
| **Supported LLMs** | Anthropic (Claude), Ollama (local models), OpenAI-compatible (any API), llama.cpp (local GGUF) |

---

## Proactive Trigger Architecture — Core Innovation

Traditional tools wait for commands. Prosophor proactively perceives and responds through a **three-layer plugin architecture**:

```
Plugin Community (Upload → Audit → Distribute → Update)
                          │
                          ▼
Plugin Layer ─ trigger script + mode config + ACTIVE.md
Scheduling   ─ periodic / idle / idle_once · priority
Execution    ─ AgentCore + ToolRegistry + LLM linkage
```

**Trigger modes**:

| Mode | Trigger | Use Case |
|------|---------|----------|
| `periodic` | Every N seconds | Critical alerts (hardware temp, errors) |
| `idle` | After N seconds idle | Reminders, suggestions |
| `idle_once` | Once per idle session | One-time guidance |

---

## Core Features

### REACT Agent Loop

Understand → Plan → Tool Invocation → Observe → Verify → Iterate / Terminate

### Tool System

| Category | Tools |
|----------|-------|
| **File Operations** | `read_file`, `write_file`, `edit_file`, `file_search` |
| **Shell Execution** | `bash`, `background_run` |
| **Search** | `web_search`, `web_fetch` |
| **Git** | `git_status`, `git_diff`, `git_log`, `git_commit`, `git_add`, `git_branch`, `git_checkout`, `git_push` |
| **MCP** | `mcp_list_tools`, `mcp_call_tool`, `mcp_read_resource` |
| **Agent** | `agent` (sub-task decomposition and delegation) |
| **Planning** | `plan`, `task` |
| **Token** | `token_usage` |

### Skill System

Skills defined via `SKILL.md` frontmatter with environment gating:

```markdown
---
name: git
description: Git version control operations
required_bins: [git]
---
```

### Permission Management

Allow / Deny / Ask rules matched by tool name, command pattern, path pattern.

### Local Model Support (llama.cpp)

Built-in `llama-server` lifecycle management:

| Command | Function |
|---------|----------|
| `/server start` | Start local model server |
| `/server stop` | Stop server |
| `/server status` | Check server status |
| `/setup` | Auto-detect hardware + scan GGUF + configure |

Auto-start on launch via `auto_start: true` in config. Detection for NVIDIA GPU, Apple Silicon, and CPU thread count.

### Context Compression & Session Management

Summary / Truncate / Hybrid strategies. Session save/load/list/delete.

---

## System Architecture

```
Command Line / SDL UI
        │
        ▼
  AgentCommander — command dispatch, agent orchestration
        │
        ▼
  AgentCore — message processing, tool execution, LLM loop
        │
  ┌─────┼──────┬──────┬──────┐
  │     │      │      │      │
  ▼     ▼      ▼      ▼      ▼
Tools  MCP   Skills Memory  LSP
       │
       ▼
  LLM Providers (Anthropic / Ollama / OpenAI / llama.cpp)
```

### Module Layout

```
main_src/
├── agents/         # Agent implementations (TaskManager, PlanMode)
├── cli/            # Terminal interaction, command registry
├── common/         # Utilities (file_utils, time_wrapper, string_utils)
├── components/     # UI components (ChatPanel, InputPanel)
├── core/           # Core logic (AgentCore, AgentCommander)
├── managers/       # Managers (Session, Memory, Plugin, Permission, LocalModel)
├── mcp/            # MCP protocol client
├── media_engine/   # SDL/ImGui rendering engine
├── platform/       # Platform abstraction (cross-platform APIs, input handling)
├── providers/      # LLM providers (Anthropic, Ollama, OpenAI)
├── scene/          # UI scenes (SDL app, home screen, galgame mode)
├── services/       # External services (LSP, Cron)
└── tools/          # Tool implementations
```

---

## Quick Start

### Requirements

| Component | Requirement |
|-----------|-------------|
| Compiler | C++17 or later |
| CMake | 3.20+ |
| Dependencies | spdlog, nlohmann/json, libcurl, OpenSSL (auto-downloaded) |

### Build

```bash
git clone https://github.com/Swair/prosophor.git
cd prosophor
make build
```

Or manually:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) && make install
```

Skip llama.cpp build:
```bash
cmake .. -DPROSOPHOR_BUILD_LLAMA=OFF
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

### Built-in Commands

| Command | Function | Command | Function |
|---------|----------|---------|----------|
| `/help` | Help | `/clear` | Clear history |
| `/plan` | Plan mode | `/compact` | Compress context |
| `/model` | Switch model | `/provider` | Switch LLM |
| `/session` | Session mgmt | `/server` | Local model server |
| `/mcp` | MCP servers | `/skill` | Skill mgmt |
| `/setup` | Auto-configure local model | `/token` | Token usage |
| `/exit` | Exit |

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/CORE_ARCHITECTURE.md](./docs/CORE_ARCHITECTURE.md) | System architecture design |

---

## License

Apache-2.0 · [LICENSE](./LICENSE)

---

<div align="center">

**Made with C++** · If this helps, give a ⭐️ Star!

</div>

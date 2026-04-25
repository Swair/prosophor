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

Prosophor is a **proactive Agentic CLI** built with C++. Beyond passive command-response, it features a **plugin-based proactive trigger architecture** that perceives context, predicts needs, and initiates interaction.

| Dimension | Traditional CLI | Prosophor |
|-----------|----------------|-----------|
| **Interaction** | Passive response | **Proactive trigger** (periodic / idle / idle_once) |
| **Architecture** | Monolithic | **Plugin-based** (hot-swappable trigger plugins) |
| **LLM** | Single provider | Multi-LLM (Claude, Qwen, Ollama) |
| **Runtime** | Node.js / interpreted | **Native C++** (zero runtime dependency) |
| **License** | Proprietary | **Apache 2.0** |

---

## 🧠 Proactive Trigger Architecture

Traditional tools wait for commands. Prosophor proactively perceives and responds through a **three-layer plugin architecture**:

```
Plugin Community → Download/Update → Runtime Architecture

┌─────────────────────────────────────────────────────────────┐
│  Plugin Layer  ─ trigger script + mode config + ACTIVE.md   │
│  Scheduling    ─ periodic / idle / idle_once  · priority    │
│  Execution     ─ AgentCore + 40+ tools + LLM                │
└─────────────────────────────────────────────────────────────┘
```

**Trigger modes**:

| Mode | Trigger | Use Case |
|------|---------|----------|
| `periodic` | Every N seconds | Critical alerts (hardware temp, errors) |
| `idle` | After N seconds idle | Reminders, suggestions |
| `idle_once` | Once per idle session | One-time guidance |

**Plugin example**:
```
active/cpu_monitor/
├── trigger          # returns true/false
├── trigger_mode.cfg # mode=periodic, interval=60
└── ACTIVE.md        # LLM interaction script
```

> Full design: [Whitepaper (English)](./Whitepaper%20about%20Plugin-Based%20Proactive%20Multimodal%20Interaction%20Trigger%20Architecture.md) · [白皮书 (中文)](./基于插件化的主动式多模态交互触发架构白皮书.md)

---

## ✨ Features

- **REACT Agent Loop** — Understand → Plan → Tool → Observe → Verify → Iterate
- **40+ Built-in Tools** — File ops, Shell, Search, Git, LSP, Web, MCP, Agent sub-tasks
- **Multi-LLM** — Anthropic, Qwen, Ollama; extensible provider interface
- **Skill System** — `SKILL.md`-defined skills with environment gating
- **Permission Management** — allow/deny/ask rules by tool, command, path
- **Context Compression** — summary / truncate / hybrid strategies
- **LSP Support** — diagnostics, go-to-definition, find references, hover
- **Session & Cron** — session save/load, scheduled tasks, Git worktrees

---

## 🚀 Quick Start

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

On first run, config is generated at `~/.prosophor/config.json`:

```json
{
  "default_provider": "anthropic",
  "providers": {
    "anthropic": {
      "api_key": "YOUR_API_KEY",
      "agents": { "default": { "model": "claude-sonnet-4-6" } }
    }
  }
}
```

**Environment variables**: `PROSOPHOR_CONFIG_PATH`, `PROSOPHOR_LOG_LEVEL`, `ANTHROPIC_API_KEY`, `QWEN_API_KEY`

---

## 📖 Documentation

| Document | Description |
|----------|-------------|
| [docs/CORE_ARCHITECTURE.md](./docs/CORE_ARCHITECTURE.md) | System architecture |
| [docs/FEATURES.md](./docs/FEATURES.md) | Feature list |

Whitepapers: [CN](./基于插件化的主动式多模态交互触发架构白皮书.md) · [EN](./Whitepaper%20about%20Plugin-Based%20Proactive%20Multimodal%20Interaction%20Trigger%20Architecture.md)
Theory: [CN](./主动式交互%20Agent%20理论建设.md) · [EN](./Theoretical%20Foundations%20of%20Proactive%20Interactive%20Agents.md)

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

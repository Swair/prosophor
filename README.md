# Prosophor

<div align="right">

**English** | [**中文**](README.zh-CN.md)

</div>

<div align="center">

**The Proactive Agentic CLI — from passive response to proactive interaction**

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)](https://en.cppreference.com/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

</div>

---

## 📋 Table of Contents

- [Overview](#overview)
- [White Paper: Core Architecture](#white-paper-core-architecture)
- [Proactive Trigger Architecture — Core Innovation](#proactive-trigger-architecture--core-innovation)
- [Role System](#role-system)
- [LLM Provider System & Model Switching](#llm-provider-system--model-switching)
- [Tool System](#tool-system)
- [Configuration Guide](#configuration-guide)
- [MCP Protocol](#mcp-protocol)
- [SDL Desktop Pet UI](#sdl-desktop-pet-ui)
- [Context Compression & Memory](#context-compression--memory)
- [Permission Management](#permission-management)
- [Skill System](#skill-system)
- [LSP Integration](#lsp-integration)
- [Cron Scheduler](#cron-scheduler)
- [Slash Commands](#slash-commands)
- [Quick Start](#quick-start)
- [Build from Source](#build-from-source)
- [Project Structure](#project-structure)
- [License](#license)

---

## Overview

Prosophor is a **proactive Agentic CLI** built with **native C++17**. It transcends the traditional command-response paradigm by implementing a **plugin-based proactive trigger architecture** that perceives context, predicts needs, and initiates interaction autonomously.

| Dimension | Traditional CLI | Prosophor |
|-----------|----------------|-----------|
| **Interaction** | Passive response | **Proactive trigger** (periodic / idle / idle_once) |
| **Architecture** | Monolithic | **Plugin-based** (hot-swappable trigger plugins) |
| **LLM** | Single provider | **Multi-LLM** (Anthropic, OpenAI-compatible, Ollama, llama.cpp in-process) |
| **Runtime** | Node.js / interpreted | **Native C++** (zero runtime dependency) |
| **Frontends** | Terminal only | **Terminal TUI** + **SDL Desktop Pet** (multi-sprite, speech bubbles, TTS/ASR) |
| **License** | Proprietary | **Apache 2.0** |

### Core Positioning

| Aspect | Description |
|--------|-------------|
| **Runtime** | C++ native, zero runtime dependencies |
| **Core Capabilities** | Proactive perception, autonomous planning, tool invocation, environment feedback |
| **Interaction** | Passive response + Proactive trigger dual mode |
| **Supported LLMs** | Anthropic (Claude), OpenAI-compatible (DeepSeek, GPT, MiniMax, any), Ollama (local models), llama.cpp (in-process GGUF inference) |
| **Frontends** | Terminal TUI + SDL Desktop Pet (transparent sprite windows, multi-character, SpeechBubble, TTS voice, voice input) |
| **Extras** | LSP integration, Cron scheduler, MCP protocol, i18n, Sub-agent delegation, Plan mode |

---

## White Paper: Core Architecture

### Architectural Philosophy

Prosophor's architecture is built on a **clean separation between engine and presentation**. The `prosophor_core` static library contains all business logic with zero UI dependencies, while frontends (Terminal TUI and SDL desktop pet) register callbacks to receive output and handle permissions.

```
Frontend Layer:
  ┌───────────────────────┐      ┌──────────────────────────────┐
  │   Terminal TUI        │      │   SDL Desktop Pet            │
  │   (AiCoding)          │      │   (VirtualSprite)            │
  │   - Line input        │      │   - Multi-sprite windows     │
  │   - Stream output     │      │   - Transparent overlay      │
  │   - Permission prompts│      │   - SpeechBubble dialog      │
  │                       │      │   - TTS audio output         │
  │                       │      │   - Voice input (ASR)        │
  └──────────┬────────────┘      └────────────┬─────────────────┘
             │ SetOutputCallback              │
             │ SetPermissionCallback          │
             ▼                                ▼
  ┌────────────────────────────────────────────────────────────┐
  │  AgentEngine (Singleton)                                   │
  │  ┌──────────┬──────────┬──────────┬──────────┐             │
  │  │ Tool     │ Session  │ Provider │ Command  │             │
  │  │ Registry │ Manager  │  Router  │ Registry │             │
  │  ├──────────┼──────────┼──────────┼──────────┤             │
  │  │ Memory   │  LSP     │  MCP     │  Cron    │             │
  │  │ Manager  │ Manager  │  Client  │Scheduler │             │
  │  └──────────┴──────────┴──────────┴──────────┘             │
  └────────────────────────────────────────────────────────────┘
```

### AgentEngine — Central Orchestrator

`AgentEngine` is the singleton heart of Prosophor, providing several key methods to frontends: an output callback for receiving agent response state changes, a permission callback for tool authorization, methods to process user messages (which auto-detect slash commands and route accordingly), execute slash commands directly, switch the active role, and stop the current session.

**Initialization sequence**:
1. Load config from `~/.prosophor/settings.json`
2. Initialize MemoryManager (load workspace files, start file watcher)
3. Initialize ToolRegistry (register all built-in tools)
4. Initialize AgentSessionManager (create default session)
5. Initialize ProviderRouter (load provider configurations)
6. Initialize LspManager (start language servers)
7. Initialize CommandRegistry (register all slash commands)
8. Create the initial session with the default role
9. Auto-start local model server if `auto_start: true` is set

### REACT Agent Loop

`AgentCore::Loop()` implements the **REACT paradigm** (Reasoning + Acting):

```
User Message
    │
    ▼
  1. Process @file references
  2. Maybe compact context (DialogStrategy)
  3. Build ChatRequest from session state + role config
    │
    ▼
  4. Call LLM (stream or block)
     If thinking mode: emit kThinkingStart → kThinkingDelta → kThinkingEnd
     Emit kContentStart → kContentDelta → kContentEnd
    │
    ▼
  ┌─ Has tool calls? ─┐
  │                   │
  Yes                 No
  │                   │
  ▼                   ▼
  Execute each tool:  Has text response?
  - Check stop flag      │           │
  - Permission check   Yes          No
  - Execute tool         │           │
  - Truncate result      ▼           ▼
  - Append to history  Return     Error: unexpected
  iterations++         response   response format
  if < max_iterations:
    → back to step 3
  else: stop
```

**Key design decisions**:
- **Streaming-first**: When streaming is enabled, the loop pushes fine-grained stream events (`kThinkingStart/Delta/End`, `kContentStart/Delta/End`) for real-time UI rendering
- **Tool result truncation**: Large tool outputs are intelligently truncated, preserving head and tail lines with a `[... N lines omitted]` indicator
- **Stop-anytime**: A `stop_requested` atomic flag allows immediate interruption with proper error propagation
- **Automatic compaction**: Context compaction triggers before each LLM call when messages exceed configurable thresholds (via DialogStrategy)
- **Stateless loop**: `AgentCore` is stateless; all state lives in `AgentSession` for thread-safe concurrent sessions

### Provider System & Routing

The `ProviderRouter` dynamically routes requests based on role configuration, mapping roles to their bound providers either by role ID or by provider name.

**Four provider types**, all implementing a unified `LLMProvider` interface:
- `AnthropicProvider` — Anthropic Messages API with SSE streaming, thinking blocks, cache_control
- `OpenAIProvider` — OpenAI-compatible API (DeepSeek, GPT, MiniMax, any OpenAI-format)
- `OllamaProvider` — Local models via Ollama
- `LlamaCppProvider` — **In-process** llama.cpp inference (no HTTP server needed, native C++)

**Session-level provider override**: When a user switches provider or model at runtime (via `/model` or `/provider`), the session creates a mutable copy of the role, updates the provider instance, base URL, API key, and model parameters — all without affecting the shared role definition. This allows concurrent sessions with different overrides sharing the same role template.

### Memory Architecture

Prosophor implements a **dual-layer memory architecture** with **dialog summarization**:

```
                          Memory Architecture
  ┌──────────────────────────────┬──────────────────────────────┐
  │      Role Memory             │      Session History         │
  │      (Long-term)             │      (Short-term)            │
  ├──────────────────────────────┼──────────────────────────────┤
  │ ~/.prosophor/memories/       │ ~/.prosophor/sessions/       │
  │   {role_id}/                 │   {session_id}/history/      │
  ├──────────────────────────────┼──────────────────────────────┤
  │ Habits & preferences         │ Full conversation trace      │
  │ Learned patterns             │ Tool execution results       │
  │ Key decisions                │ Intermediate decisions       │
  │ Daily summaries              │ Workspace context            │
  ├──────────────────────────────┼──────────────────────────────┤
  │ Persists across projects     │ Within project session       │
  │ and sessions                 │ (compacted or cleared)       │
  └──────────────────────────────┴──────────────────────────────┘
```

**DialogStrategy** implements Bellman-decay summarization — each round auto-generates a summary that decays with a discount factor, injected as context in the next request. Key decisions persist indefinitely.

**MemoryConsolidationService**:
- Extracts **key decisions** from conversations in four categories: design decisions, code changes, unresolved issues, and lessons learned
- Triggers every N messages (configurable threshold, default 30)
- Saves consolidated summaries to role memory directory
- Generates session exit summaries for permanent record and cross-session continuity

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

### Trigger Modes

| Mode | Trigger | Use Case |
|------|---------|----------|
| `periodic` | Every N seconds | Critical alerts (hardware temp, errors) |
| `idle` | After N seconds idle | Reminders, suggestions |
| `idle_once` | Once per idle session | One-time guidance |

### Plugin Structure

Each plugin lives in its own directory under `~/.prosophor/active/{plugin_name}/`:

- `cpu_temperature_monitor/` — `trigger_mode.cfg` (mode=periodic, interval=60), `trigger.py` (detection script), `prompt.md` (LLM prompt with {variables})
- `file_organizer/` — `trigger_mode.cfg` (mode=idle, threshold=300), `trigger.py`, `prompt.md`
- `new_user_guide/` — `trigger_mode.cfg` (mode=idle_once, threshold=120), `trigger.py`, `prompt.md`

### ActiveInteractionManager — Post-Interaction Proactivity

Beyond scheduled triggers, post-interaction proactivity adds two behaviors:
- **5 minutes after user question**: Proactively recommend related questions
- **10 minutes after user question**: Summarize conversation and save to changelog

This closes the loop: not only does Prosophor proactively initiate, it also reflects on its own interactions to deepen context.

---

## Role System

Roles are defined as **JSON files** at `~/.prosophor/roles/`. Each role is a complete agent persona with its own provider binding, model, personality, tools, skills, and long-term memory.

### Built-in Roles (11 roles)

| Role | ID | Provider | Model | Personality |
|------|----|----------|-------|-------------|
| **Default Assistant** | `default` | llamacpp | google_gemma-4-E4B-it-Q4_K_M | Balanced, all-purpose |
| **Ayaka** | `ayaka` | anthropic | deepseek-v4-flash | Kamisato clan heiress, elegant and resolute |
| **Code Expert** | `coder` | llamacpp | google_gemma-4-E4B-it-Q4_K_M | Concise, coding-focused |
| **Architect** | `architect` | llamacpp | google_gemma-4-E4B-it-Q4_K_M | Structured, design-focused |
| **Reviewer** | `reviewer` | llamacpp | google_gemma-4-E4B-it-Q4_K_M | Detail-oriented, critical |
| **Teacher** | `teacher` | llamacpp | google_gemma-4-E4B-it-Q4_K_M | Patient, educational |
| **Kazuha** | `kazuha` | anthropic | deepseek-v4-flash | Wandering poet, calm and perceptive |
| **Keqing** | `keqing` | anthropic | deepseek-v4-flash | Decisive, pragmatic, efficient |
| **Sayu** | `sayu` | anthropic | deepseek-v4-flash | Lazy ninja, reliable when it counts |
| **Skirk** | `skirk-2` | anthropic | deepseek-v4-flash | Abyssal sword mentor, calm and profound |
| **Linnea** | `linnea-2` | anthropic | deepseek-v4-flash | Energetic little adventurer |

### Role Fields

A role definition includes: identity fields (ID, name, description, spritesheet path); provider binding (which provider protocol to use, default model); personality configuration (detailed personality prompt); system instructions; capability configuration (whitelisted skills and tools); behavioral constraints (max tool call iterations, auto-confirm tools); TTS voice setting; and a dedicated memory directory for long-term storage.

### Runtime Override Mechanism

When a user switches provider or model at runtime (via `/model` or `/provider` command), the system creates a mutable copy of the role, redirects the session's role pointer to this copy, updates the provider instance, base URL, API key, and timeout, searches provider entries for the matching model to find the correct endpoint, and leaves the shared role definition untouched. This design allows concurrent sessions with different overrides sharing the same role template.

---

## LLM Provider System & Model Switching

Prosophor abstracts all LLM providers behind a unified interface and supports dynamic switching at session, role, and command level — all with a single command, no restart needed.

### Simplicity First

Unlike other agent frameworks that require editing config files or restarting to change models, Prosophor lets you switch models with just **`/model [name]`** or select by index with **`/model [idx]`** (e.g., `/model 2` picks the second available model). The same simplicity applies to switching entire providers (`/provider ollama`) or swapping roles (`/role coder`) — each creates a new session with the new configuration automatically resolved.

### Supported Providers

| Provider | Protocol | Best For |
|----------|----------|----------|
| **Anthropic** | anthropic-messages (SSE streaming) | Claude models with thinking/reasoning, also DeepSeek via Anthropic-compatible endpoint |
| **OpenAI-compatible** | openai-chat-completions | DeepSeek, GPT, MiniMax, any OpenAI-format API |
| **Ollama** | openai-completions (via Ollama) | Local models (Qwen, Gemma, etc.) |
| **llama.cpp** | In-process C++ inference | GGUF local models, no HTTP server needed |

The abstract provider interface defines four core operations: a non-streaming chat method, a streaming chat method with per-event callbacks, and serialization/deserialization that each provider implements for its own wire format.

### Model Switching Commands

| Command | Operation |
|---------|-----------|
| `/model [name]` | Switch model by name (e.g., `/model gpt-4o`) |
| `/model [idx]`  | Switch model by index (e.g., `/model 2`) |
| `/provider [name]` | Switch provider (e.g., `/provider ollama`) |
| `/role [id]` | Switch role (creates new session with the role's default model) |
| `/setup` | Auto-detect hardware, scan GGUF models, generate config |

**Switching behavior**: `/model deepseek-v4-pro` or `/model 2` searches all provider entries for the model, finds the matching agent config, and updates the role's mutable copy. `/role coder` creates a fresh session using the coder role's provider/model. Provider-specific API keys and base URLs are automatically resolved from config — no file editing required.

### Configuration Structure

Each provider supports **multiple entries** (array), each with its own api_key and base_url. Each entry contains multiple **agent configs** with model, temperature, max_tokens, and context window settings. For example, an Anthropic provider entry might define a "default" agent using `qwen3.5-plus` with temperature 0.7 and a "fast" agent using `deepseek-v4-flash` with temperature 0.1.

### In-Process llama.cpp

The `LlamaCppProvider` runs llama.cpp **in-process** — no separate llama-server process needed. It loads GGUF models directly via the C++ API with configurable GPU layers, thread count, and context window. The `/setup` command auto-detects hardware (NVIDIA GPU, Apple Silicon, or CPU) and generates optimal configuration.

### Request Building Pipeline

The request assembler collects from session state: the model (from role, possibly overridden); the base URL (resolved from provider entry via agent config lookup); the API key (from provider entry); the full conversation history; the system prompt (role system prompt + personality + loaded memory files); the tool list (filtered by role's tool whitelist); and the thinking flag (enabled/disabled per request based on role config).

---

## Tool System

`ToolRegistry` centrally manages all tools with a schema-based registration system.

### Built-in Tools

| Category | Tools |
|----------|-------|
| **File Operations** | `read_file`, `write_file`, `edit_file`, `file_search`, `apply_patch` |
| **Shell Execution** | `bash`, `background_run` (long-running shell sessions) |
| **Search** | `web_search` (Brave/Tavily/DuckDuckGo), `web_fetch` |
| **Git** | `git_status`, `git_diff`, `git_log`, `git_commit`, `git_add`, `git_branch` |
| **MCP** | `mcp_list_tools`, `mcp_call_tool`, `mcp_list_resources`, `mcp_read_resource` |
| **Memory** | `memory_search`, `memory_get` |
| **Token** | `token_count`, `token_usage` |
| **Agent** | Sub-agent delegation via `SubagentCoordinator` |
| **Planning** | `plan` (PlanModeManager), `task` (TaskManager) |

### Tool Execution Flow

When the LLM responds with tool calls, each call goes through permission checking (Allow → continue, Deny → return error, Ask → user callback), then execution. Successful results are truncated to head and tail lines with a `[... N lines omitted]` indicator; errors are returned in full so the LLM has complete context for diagnosis. Results are appended to message history, and the loop continues.

### Permission Integration

Tool execution is gated by a permission manager supporting pattern matching by tool name, command pattern, and path pattern; three modes (Allow auto-approve, Deny auto-deny, Ask interactive); fallback logic that auto-allows after N sequential denials (default 3); and configurable levels: auto, default, and bypass.

---

## Configuration Guide

### Config File Location

First launch auto-generates config at **`~/.prosophor/settings.json`**.

### Configuration Overview

The configuration file has these top-level sections:

- **default_role**: The role(s) to activate on startup (e.g., `["default", "ayaka"]` for dual roles)
- **log_level**: Logging verbosity (debug, info, warn, error)
- **enable_summary**: Toggle Bellman-decay dialog summarization
- **providers**: Each provider type (anthropic, openai, ollama, llamacpp) maps to an **array** of entries. Each entry has its own api_key, base_url, timeout, and a map of agent configs keyed by name. Each agent config specifies model name, temperature, max_tokens, context_window, and whether thinking is enabled.
- **local_models**: For llamacpp provider: model_path, n_gpu_layers (-1 = all layers on GPU, 0 = CPU only), n_threads (0 = auto-detect), context_size, auto_start, start_timeout_ms
- **tts**: Local TTS configuration — MeloTTS via sherpa-onnx (Windows), aidvoice NPU (NutriBot)
- **asr**: Local ASR configuration — SenseVoice via sherpa-onnx (Windows), aidvoice NPU (NutriBot); model dir, language, threads, push-to-talk
- **vad**: VAD backend/tuning (WebRTC on desktop, energy on NutriBot)
- **security**: Contains permission_level (auto, default, or bypass) and allow_local_execute toggle

### Local Model Setup

The `/setup` command automates local model configuration:
1. Detects **NVIDIA GPU** via nvidia-smi and sets GPU layers to all
2. Detects **Apple Silicon** via sysctl and enables Metal
3. Falls back to **CPU** with auto-detected thread count
4. Scans common directories for `.gguf` files
5. Generates `settings.json` with optimal parameters

### Model Auto-Start

When `auto_start: true` is set, the engine automatically starts in-process llama.cpp inference before creating any sessions.

---

## MCP Protocol

Prosophor implements a full **Model Context Protocol (MCP)** client for standardized tool/resource/prompt sharing.

### Transports

| Transport | Description |
|-----------|-------------|
| **stdio** | Server as subprocess stdin/stdout |
| **SSE** | Server-Sent Events over HTTP |
| **WebSocket** | Bidirectional WebSocket |

### MCP Server Management

- `/mcp list` — List connected servers
- `/mcp add <name> <type> <config>` — Add a server
- `/mcp remove <name>` — Remove a server

### MCP Client Capabilities

The MCP client can connect to servers (stdio, SSE, or WebSocket), discover tools, call tools, read resources, and retrieve prompts. MCP tools are dynamically registered into the ToolRegistry (using `mcp__{server}__{tool}` naming convention) and exposed to the LLM as native tools.

---

## SDL Desktop Pet UI

When built with `PROSOPHOR_SDL_UI=ON`, Prosophor launches as a **desktop pet** application powered by SDL3.

<div align="center">

![SDL Desktop Pet](docs/images/show.png)
**SDL desktop pet — transparent sprite window with speech bubble and character animation**

</div>

### Architecture

```
VirtualSprite (Singleton)
  ┌─────────────────────────────────────────────┐
  │  Manages central window, input forwarding    │
  └─────────────────────────────────────────────┘
        │
        ▼
  SpriteManager (Singleton)
  ┌─────────────────────────────────────────────┐
  │  Multiple Sprite instances, focus tracking   │
  └─────────────────────────────────────────────┘
      /  |  \
     ▼   ▼   ▼
  Sprite  Sprite  Sprite
  (Transparent window with pet animation, speech bubble, nav buttons, own session)
```

### Features

| Feature | Description |
|---------|-------------|
| **Multi-sprite** | Each sprite is a transparent overlay window with its own AgentEngine session |
| **SpeechBubble** | Cloud-shaped dialog bubble with title bar, messages, input area, send button, resize handle |
| **Character animations** | State-driven pet animations via `StateToAction()` mapping (9 action types) |
| **TTS audio** | Offline MeloTTS speech synthesis (sherpa-onnx, Windows) |
| **Voice input** | Offline SenseVoice ASR with push-to-talk and VAD (sherpa-onnx, Windows) |
| **SDL chat window** | Central chat panel with message history + input |

### UI Component Architecture (Three-Layer)

```
media_engine/       → 引擎原语：PanelContainer（页面壳）、DrawList（RoundRect/Text/Channels）、Child
virtual_sprite/     → SDL 前端：20+ 业务视图 + components/ 可复用组件（Card（参数化背景+边框+标题+Field）、ItemList、BorderedContainer 等）
```

### Components

| Component | Layer | Description |
|-----------|-------|-------------|
| **PanelContainer** | media_engine | 页面壳：白底圆角 + OrangeDeep 标题 + 内容区坐标 |
| **DrawList** | media_engine | 2D 绘图原语：RoundRect、Text、ChannelsSplit/Merge 等 |
| **Card** | components | 通用卡片：背景+边框+标题+Field()+自适应/固定高度，ChannelsSplit 正确图层 |
| **BorderedContainer** | components | 带边框 ScopedChild，可选底色/圆角/定位 |
| **ItemList** | components | 可选中列表容器（Y 追踪 + SelectableItem） |
| **SelectableItem** | components | 三态列表项（InvisibleButton + 选中/悬浮/中性） |
| **SplitPanel** | components | 左右分栏坐标计算 |
| **ActionBar** | components | 底部 Save/Cancel 按钮栏 |
| **FocusCard** | components | OrangeLightest + OrangeWarm 三态卡片 |
| **WhiteCard** | components | White + CreamBorder 简化卡片 |
| **StatCard** | components | 白底 + 色条统计指标卡 |
| **ModelCard** | panels | BorderedContainer + 4 字段（model/temperature/max_tokens/context_window） |
| **ProviderEntryCard** | panels | BorderedContainer + 字段 + ModelCard × N |
| **VirtualSprite** | app | Top-level SDL application entry |
| **Sprite** | app | Per-character transparent window with animation |
| **SpriteManager** | app | Multi-sprite lifecycle management |
| **ChatWindow** | app | Central window + view router (20+ views) |
| **SpeechBubble** | components | Inline dialog bubble above sprites |
| **Spritesheet** | components | Multi-frame sprite animation system |
| **ChatPanel** | components | Chat message history display |
| **StatusBar** | components | Agent state → visual color mapping |
| **VoiceManager** | app | Voice I/O service (TTS + ASR) |

### State-to-Visual Mapping

The agent state observer maps runtime states to visual properties: `THINKING` triggers character thinking animation, `EXECUTING_TOOL` shows a tool execution indicator, `STREAM_CONTENT_TYPING` displays typing animation, and `STATE_ERROR` shows an error state visual.

---

## Context Compression & Memory

### FixedBuffer — Ring Buffer Pattern

A core utility used extensively across the engine for fixed-capacity, lock-free circular buffering. The `FixedBuffer<T, kCapacity>` always retains the most recent `kCapacity` items, overwriting the oldest when full — ideal for sliding-window contexts, recent message history, audio sample buffers, and log rolling.

> **Live demo**: The code and diagrams below are **real** — they come from the [Gemma 4](https://ai.google.dev/gemma) model running **in-process** via the local `LlamaCppProvider` (no HTTP server, native C++ inference). The entire example is generated by a local llamacpp invocation calling `google_gemma-4-E4B-it-Q4_K_M`, demonstrating both the ring buffer utility and Prosophor's local-first, in-process GGUF inference capability.

**Three operational states**:

<div align="center">

| Filling (below capacity) | Full (steady-state) | Overwrite (wrap-around) |
|:---:|:---:|:---:|
| ![Ring Buffer Case 1](docs/images/ringbuf_case_1.png) | ![Ring Buffer Case 2](docs/images/ringbuf_case_2.png) | ![Ring Buffer Case 3](docs/images/ringbuf_case_3.png) |

</div>

The implementation from `main_src/base/common/list_buffer.h`:

```cpp
template<typename T, int kCapacity>
class FixedBuffer {
public:
    void Push(const T& item) {
        data_[head_] = item;
        head_ = (head_ + 1) % kCapacity;
        if (count_ < kCapacity) count_++;
    }

    std::vector<T> ReadAll() const {
        std::vector<T> out;
        if (count_ == 0) return out;
        out.reserve(count_);
        int start = (head_ - count_ + kCapacity) % kCapacity;
        for (int i = 0; i < count_; i++)
            out.push_back(data_[(start + i) % kCapacity]);
        return out;
    }

private:
    T data_[kCapacity];
    int head_ = 0;    // next write position
    int count_ = 0;   // number of items stored
};
```

- **Case 1 (`count_ < kCapacity`)**: Buffer still filling — `Push()` writes at `head_`, increments `head_` and `count_`, no data overwritten yet.
- **Case 2 (`count_ == kCapacity`)**: Buffer full — subsequent `Push()` starts overwriting from the beginning (index 0).
- **Case 3 (`head_` wraps around)**: After reaching the end of the array, `head_` wraps to 0 via modular arithmetic, and `ReadAll()` reconstructs items in insertion order.

### DialogStrategy

Three compaction strategies via strategy pattern:

| Strategy | Behavior |
|----------|----------|
| **Summary** | Generate AI summary of old messages, keep only summary |
| **Truncate** | Keep only recent N messages, discard rest |
| **Hybrid** | Generate summary + keep recent N messages |

**Trigger conditions** are configurable per role: auto-compaction toggle, message count threshold (default 100), number of recent messages to always keep (default 20), and token count threshold (default ~100k tokens).

### Dialog Summarization (Bellman Decay)

Zero extra API calls — summaries are auto-generated within the LLM's response:

```
ApplyDialogStrategy (pre-request):
  [ts]
  [summary]  ← previous round's running_summary (if any)
  {message}

  → LLM appends [summary] at reply end

ExtractDialogSummary (post-response):
  assistant.summary = [summary] content → injected next round
  [summary] stays in content → cache prefix uninterrupted
```

**Recurrence**: `summary_each_round = current_content + γ × previous_summary`, with key decisions never decaying.

**Toggle**: Set `"enable_summary": false` in settings.json to revert to pure conversation mode.

### MemoryConsolidationService

Provides structured memory management through **key decision extraction** — parsing conversation into structured records with type (design decision, code change, unresolved issue, or lesson learned), content description, related files, and timestamp. Also generates **session exit summaries** that save to role memory for cross-session continuity.

### Token Tracker

Records per-model and aggregate token usage (input, output, cache read/write), API timing and retry counts, cost estimation with configurable per-model pricing, tool invocation metrics, web search requests, and code change statistics.

---

## Permission Management

Three-level permission system with rule matching ordered as: Deny rules → Ask rules → Allow rules. If a rule matches, its action is applied; unmatched rules fall back to the default mode.

**Rule patterns** use glob matching on tool names (e.g., `bash`, `read_*`, `*`), command patterns for the bash tool (e.g., `git *`, `npm install *`), path patterns for file tools, and argument value filtering.

**Denial tracking**: After 3 denials of the same tool, the system auto-allows to reduce noise.

---

## Skill System

Skills are defined via `SKILL.md` files with YAML frontmatter, supporting environment gating (required binaries, environment variables, OS restrictions), multiple install methods (npm, go, pip, brew, download), slash commands (auto-registered as `/skillname`), and multi-directory loading with dedup.

### Built-in Skills

| Skill | Description |
|-------|-------------|
| **md2paper** | Convert Markdown to academic paper Word/PDF (supports Chinese and arXiv formats) |
| **architecture_designing** | Internal architecture documentation |

---

## LSP Integration

The LSP manager provides Language Server Protocol integration: go to definition, find references, hover information, document and workspace symbols, diagnostics, code formatting, and LSP server lifecycle management. Supports per-file-type LSP server registration with JSON-RPC over stdio.

---

## Cron Scheduler

Built-in cron scheduler with standard 5-field cron expressions, recurring and one-shot tasks, durable persistence to `.claude/scheduled_tasks.json`, and task pause/resume/delete/run-now operations via `/schedule` command.

---

## Slash Commands

| Command | Function | Command | Function |
|---------|----------|---------|----------|
| `/help` | Show help | `/clear` | Clear history |
| `/plan` | Plan mode | `/compact` | Compress context |
| `/model` | Switch model | `/provider` | Switch LLM provider |
| `/role` | Switch role | `/roles` | List roles |
| `/session` | Session management | `/sessions` | List sessions |
| `/resume` | Resume last session | `/setup` | Auto-configure local model |
| `/mcp` | MCP servers | `/skills` | List/use skills |
| `/cost` | Cost statistics | `/status` | Git status |
| `/diff` | Show git diff | `/commit` | Git commit |
| `/tasks` | Task management | `/config` | Edit config |
| `/context` | Show context | `/doctor` | System diagnostics |
| `/effort` | Set reasoning effort | `/auto-commit` | Auto-commit toggle |
| `/memory` | Memory management | `/summary` | Session summary |
| `/permissions` | Permission settings | `/history` | Session history |
| `/workspace` | Set/show workspace | `/schedule` | Cron task management |
| `/plugins` | Plugin management | `/token` | Token usage |
| `/bye` | Exit | | |

---

## Quick Start

### Install

**macOS / Linux** (one command):
```
curl -fsSL https://aicodingbox.com/install.sh | bash
```

**Windows** (PowerShell):
```
irm https://aicodingbox.com/install.ps1 | iex
```

Or via package manager:

| Platform | Command |
|----------|---------|
| **Homebrew** (macOS/Linux) | `brew install Swair/tap/prosophor` |
| **Scoop** (Windows) | `scoop install prosophor` |
| **WinGet** (Windows) | `winget install Swair.prosophor` |

### Run

```
prosophor
```

First launch auto-generates **`~/.prosophor/settings.json`**. Edit it to add your API keys and configure providers.

### Quick Configuration

At minimum, set up at least one provider with your API key and model. The config file uses a JSON format with provider entries as arrays — each entry has its own api_key and base_url, and contains named agent configs specifying model, temperature, and other parameters.

---

## Build from Source

### Requirements

| Component | Requirement |
|-----------|-------------|
| Compiler | C++17 or later (GCC 9+, Clang 12+, MSVC 2019+) |
| CMake | 3.20+ |
| Dependencies | spdlog, nlohmann/json, libcurl, OpenSSL, OpenMP (auto-fetched) |

### Build

```
git clone https://github.com/Swair/prosophor.git
cd prosophor
make build_tui            # TUI (default full; _fast for remote-only)
make build_sdl            # SDL desktop pet (default full; _fast for remote-only)
make build_web            # web server
```

Or manually:

```
mkdir -p build/tui_full && cd build/tui_full
cmake ../.. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(nproc) && cmake --install .
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `PROSOPHOR_BUILD_LLAMA` | ON | Build in-process llama.cpp for local GGUF inference |
| `PROSOPHOR_BUILD_LLAMA_CUDA` | Linux:ON, Win:OFF | CUDA GPU support for llama.cpp |
| `PROSOPHOR_BUILD_LLAMA_VULKAN` | Win:ON, else:OFF | Vulkan GPU support for llama.cpp |
| `PROSOPHOR_BUILD_SHERPA_VOICE` | Win SDL:ON, else:OFF | sherpa-onnx local TTS/ASR (MeloTTS + SenseVoice) |
| `PROSOPHOR_SDL_UI` | OFF | Build SDL desktop pet UI (requires SDL3 dev libs) |

### Build Variants (Makefile)

| Target | Description |
|--------|-------------|
| `build_tui_fast` | Terminal, remote API only (no local model), fastest build |
| `build_tui_full` | Terminal + local model |
| `build_sdl_fast` | Desktop pet, remote API only |
| `build_sdl_full` | Desktop pet + local model |
| `build_web` | Web server |

Target names are platform-neutral: the Makefile detects the host automatically (Unix → CMake + make with CUDA GPU defaults, Windows → MSYS2/MinGW + Ninja with Vulkan GPU defaults). Each variant has matching `run_<variant>` and `clean_<variant>` targets, e.g. `make run_sdl_full`.

### Package & Deploy (Windows only)

| Target | Description |
|--------|-------------|
| `package_sdl_full` / `package_sdl_fast` | Build + NSIS installer (requires MSYS2/MinGW) |
| `deploy_sdl_full` / `deploy_sdl_fast` | Package + publish to GitHub Releases (`gh` CLI required) |
| `deploy_all_sdl_full` / `deploy_all_sdl_fast` | Deploy to GitHub + Gitee (Gitee requires `GITEE_TOKEN`) |

Release notes are automatically extracted from `CHANGELOG.md` for the matching version. Fallback to `--generate-notes` if no entry found.

### Tests

```
make tests
```

Or:

```
cd build && ctest --output-on-failure
```

---

## Project Structure

```
├── CMakeLists.txt              # Top-level CMake build
├── Makefile                    # Convenience build targets
├── config/
│   └── .prosophor/             # Configuration directory
│       ├── settings.json       # Main configuration
│       ├── roles/              # Agent role definitions (11 roles)
│       ├── skills/             # Skill definitions
│       ├── active/             # Proactive trigger plugins
│       ├── lang/               # i18n (en.json, zh-CN.json)
│       ├── memories/           # Long-term memory per role
│       ├── assets/             # Sprite assets per character
│       └── workspace/          # Default workspace
├── main_src/
│   ├── prosophor_core/         # Static library (zero UI deps)
│   │   ├── agent_engine.cc/h   # Central orchestrator (singleton)
│   │   ├── command_registry.cc/h  # 31+ slash commands
│   │   ├── core/               # AgentCore (stateless loop), DialogStrategy, MemoryConsolidation
│   │   ├── agents/             # SubagentCoordinator, TaskManager, PlanModeManager
│   │   ├── config/             # Configuration system
│   │   ├── managers/           # Session, memory, plugin, permission, trigger, token, skill, background task mgmt
│   │   ├── mcp/                # MCP protocol client (stdio/SSE/WebSocket)
│   │   ├── network/            # HTTP client (libcurl), WebSocket client
│   │   ├── providers/          # LLM providers (Anthropic, OpenAI, Ollama, LlamaCpp), TTS, ASR
│   │   ├── services/           # LSP, Cron, TTS speaker
│   │   └── tools/              # 16+ tool implementations
│   ├── ai_coding/              # Terminal TUI frontend (AiCoding)
│   ├── common/                 # Utilities (file, string, time, logging, i18n, thread pool)
│   ├── platform/               # Cross-platform abstraction (Win32/POSIX)
│   ├── media_engine/           # UI引擎层：PanelContainer（页面壳）、DrawList（2D绘图原语）、Child/ScopedChild（子窗口）
│   ├── scene/                  # SDL scenes
│   └── virtual_sprite/         # SDL 前端：ChatWindow（主窗口+视图路由）、panels/（20+业务视图）、components/（可复用控件：Card, ItemList, SpeechBubble 等）
├── tests/                      # GoogleTest-based tests
│   ├── unittest/               # Stream handler tests, TTS, ASR tests
│   └── benchmark/              # Performance benchmarks
├── scripts/                    # llama-server start/stop scripts
├── assets/                     # Game assets (images, audio)
└── docs/                       # Documentation
```

---

## License

Apache-2.0 · [LICENSE](./LICENSE)

---

<div align="center">

**Made with C++** · If this helps, give us a ⭐️ Star!

</div>

## Star History

<a href="https://www.star-history.com/?type=date&repos=Swair%2Fprosophor">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Swair/prosophor&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Swair/prosophor&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Swair/prosophor&type=date&legend=top-left" />
 </picture>
</a>

# 模块: prosophor_core — 核心业务逻辑

## 用途
所有前端（TUI 和 SDL）共享的核心业务逻辑层。管理 LLM 提供商、会话、工具注册、命令、内存/文件系统、技能、MCP 客户端、LSP 集成、计划模式、任务管理、子 Agent 协调、权限、Token 跟踪、定时任务、对话策略（消息预处理 + 上下文压缩）。

## 关键子目录与文件

- `agent_engine.h` — 顶层 `AgentEngine` 单例（会话管理、路由、前端回调）
- `core/` — `AgentCore`（消息循环）、`AgentSession`（每会话状态）、`AgentRuntimeState`、`AgentRole`、`MessageSchema`、`DialogStrategy`、`ReferenceParser`、`MemoryConsolidationService`
- `config/` — `ProsophorConfig`、`EffortConfig`、`LlmProviderConfig`、`ToolConfig`、`SkillsConfig`、`SecurityConfig`
- `providers/` — `LLMProvider` 抽象基类 + Anthropic/OpenAI/Ollama 实现 + 流处理器
  （ASR/TTS provider 已下沉到 `base/voice_interaction/providers/`）
- `tools/` — `ToolRegistry`（文件、bash、web、内存、git、MCP、补丁操作）
- `managers/` — 会话、内存、权限、技能、插件、Token、后台任务、主动触发管理器
- `mcp/` — `McpClient`（stdio/SSE/WebSocket）
- `services/` — LSP 管理器、定时任务调度器（TTS 语音见 `base/voice_interaction/`）
- `network/` — HTTP 客户端 + 本地模型工具
- `agents/` — 计划模式、任务管理器、子 Agent 协调
- `command_registry.h` — 约 30 个内置命令

## 依赖
- `common/`（工具）、`platform/`（进程/编码）
- 外部: spdlog, nlohmann_json, libcurl, OpenSSL

## 责任约束
- **不**引用 `media_engine/`、`scene/`、`virtual_sprite/`、`ai_coding/`
- **不**包含任何 UI 渲染代码
- 编译为静态库，TUI 和 SDL 前端都链接
- 会话状态必须线程安全（shared_mutex 用于快照）

## 公开接口

前端（ai_coding / virtual_sprite / nutribot / web_server）只通过 `public/` 转发头依赖 core：

| 公开头 | 用途 |
|--------|------|
| `public/agent_engine.h` | 顶层门面 |
| `public/core_types.h` | 共享数据类型（agent_types / messages_schema / session_key） |
| `public/config.h` | 配置 schema（设置 UI） |
| `public/role_config.h` | 角色配置编辑 |
| `public/token_tracker.h` | 用量面板 |
| `public/memory.h` | 记忆文件管理面板 |
| `public/skill.h` | 技能加载/编辑面板 |
| `public/security.h` | 权限规则面板 |
| `public/mcp.h` | MCP 服务器面板 |

provider 就绪状态走 `AgentEngine::GetProviderStatus`，前端不直接接触 `LLMProvider`；
LLM 回合统一走 `AgentCore::Loop`；NutriBot 周期感知使用独立 session（无工具、保留文字上下文，图片只服务当轮），状态隔离在 session 内；
主动触发插件状态走 `AgentEngine::ListActiveTriggerPlugins`，前端不直接接触 `ActiveTriggerManager`。

内部头（`core/`、`managers/`、`providers/`、`tools/` 等，`public/` 除外）不得由前端直接 include。
检查：`python3 scripts/check_public_includes.py`（默认 strict；迁移期可加 `--warn-only`）。
迁移期 CMake 仍暴露全部 include 目录；allowlist 清零后可改为只 PUBLIC `public/`。

## 代码组织关系

```
┌══════════════════════════════════════════════════════════┐
│                      agent_engine.h                       │
│              顶层编排器（前向声明所有管理器）                  │
└──────────┬────────────────────────────┬──────────────────┘
           │                            │
           ▼                            ▼
┌─────────────────────┐   ┌──────────────────────────┐
│ AgentSessionManager  │   │      ProviderRouter       │
│ （会话生命周期、角色加载） │   │ （按角色路由到具体提供商）    │
└──────────┬──────────┘   └──────────┬───────────────┘
           │                         │
           ▼                         ▼
┌─────────────────────┐   ┌──────────────────────────┐
│     AgentCore        │   │  ┌──────────────────┐   │
│  （无状态消息循环）     │   │  │AnthropicProvider │   │
│                      │   │  │OpenAIProvider    │   │
│  接收 AgentSession&   │   │  │OllamaProvider   │   │
│  驱动 compact/memory  │   │  └──────────────────┘   │
└──────────┬──────────┘   └──────────────────────────┘
           │
           ▼
┌────────────────────────────────────────────────────┐
│                   AgentSession                      │
│  （每会话运行时状态）    → DialogStrategy            │
│                        → MemoryConsolidationService │
│                        → ProviderRouter             │
│                        → LLMProvider                │
│                        → AgentRole                  │
└────────────────────────────────────────────────────┘

┌─────────────┬───────────────┬──────────────────┐
│             │               │                  │
▼             ▼               ▼                  ▼
┌──────────┐ ┌──────────┐ ┌────────────┐ ┌──────────────┐
│ToolRegistr│ │CommandReg│ │ActiveTriggr│ │MemoryManager  │
│y          │ │istry     │ │Manager     │ │  （非单例）    │
│→Permissio │ │→AgentSess│ │→AgentSessi │ │              │
│nMgr       │ │ion       │ │onManager   │ │              │
│→McpClient │ │          │ │            │ │              │
│→BackgndRun│ │          │ │            │ │              │
└──────────┘ └──────────┘ └────────────┘ └──────────────┘

┌──────────────────────────────────────────────────────┐
│             自包含叶子模块（零内部依赖）                    │
│  config/config.h  → 全模块共用                           │
│  core/messages_schema.h / agent_types.h  → 数据类型     │
│  agents/plan_mode.h, task_manager.h                     │
│  managers/permission_manager.h, plugin_manager.h        │
│  managers/background_task_manager.h, skill_loader.h     │
│  services/cron_scheduler.h, lsp_manager.h, tts_speaker │
│  mcp/mcp_client.h, network/curl_client.h                │
└──────────────────────────────────────────────────────┘
```

## 实例化模式

### 单例模式（`GetInstance()`）
模块内的管理器类几乎全部是单例，通过静态局部变量初始化的方式实现：

| 类 | 文件名 | 是否有可变状态 | 职责 |
|----|--------|---------------|------|
| `AgentEngine` | `agent_engine.h` | 是 | 顶层入口，管理会话/工具/提供商 |
| `AgentSessionManager` | `managers/agent_session_manager.h` | 是 | 多会话创建、切换、广播 |
| `AgentRoleLoader` | `managers/agent_role_loader.h` | **否** | 从 markdown 加载角色定义 |
| `ToolRegistry` | `tools/tool_registry.h` | 是 | 注册并按名执行工具 |
| `ProviderRouter` | `providers/llm_provider_router.h` | 是 | 按角色配置路由 LLM 请求 |
| `CommandRegistry` | `command_registry.h` | 是 | 约 30 个斜杠命令注册与执行 |
| `PermissionManager` | `managers/permission_manager.h` | 是 | 按规则授权工具调用 |
| `PluginManager` | `managers/plugin_manager.h` | 是 | 第三方插件发现与加载 |
| `SkillLoader` | `managers/skill_loader.h` | **否** | 加载/校验 SKILL.md |
| `TokenTracker` | `managers/token_tracker.h` | 是 | 记录各模型 Token 用量与费用 |
| `MemoryConsolidationService` | `core/memory_consolidation_service.h` | 是 | 提取关键决策写入角色记忆 |
| `BackgroundTaskManager` | `managers/background_task_manager.h` | 是 | 后台 Shell 命令生命周期 |
| `BackgroundRunTool` | `tools/command_tools/background_run_tool.h` | **否** | 工具包装，委托给 BackgroundTaskManager |
| `LlamaCppServerManager` | `local_server/llamacpp_server_manager.h` | 是 | llama-server 子进程启停与健康检查 |
| `ActiveTriggerManager` | `managers/active_trigger_manager.h` | 是 | 定时/空闲 LLM 主动触发 |
| `McpClient` | `mcp/mcp_client.h` | 是 | MCP 服务器连接与工具发现 |
| `PlanModeManager` | `agents/plan_mode.h` | 是 | 计划模式进入/退出/步骤管理 |
| `TaskManager` | `agents/task_manager.h` | 是 | 任务 CRUD 与持久化 |
| `SubagentCoordinator` | `agents/subagent_coordinator.h` | 是 | 子 Agent 创建与并行执行 |
| `CronScheduler` | `services/cron_scheduler.h` | 是 | 定时任务调度 |
| `LspManager` | `services/lsp_manager.h` | 是 | LSP 诊断/跳转/补全 |
| `TtsSpeaker` | `providers/tts/tts_speaker.h` | 是 | 文本转语音（异步队列） |
| `HttpClient` | `network/curl_client.h` | 是 | HTTP 连接池与流式请求 |
| `ProsophorConfig` | `config/config.h` | 是 | 顶层配置持有着 |

### 静态类（全静态方法，无实例）
| 类 | 文件名 | 是否有可变状态 | 职责 |
|----|--------|---------------|------|
| `AgentCore` | `core/agent_core.h` | **否** | 消息处理循环，把 `AgentSession&` 传进去执行。**特意设计为无状态** |
| `ReferenceParser` | `core/reference_parser.h` | **否** | 解析 `@file` / `#session` 引用 |
| `EffortConfig` | `config/effort_config.h` | 是 | 推理深度控制，API 全静态，内部藏一个全局实例 |

### 常规类（非单例，按需构造）
| 类 | 文件名 | 是否有可变状态 | 职责 |
|----|--------|---------------|------|
| `AgentSession` | `core/agent_session.h` | 是 | 每个会话的运行时状态。`AgentSessionManager` 创建，`unique_ptr` 持有。Move-only |
| `MemoryManager` | `managers/memory_manager.h` | 是 | 工作区记忆文件管理。**唯一的例外**: 非单例，显式传入 workspace_path 构造 |
| `LLMProvider` | `providers/llm_provider.h` | **否** | 纯虚接口，没有数据成员 |
| `AnthropicProvider` | `providers/anthropic_provider.h` | **否** | Anthropic API 实现 |
| `OpenAIProvider` | `providers/openai_provider.h` | 是 | 兼容 OpenAI 的提供商 |
| `OllamaProvider` | `providers/ollama_provider.h` | **否** | Ollama 本地模型 |
| `TokenCounter` | `managers/token_tracker.h` | **否** | Token 估算工具，全静态方法 |
| `DialogStrategy` | `core/dialog_strategy.h` | **否** | 由 `AgentRole` 以 `shared_ptr` 索引，同策略的 role 共享同一实例。合并原 `CompactService` 功能：`Preprocess` 按 `preprocessor_` → `SummarizingPreprocessor` → `DefaultPreprocessor` 链式 fallback；`CompactWithLLM`/`CompressToTokenLimit` 保留阈值检查、LLM 摘要、Token 估算。静态工具：`KeepRecentMessages`、`NeedsCompaction`、`EstimateTokens` |

### 数据结构体（纯数据，无行为）
`AgentRole` 以 `shared_ptr<DialogStrategy>` 索引对话策略（消息预处理 + 上下文压缩），同策略的 role 共享同一实例。
`MessageSchema`、`ContentSchema`、`SystemSchema`、`ChatRequest`、`ChatResponse`、`RenderSnapshot`、`AgentRuntimeState` 等——无状态，仅作为数据载体。

## 关键约定
- `AgentCore` 是无状态循环，通过引用接收每次的 `AgentSession`（不持有会话自身）。消息预处理委托给 `role->dialog_strategy.Preprocess()`，上下文截断委托给 `role->dialog_strategy.Compact()`
- `AgentCore::Loop` / `SendToSession` / `SendUserMessage` 同步接口均返回 `void`；UI 从 output callback 的 `COMPLETE` reply 取结果，内部调用方从 `GetSnapshot()` 取，失败看 `state == STATE_ERROR`，不要在失败路径读 `messages_` 反推结果
- 前端只 include `public/` 头；AgentEngine 负责 LLM/session，不承载机器人等领域动作
- 前端解耦通过 `std::function` 回调（输出、权限、快照）
- 跨线程快照访问用 `std::shared_mutex` (C++17)
- 管理器类用单例模式（`static T instance`）
- 提供商实现继承 `LLMProvider` 虚基类
- 单例构造为 `static` 局部变量，通过 `Noncopyable` 基类禁止拷贝

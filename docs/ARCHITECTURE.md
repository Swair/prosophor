# AiCode 架构设计文档

## 1. 系统概述

AiCode 是一个基于 LLM 的智能编码助手，采用 C++ 实现，支持多 LLM 提供者、工具调用系统、技能扩展和 MCP 协议。

### 1.1 核心特性

- **多 LLM 提供者支持**: Anthropic、Qwen、Ollama 等
- **工具调用系统**: 文件操作、Shell 命令、Git、LSP、子 Agent 等 40+ 工具
- **技能系统**: 通过 SKILL.md 文件定义的可扩展技能
- **MCP 协议**: 支持外部工具服务器集成
- **计划模式**: 结构化任务规划和执行
- **上下文压缩**: 自动管理长对话历史
- **权限管理**: 细粒度的工具调用授权
- **子 Agent 系统**: 任务分解与委派
- **定时任务**: Cron 调度器支持
- **工作树管理**: Git worktree 集成
- **伴生宠物**: Buddy 系统（ASCII 艺术渲染）

---

## 2. 系统架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                      CLI Layer (cli/)                           │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │ CommandRegistry │  │  InputHandler   │  │      UI         │ │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   AgentCommander (core/)                        │
│           用户交互、命令处理、Agent 执行编排                      │
└─────────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│   AgentCore     │ │ MemoryManager   │ │  SkillLoader    │
│   消息处理/工具  │ │ 工作空间文件/   │ │ 技能加载/解析   │
│   执行/LLM 交互  │ │ 会话管理        │ │                 │
└─────────────────┘ └─────────────────┘ └─────────────────┘
              │               │               │
              ▼               ▼               ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│  ToolRegistry   │ │ LspManager      │ │ McpClient       │
│  工具注册/执行  │ │ LSP 语言服务    │ │ MCP 协议客户端  │
└─────────────────┘ └─────────────────┘ └─────────────────┘
              │
              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    LLM Providers (providers/)                   │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │ AnthropicProvider│ │  QwenProvider   │ │   OllamaProvider│ │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘ │
│  ┌─────────────────┐                                            │
│  │ LLMProvider     │                                            │
│  │ (interface)     │                                            │
│  └─────────────────┘                                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. 核心模块设计

### 3.1 AgentCommander (`core/agent_commander.h`)

**职责**: 系统入口点，编排用户交互、命令处理和 Agent 执行

```cpp
class AgentCommander : public Noncopyable {
 public:
    static AgentCommander& GetInstance();  // 单例模式
    int Run();                              // 主交互循环
    bool HandleCommand(const std::string& line);  // 斜杠命令处理
    void ProcessUserMessage(const std::string& line);
    
 private:
    AiCodeConfig config_;
    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<SkillLoader> skill_loader_;
    std::shared_ptr<ToolRegistry> tool_registry_;
    std::shared_ptr<LLMProvider> llm_provider_;
    std::shared_ptr<AgentCore> agent_core_;
};
```

**关键方法**:
- `Run()`: 启动主交互循环
- `HandleCommand()`: 处理 `/help`, `/plan`, `/mcp` 等命令
- `BuildSystemPrompt()`: 构建包含技能和身份文件的系统提示

---

### 3.2 AgentCore (`core/agent_core.h`)

**职责**: 消息处理、工具执行和 LLM 交互的核心编排器

```cpp
class AgentCore : public Noncopyable {
 public:
    AgentCore(std::shared_ptr<MemoryManager> memory_manager,
              std::shared_ptr<SkillLoader> skill_loader,
              std::vector<ToolsSchema> tool_schemas,
              std::function<std::string(const std::string&, const nlohmann::json&)> tool_executor,
              std::function<ChatResponse(const ChatRequest&)> chat_completion,
              std::function<void(const ChatRequest&, std::function<void(const ChatResponse&)>)> chat_completion_stream,
              const AgentConfig& agent_config);
    
    std::vector<MessageSchema> CloseLoop(const std::string& message);
    std::vector<MessageSchema> LoopStream(const std::string& message, AgentEventCallback callback);
    void Stop();
    
 private:
    ChatRequest chat_request_;
    AgentConfig agent_config_;
    std::atomic<bool> stop_requested_;
    int max_iterations_;
};
```

**关键流程**:
1. 用户消息 → `CloseLoop()` / `LoopStream()`
2. 添加用户消息到历史
3. 调用 LLM 获取响应
4. 解析工具调用 → 执行工具 → 获取结果
5. 将工具结果反馈给 LLM
6. 循环直到无工具调用或达到迭代限制

---

### 3.3 MemoryManager (`core/memory_manager.h`)

**职责**: 工作空间文件管理和每日记忆存储

```cpp
class MemoryManager {
 public:
    explicit MemoryManager(const std::filesystem::path& workspace_path);
    void LoadWorkspaceFiles();           // 加载工作空间文件
    std::string ReadIdentityFile(const std::string& filename);  // SOUL.md, USER.md, MEMORY.md
    std::string ReadAgentsFile();        // AGENTS.md (行为指令)
    std::string ReadToolsFile();         // TOOLS.md (工具使用指南)
    void SaveDailyMemory(const std::string& content);  // 保存每日记忆
    void StartFileWatcher();             // 文件系统监听
    
 private:
    std::filesystem::path workspace_path_;
    std::filesystem::path base_dir_;     // ~/.aicode
    std::atomic<bool> watching_;
};
```

**身份文件**:
- `SOUL.md`: AI 助手的身份/个性定义
- `USER.md`: 用户偏好和上下文
- `MEMORY.md`: 持久化记忆索引
- `AGENTS.md`: 行为指令
- `TOOLS.md`: 工具使用指南

---

### 3.4 SkillLoader (`core/skill_loader.h`)

**职责**: 从 SKILL.md 文件加载和解析技能

```cpp
class SkillLoader {
 public:
    std::vector<SkillMetadata> LoadSkillsFromDirectory(const std::filesystem::path& skills_dir);
    bool CheckSkillGating(const SkillMetadata& skill);  // 环境检查
    std::string GetSkillContext(const std::vector<SkillMetadata>& skills);
    bool InstallSkill(const SkillMetadata& skill);  // 自动安装依赖
    std::vector<SkillCommand> GetAllCommands(const std::vector<SkillMetadata>& skills);
    
 private:
    SkillMetadata ParseSkillFile(const std::filesystem::path& skill_file);
    nlohmann::json ParseYamlFrontmatter(const std::string& yaml_str);
};
```

**SKILL.md 结构**:
```yaml
---
name: git
description: Git version control operations
required_bins: [git]
required_envs: []
any_bins: []
config_files: []
os_restrict: []
always: false
---

# Skill instructions and context
```

---

### 3.5 ToolRegistry (`core/tool_registry.h`)

**职责**: 工具注册管理和执行

```cpp
class ToolRegistry {
 public:
    void RegisterBuiltinTools();
    void RegisterTool(const std::string& name, const std::string& description,
                      nlohmann::json parameters,
                      std::function<std::string(const nlohmann::json&)> executor);
    std::string ExecuteTool(const std::string& tool_name, const nlohmann::json& parameters);
    std::vector<ToolsSchema> GetToolSchemas();
    
 private:
    // 内置工具实现
    std::string ReadFileTool(const nlohmann::json& params);
    std::string WriteFileTool(const nlohmann::json& params);
    std::string EditFileTool(const nlohmann::json& params);
    std::string ExecTool(const nlohmann::json& params);
    std::string BashTool(const nlohmann::json& params);
    std::string GlobTool(const nlohmann::json& params);
    std::string GrepTool(const nlohmann::json& params);
    std::string GitStatusTool(const nlohmann::json& params);
    // ... 更多工具
};
```

**内置工具分类**:

| 类别 | 工具 |
|------|------|
| 文件操作 | `read_file`, `write_file`, `edit_file` |
| Shell | `exec`, `bash` |
| 搜索 | `glob`, `grep` |
| Git | `git_status`, `git_diff`, `git_log`, `git_commit`, `git_add`, `git_branch` |
| 交互 | `ask_user_question`, `todo_write` |
| LSP | `lsp_diagnostics`, `lsp_go_to_definition`, `lsp_find_references`, `lsp_get_hover` |
| Web | `web_search`, `web_fetch` |
| Token | `token_count`, `token_usage` |
| MCP | `mcp_list_tools`, `mcp_call_tool`, `mcp_list_resources` |

---

### 3.6 LLMProvider (`providers/llm_provider.h`)

**职责**: LLM 提供者抽象接口

```cpp
class LLMProvider {
 public:
    virtual ChatResponse Chat(const ChatRequest& request) = 0;
    virtual void ChatStream(const ChatRequest& request,
                           std::function<void(const ChatResponse&)> callback) = 0;
    virtual std::string GetProviderName() const = 0;
    virtual std::vector<std::string> GetSupportedModels() const = 0;
    virtual std::string Serialize(const ChatRequest& request) const = 0;
    virtual ChatResponse Deserialize(const std::string& json_str) const = 0;
};
```

**提供者实现**:
- `AnthropicProvider`: Anthropic API (Claude)
- `QwenProvider`: 通义千问 API

---

### 3.7 CompactService (`core/compact_service.h`)

**职责**: 上下文压缩，管理长对话历史

```cpp
class CompactService {
 public:
    static CompactService& GetInstance();
    bool NeedsCompaction(const std::vector<MessageSchema>& messages);
    CompactResult Compact(const std::vector<MessageSchema>& messages,
                         std::function<std::string(const std::string&)> llm_callback);
    CompactResult CompressToTokenLimit(const std::vector<MessageSchema>& messages,
                                       int max_tokens,
                                       std::function<std::string(const std::string&)> llm_callback);
    
 private:
    CompactConfig config_;
    std::string GenerateSummary(const std::vector<MessageSchema>& old_messages, ...);
    std::vector<MessageSchema> KeepRecentMessages(const std::vector<MessageSchema>& messages, int keep_count);
};
```

**压缩策略**:
- `Summary`: 生成旧消息的 AI 摘要
- `Truncate`: 仅保留最近 N 条消息
- `Hybrid`: 摘要 + 保留最近消息（默认）

---

### 3.8 PermissionManager (`core/permission_manager.h`)

**职责**: 工具调用授权和权限控制

```cpp
class PermissionManager {
 public:
    static PermissionManager& GetInstance();
    void Initialize(const nlohmann::json& config);
    void SetMode(const std::string& mode);  // "auto", "default", "bypass"
    PermissionResult CheckPermission(const std::string& tool_name, const nlohmann::json& input);
    void AddAllowRule(const PermissionRule& rule);
    void AddDenyRule(const PermissionRule& rule);
    void AddAskRule(const PermissionRule& rule);
    
 private:
    std::vector<PermissionRule> allow_rules_;
    std::vector<PermissionRule> deny_rules_;
    std::vector<PermissionRule> ask_rules_;
    ConfirmCallback confirm_callback_;
};
```

**权限级别**:
- `Allow`: 自动批准
- `Deny`: 自动拒绝
- `Ask`: 需要用户确认

**规则匹配**:
```cpp
struct PermissionRule {
    std::string tool_name;       // 工具名模式 (e.g., "bash", "read_*")
    std::string command_pattern; // Bash 命令模式 (e.g., "git *", "npm test")
    std::string path_pattern;    // 文件路径模式
    PermissionLevel default_level;
    std::vector<std::string> allowed_args;
    std::vector<std::string> denied_args;
};
```

---

### 3.9 PlanModeManager (`core/plan_mode.h`)

**职责**: 结构化任务规划和执行跟踪

```cpp
class PlanModeManager {
 public:
    static PlanModeManager& GetInstance();
    void EnterPlanMode();
    void ExitPlanMode();
    void CreatePlan(const std::string& title, const std::string& description);
    void AddStep(const std::string& description, const std::string& tool_name = "", ...);
    void ApprovePlan();
    void CompleteStep(int step_id, const std::string& result = "");
    std::string GetPlanAsMarkdown() const;
    
 private:
    bool in_plan_mode_ = false;
    Plan current_plan_;
};
```

**计划步骤状态**:
```cpp
enum class PlanStepStatus {
    Pending,     // 待处理
    InProgress,  // 进行中
    Completed,   // 已完成
    Skipped,     // 已跳过
    Failed       // 失败
};
```

---

### 3.10 LspManager (`services/lsp_manager.h`)

**职责**: 语言服务器协议管理

```cpp
class LspManager {
 public:
    static LspManager& GetInstance();
    void Initialize();
    void RegisterServer(const LspServerConfig& config);
    bool StartServerForFile(const std::string& filepath);
    std::vector<Diagnostic> GetDiagnostics(const std::string& uri);
    std::vector<Symbol> GoToDefinition(const std::string& uri, int line, int character);
    std::vector<Symbol> FindReferences(const std::string& uri, int line, int character);
    std::string GetHover(const std::string& uri, int line, int character);
    std::vector<Symbol> GetDocumentSymbols(const std::string& uri);
    
 private:
    std::unordered_map<std::string, ServerInstance> servers_;
    nlohmann::json SendRequest(ServerInstance& server, const std::string& method, ...);
};
```

**LSP 功能**:
- 诊断（错误/警告）
- 跳转到定义
- 查找引用
- 悬停信息
- 文档符号
- 工作区符号
- 文档格式化

---

### 3.11 McpClient (`mcp/mcp_client.h`)

**职责**: MCP (Model Context Protocol) 协议客户端

```cpp
class McpClient {
 public:
    static McpClient& GetInstance();
    void Initialize(const std::vector<McpServerConfig>& servers);
    bool ConnectToServer(const McpServerConfig& config);
    std::vector<McpTool> GetAvailableTools();
    std::string CallTool(const std::string& tool_name, const nlohmann::json& arguments);
    std::string ReadResource(const std::string& uri);
    bool AddServer(const McpServerConfig& config);
    bool RemoveServer(const std::string& server_name);
    
 private:
    struct ServerConnection {
        std::string name;
        std::string type;  // "stdio" | "sse" | "websocket"
        void* process_handle;
        int stdin_fd, stdout_fd;
        std::vector<McpTool> tools;
    };
    std::unordered_map<std::string, std::unique_ptr<ServerConnection>> servers_;
};
```

**MCP 服务器配置**:
```cpp
struct McpServerConfig {
    std::string name;
    std::string type;    // "stdio" | "sse" | "websocket"
    std::string command; // stdio: 执行的命令
    std::vector<std::string> args;
    std::string url;     // SSE/WebSocket URL
    nlohmann::json env;  // 环境变量
};
```

---

## 4. 数据结构设计

### 4.1 消息模式 (`core/messages_schema.h`)

```cpp
struct ContentSchema {
    std::string type;        // "text" | "tool_use" | "tool_result" | "thinking"
    std::string text;        // 文本/思考内容
    std::string tool_use_id; // 工具调用 ID
    std::string name;        // 工具名
    nlohmann::json input;    // 工具参数
    std::string content;     // 工具结果
    bool is_error = false;
};

struct MessageSchema {
    std::string role;  // "user" | "assistant"
    std::vector<ContentSchema> content;
};
```

### 4.2 LLM 请求/响应 (`providers/llm_provider.h`)

```cpp
struct ChatRequest {
    std::vector<SystemSchema> system;
    std::vector<ToolsSchema> tools;
    std::vector<MessageSchema> messages;
    std::string model;
    int max_tokens = 8192;
    double temperature = 0.7;
    bool tool_choice_auto = true;
    bool stream = false;
    std::string thinking = "off";  // "off" | "low" | "medium" | "high"
};

struct ChatResponse {
    std::string content_text;
    std::vector<ToolUseSchema> tool_calls;
    bool is_stream_end = false;
    std::string stop_reason;
    TokenUsageSchema usage;
};
```

### 4.3 配置结构 (`common/config.h`)

```cpp
struct AiCodeConfig {
    std::string log_level = "info";
    std::string default_provider = "anthropic";
    SecurityConfig security;
    std::unordered_map<std::string, ProviderConfig> providers;
    ToolConfig tools;
    SkillsConfig skills;
};

struct AgentConfig {
    std::string name = "default";
    std::string model = "claude-sonnet-4-6";
    double temperature = 0.7;
    int max_tokens = 8192;
    int context_window = 128000;
    bool auto_compact = true;
    int compact_max_messages = 100;
};
```

---

## 5. 关键设计模式

### 5.1 单例模式

以下组件使用单例模式：
- `AgentCommander`
- `CompactService`
- `PermissionManager`
- `PlanModeManager`
- `LspManager`
- `McpClient`
- `SessionManager`
- `TokenTracker`
- `CronScheduler`

### 5.2 策略模式

- `LLMProvider` 接口：`AnthropicProvider`, `QwenProvider`, `OllamaProvider`
- `CompactStrategy`: `Summary`, `Truncate`, `Hybrid`

### 5.3 工厂模式

- `ToolRegistry` 注册和创建工具执行器
- `SkillLoader` 解析和实例化技能

### 5.4 观察者模式

- `AgentEventCallback`: 流式响应事件
- `FileChangeCallback`: 文件变化监听

---

## 6. 目录结构

```
main_src/
├── cli/                  # CLI 交互层
│   ├── command_registry.h/cc
│   └── input_handler.h/cc
├── common/               # 通用工具
│   ├── config.h/cc       # 配置结构
│   ├── constants.h
│   ├── curl_client.h/cc  # HTTP 客户端
│   ├── log_wrapper.h
│   ├── noncopyable.h
│   ├── file_utils.h/cc   # 文件工具
│   ├── time_wrapper.h/cc # 时间工具
│   └── string_utils.h/cc # 字符串工具
├── core/                 # 核心业务逻辑
│   ├── agent_commander.h/cc  # 主入口/编排器
│   ├── agent_core.h/cc       # Agent 核心
│   ├── compact_service.h/cc  # 上下文压缩
│   ├── memory_manager.h/cc   # 文件/记忆管理
│   ├── messages_schema.h     # 消息数据结构
│   ├── permission_manager.h/cc
│   ├── plan_mode.h/cc        # 计划模式
│   ├── skill_loader.h/cc     # 技能加载
│   ├── system_prompt.h/cc    # 系统提示构建
│   ├── tool_registry.h/cc    # 工具注册
│   ├── session_manager.h/cc  # 会话管理
│   └── reference_parser.h/cc # 文件引用解析
├── providers/            # LLM 提供者
│   ├── llm_provider.h/cc # 抽象接口
│   ├── anthropic_provider.h/cc
│   ├── qwen_provider.h/cc
│   └── ollama_provider.h/cc
├── tools/                # 工具实现
│   ├── tool_registry.h/cc
│   ├── agent_tool.h/cc
│   ├── ask_user_question_tool.h/cc
│   ├── todo_write_tool.h/cc
│   ├── glob_tool.h/cc
│   ├── grep_tool.h/cc
│   ├── lsp_tool.h/cc
│   ├── task_tool.h/cc
│   ├── worktree_tool.h/cc
│   ├── cron_tool.h/cc
│   └── background_run_tool.h/cc
├── mcp/                  # MCP 协议
│   └── mcp_client.h/cc
├── managers/             # 管理器
│   ├── buddy_manager.h/cc
│   ├── token_tracker.h/cc
│   ├── worktree_manager.h/cc
│   └── background_task_manager.h/cc
├── agents/               # Agent 相关
│   ├── plan_mode.h/cc
│   ├── task_manager.h/cc
│   └── subagent_coordinator.h/cc
└── services/             # 外部服务
    ├── lsp_manager.h/cc  # LSP 管理
    └── cron_scheduler.h/cc
```

---

## 7. 数据流

### 7.1 用户请求处理流程

```
用户输入
    │
    ▼
AgentCommander.Run() ──▶ HandleCommand() [斜杠命令]
    │
    ▼ (普通消息)
AgentCore.CloseLoop(message)
    │
    ├──▶ 添加用户消息到 chat_request_.messages
    │
    ├──▶ llm_provider_.Chat(chat_request_)
    │        │
    │        ▼
    │     AnthropicProvider / QwenProvider
    │        │
    │        ▼
    │     HTTP POST to LLM API
    │
    ▼
解析 ChatResponse
    │
    ├──▶ content_text ──▶ 流式输出到用户
    │
    └──▶ tool_calls ──▶ ToolRegistry.ExecuteTool()
             │
             ▼
         工具执行结果
             │
             ▼
         添加 tool_result 到 messages
             │
             └──────┐
                    │
                    ▼
              继续循环 (直到无 tool_calls)
```

### 7.2 工具调用流程

```
LLM 返回 tool_calls
    │
    ▼
AgentCore 解析 ToolUseSchema
    │
    ▼
ToolRegistry.ExecuteTool(tool_name, params)
    │
    ├──▶ PermissionManager.CheckPermission()
    │        │
    │        ├──▶ 匹配规则 (allow/deny/ask)
    │        │
    │        └──▶ 需要确认? ──▶ RequestUserConfirmation()
    │
    ▼ (已授权)
执行工具实现
    │
    ├──▶ ReadFileTool ──▶ 读取文件内容
    ├──▶ BashTool ──▶ 执行 shell 命令
    ├──▶ GitStatusTool ──▶ git status
    └──▶ ...
    │
    ▼
返回结果字符串
    │
    ▼
AgentCore 添加 tool_result 到 messages
    │
    ▼
继续 LLM 循环
```

---

## 8. 配置系统

### 8.1 配置文件位置

- 默认：`~/.aicode/config.json`
- 可通过环境变量覆盖

### 8.2 配置结构

```json
{
  "log_level": "info",
  "default_provider": "anthropic",
  "security": {
    "permission_level": "auto",
    "allow_local_execute": true
  },
  "providers": {
    "anthropic": {
      "api_key": "...",
      "base_url": "https://api.anthropic.com",
      "api_type": "anthropic-messages",
      "timeout": 30,
      "agents": {
        "default": {
          "model": "claude-sonnet-4-6",
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
    "timeout": 60
  },
  "skills": {
    "path": "~/.aicode/skills",
    "auto_approve": [],
    "load": {
      "extra_dirs": []
    }
  }
}
```

---

## 9. 扩展机制

### 9.1 添加新的 LLM 提供者

1. 继承 `LLMProvider` 接口
2. 实现 `Chat()`, `ChatStream()`, `Serialize()`, `Deserialize()`
3. 在配置中注册提供者

### 9.2 添加新工具

1. 在 `ToolRegistry` 中添加工具实现方法
2. 在 `RegisterBuiltinTools()` 中注册
3. 定义工具 schema（名称、描述、参数）

### 9.3 添加新技能

创建 `SKILL.md` 文件：
```markdown
---
name: my-skill
description: My custom skill
required_bins: [my-tool]
required_envs: [MY_API_KEY]
---

# Skill Instructions

Detailed instructions for the AI...
```

---

## 10. 安全设计

### 10.1 权限控制

- 默认模式：需要用户确认危险操作
- 规则匹配：工具名、命令模式、路径模式
- 失败回退：3 次拒绝后自动批准（避免重复确认）

### 10.2 沙箱机制

- 文件操作限制在允许的路径内
- Shell 命令可通过白名单/黑名单控制
- 超时保护：工具执行超时自动终止

---

## 11. 性能优化

### 11.1 上下文压缩

- 自动检测消息数量/Token 数超限
- 使用 LLM 生成历史摘要
- 保留最近的 N 条消息

### 11.2 Token 跟踪

```cpp
void RecordTokenUsage(const std::string& model, const TokenUsageSchema& usage);
```

- 记录 prompt/completion  token
- 计算成本估算

### 11.3 缓存策略

- 系统提示缓存（`cache_control` 标记）
- LSP 诊断缓存
- 文件内容缓存

---

## 12. 会话管理

```cpp
class SessionManager {
 public:
    std::string StartSession(const std::string& workspace = "");
    void SaveCurrentSession();
    bool LoadSession(const std::string& session_id, ...);
    std::vector<SessionInfo> ListSessions() const;
    bool ResumeLastSession(...);
};
```

- 会话文件：`~/.aicode/sessions/{session_id}.json`
- 自动保存：每条消息添加后
- 恢复：从文件加载消息历史

---

## 13. 总结

AiCode 是一个模块化的 AI 编码助手架构：

1. **分层设计**: CLI 层 → 编排层 → 核心层 → 提供者层
2. **接口抽象**: LLM 提供者、工具执行器、技能加载器
3. **可扩展性**: 技能系统、MCP 协议、工具注册
4. **安全性**: 权限管理、命令限制、超时保护
5. **用户体验**: 计划模式、会话管理、上下文压缩
6. **高级特性**: 子 Agent 系统、定时任务、工作树管理、伴生宠物

核心价值主张：
- 开源、可定制的 Claude Code 替代方案
- 支持多种 LLM 提供者 (Anthropic/Qwen/Ollama)
- 灵活的技能和工具扩展机制
- 40+ 内置工具，覆盖文件操作、Git、LSP、Web 等
- 完整的 MCP 协议支持，可集成外部工具服务器

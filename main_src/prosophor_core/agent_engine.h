// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/noncopyable.h"
#include "config/config.h"
#include "core/messages_schema.h"
#include "core/agent_types.h"

namespace prosophor {

// Forward declarations
class MemoryManager;
class ToolRegistry;
class AgentSessionManager;
class LlmProviderRouter;
class CommandRegistry;

/// 前端关心的角色运行时配置（当前只暴露 TTS；其余仍属 core 内部）
struct RoleTtsConfig {
    std::string backend;
    std::string voice;
};

/// provider 运行时状态（通用只读探测，不暴露 provider 实例）
struct ProviderStatus {
    bool loaded = false;
    bool loading = false;
    bool has_vision = false;
    std::string error;
};

/// 主动触发插件的只读状态（UI 展示用，不暴露 ActiveTriggerManager）
struct ActiveTriggerInfo {
    std::string name;
    std::string role;
    std::string mode;
    int interval = 0;
    int threshold = 0;
    int cooldown = 300;
    int priority = 50;
    bool enabled = true;
};

/// AgentEngine: core business logic shared by all frontends (Terminal, SDL, etc.)
/// Manages sessions, tools, providers, and commands.
/// Frontends register callbacks to receive output and handle permissions
/// and hold session_ids to manage multi-character conversations.
class AgentEngine : public Noncopyable {
 public:
    static AgentEngine& GetInstance();

    /// 专用形态（NutriBot）在注册自己的工具之前调用 false，清空 bash/file/web
    /// 等通用内建工具。
    void SetUseDefaultTools(bool enabled);

    /// 外部工具注册完成后、创建会话前调用：重算角色的 role.tools 快照。
    void ReloadRoles();

    using OutputCallback = std::function<void(
        const std::string& session_id,
        const std::string& role_id,
        AgentRuntimeState state,
        const std::string& state_msg,
        const std::optional<MessageSchema>& reply,
        const std::string& delta)>;

    using PermissionCallback = std::function<bool(
        const std::string& tool_name,
        const nlohmann::json& input,
        const std::string& reason)>;

    /// 主动触发产出的投递回调：由前端决定打印/气泡/TTS/推送。
    using ProactiveMessageCallback = std::function<void(
        const std::string& source,
        const std::string& role_id,
        const std::string& session_id,
        const std::string& text)>;

    /// Change workspace path at runtime
    void ChangeWorkspace(const std::string& new_path);

    void SetOutputCallback(OutputCallback cb);
    void SetPermissionCallback(PermissionCallback cb);
    /// Add an output callback without clearing existing ones (multi-frontend coexistence)
    void AddOutputCallback(OutputCallback cb);
    /// 注册主动消息投递回调；未注册时回复只写日志
    void SetProactiveMessageCallback(ProactiveMessageCallback cb);

    // ── Session API (session_id owned by caller) ────────────────────────────
    // 三个身份维度不要混：
    //   session_id —— 哪一次对话（运行时句柄，进程内生成）
    //   owner_id   —— 会话归谁（私聊=用户，群聊=群）
    //   sender_id  —— 本条消息是谁说的（群聊=发言的群成员用户，不是群）
    /// Create a new session for the given role; returns its session_id.
    /// @param owner_id IM 归属主体(用户/群 ID),空表示本机单用户模式
    std::string CreateSession(const std::string& role_id, const std::string& task_desc = "",
                              const std::string& owner_id = "",
                              SessionType session_type = SessionType::kDirect);

    /// 同步发送用户消息：阻塞至整轮完成（含工具调用与流式回调）。
    /// 回复通过 output callback 获取；需要异步并发的调用方用 SendUserMessageAsync。
    /// @param sender_id 本条消息的发言者：群聊中即发言的群成员用户 ID（不是群 ID）；
    ///                  私聊可空；空 = 未提供 IM 身份（本地单用户前端），记忆归属按 owner 兜底
    /// @param sender_name 发言者展示名，可空
    void SendUserMessage(const std::string& session_id, const std::string& text,
                         const std::string& sender_id = "",
                         const std::string& sender_name = "");

    /// 同步发送用户消息，并随附图片（用户随时给图：图只属于本条消息）。
    /// 直接照抄上面那个重载即可 —— 纯文本路径行为逐字节不变。
    /// @param image_paths 本条消息随附的图片路径；空则等同不带图的重载
    /// @param sender_id 同上一重载：本条消息的发言者（群聊=发言的群成员用户 ID）
    /// @param sender_name 发言者展示名，可空
    void SendUserMessage(const std::string& session_id, const std::string& text,
                         const std::vector<std::string>& image_paths,
                         const std::string& sender_id = "",
                         const std::string& sender_name = "");

    /// 每轮结束后清掉历史图片块（NutriBot 打开；图片只服务当轮）。
    void SetClearImagesAfterTurn(bool enabled);

    /// Send a user message to a specific session (handles /slash commands internally).
    /// @param sender_id 本条消息的发言者：群聊中即发言的群成员用户 ID；
    ///                  空 = 未提供 IM 身份（本地单用户前端）
    /// @param sender_name 发言者展示名，可空
    void SendUserMessageAsync(const std::string& session_id, const std::string& text,
                         const std::string& sender_id = "",
                         const std::string& sender_name = "");

    /// Stop a specific session.
    void StopSession(const std::string& session_id);

    /// 停止主动触发调度线程（NutriBot 退出前调用，避免回调越过对象生命周期）。
    void StopActiveTriggers();

    /// Flush unsaved messages to disk (keeps session alive; used by web after each turn).
    void FlushSession(const std::string& session_id);

    /// Switch an existing session to a different role (preserves history).
    void SwitchRole(const std::string& session_id, const std::string& new_role_id);

    /// Get a snapshot of the focused (last active) session for UI rendering.
    std::optional<RenderSnapshot> GetFocusedSessionSnapshot();

    /// Get a snapshot of a specific session (for per-sprite rendering).
    std::optional<RenderSnapshot> GetSessionSnapshot(const std::string& session_id);


    // ── Commands ────────────────────────────────────────────────────────────
    /// Execute a slash command in the context of the given session.
    bool HandleCommand(const std::string& line, const std::string& session_id);

    // ── Info ────────────────────────────────────────────────────────────────
    std::vector<std::string> ListRoles() const;
    std::vector<std::string> ListSessions() const;

    /// 角色 TTS 配置副本（不暴露 AgentRole 内部）
    std::optional<RoleTtsConfig> GetRoleTtsConfig(const std::string& role_id) const;

    /// 角色绑定的 provider 类型（"aidgen" / "anthropic" / "openai" / ...）
    std::string GetRoleProviderName(const std::string& role_id) const;

    /// 注册前端/形态专用工具（nutribot 的感知上报、成员档案等）。
    /// 必须在创建使用该工具的角色会话之前调用。
    void RegisterTool(const std::string& name, const std::string& description,
                      nlohmann::json parameters,
                      std::function<std::string(const nlohmann::json&)> executor);

    /// 会话工具开关（SDL 右键菜单用）
    bool GetSessionUseTools(const std::string& session_id) const;
    void SetSessionUseTools(const std::string& session_id, bool enabled);

    /// 无状态会话：每轮结束自动清空历史/图片，且不落盘、不做记忆整理。
    /// 适用于周期感知这类"每次只看当前帧"的 session。
    bool GetSessionStateless(const std::string& session_id) const;
    void SetSessionStateless(const std::string& session_id, bool enabled);

    /// provider 就绪状态查询（含视觉能力；不暴露 provider 实例）
    ProviderStatus GetProviderStatus(const std::string& provider_name) const;

    /// 主动触发插件状态快照（SDL 插件面板只读展示）
    std::vector<ActiveTriggerInfo> ListActiveTriggerPlugins() const;

    const ProsophorConfig& GetConfig() const { return config_; }
    MemoryManager& GetMemoryManager() const { return *memory_manager_; }

 private:
    AgentEngine();
    ~AgentEngine();

    void InitializeComponents();

    /// 主动触发的一次同步 turn：解析角色、复用/创建 active 会话、调用 Loop。
    void RunActiveTurn(const std::string& plugin, const std::string& role_id,
                       const std::string& reason, const std::string& prompt_md);

    ProsophorConfig config_;
    ModelConfig model_config_;
    std::string workspace_path_;

    std::shared_ptr<MemoryManager> memory_manager_;
    ToolRegistry*        tool_registry_   = nullptr;
    AgentSessionManager* session_manager_ = nullptr;
    LlmProviderRouter*  provider_router_ = nullptr;
    CommandRegistry*     command_registry_ = nullptr;

    ProactiveMessageCallback proactive_callback_;
    std::mutex active_sessions_mutex_;                              // 保护回调与映射（调度线程/析构）
    std::unordered_map<std::string, std::string> active_session_ids_;  // "plugin|role" → session_id

};

}  // namespace prosophor

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

/// AgentEngine: core business logic shared by all frontends (Terminal, SDL, etc.)
/// Manages sessions, tools, providers, and commands.
/// Frontends register callbacks to receive output and handle permissions
/// and hold session_ids to manage multi-character conversations.
class AgentEngine : public Noncopyable {
 public:
    static AgentEngine& GetInstance();

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

    /// Change workspace path at runtime
    void ChangeWorkspace(const std::string& new_path);

    void SetOutputCallback(OutputCallback cb);
    void SetPermissionCallback(PermissionCallback cb);
    /// Add an output callback without clearing existing ones (multi-frontend coexistence)
    void AddOutputCallback(OutputCallback cb);

    // ── Session API (session_id owned by caller) ────────────────────────────
    /// Create a new session for the given role; returns its session_id.
    /// @param owner_id IM 归属主体(用户/群 ID),空表示本机单用户模式
    std::string CreateSession(const std::string& role_id, const std::string& task_desc = "",
                              const std::string& owner_id = "",
                              SessionType session_type = SessionType::kDirect);

    /// 同步发送用户消息：阻塞至整轮完成（含工具调用与流式回调），
    /// 返回本轮助手回复全文。需要异步并发的调用方用 SendUserMessageAsync。
    /// @param sender_id IM 发送者 ID(群聊必填),空表示本机用户
    /// @return 助手回复文本；斜杠命令或出错时为空
    std::string SendUserMessage(const std::string& session_id, const std::string& text,
                                const std::string& sender_id = "",
                                const std::string& sender_name = "");

    /// 同步发送用户消息，并随附图片（用户随时给图：图只属于本条消息）。
    /// 直接照抄上面那个重载即可 —— 纯文本路径行为逐字节不变。
    /// @param image_paths 本条消息随附的图片路径；空则等同不带图的重载
    /// @return 助手回复文本；斜杠命令或出错时为空
    std::string SendUserMessage(const std::string& session_id, const std::string& text,
                                const std::vector<std::string>& image_paths,
                                const std::string& sender_id = "",
                                const std::string& sender_name = "");

    /// Send a user message to a specific session (handles /slash commands internally).
    /// @param sender_id IM 发送者 ID(群聊必填),空表示本机用户
    void SendUserMessageAsync(const std::string& session_id, const std::string& text,
                         const std::string& sender_id = "",
                         const std::string& sender_name = "");

    /// Stop a specific session.
    void StopSession(const std::string& session_id);

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

    const ProsophorConfig& GetConfig() const { return config_; }
    MemoryManager& GetMemoryManager() const { return *memory_manager_; }

 private:
    AgentEngine();
    ~AgentEngine();

    void InitializeComponents();

    ProsophorConfig config_;
    ModelConfig model_config_;
    std::string workspace_path_;

    std::shared_ptr<MemoryManager> memory_manager_;
    ToolRegistry*        tool_registry_   = nullptr;
    AgentSessionManager* session_manager_ = nullptr;
    LlmProviderRouter*  provider_router_ = nullptr;
    CommandRegistry*     command_registry_ = nullptr;

};

}  // namespace prosophor

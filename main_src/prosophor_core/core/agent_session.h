// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <optional>
#include <functional>
#include <atomic>
#include <mutex>

#include <nlohmann/json.hpp>

#include "core/agent_role.h"
#include "core/messages_schema.h"
#include "providers/llm/llm_provider.h"
#include "providers/llm_provider_router.h"
#include "common/list_buffer.h"
#include "common/time_wrapper.h"
#include "core/agent_types.h"

// Forward declaration for MemoryConsolidationService
namespace prosophor { class MemoryConsolidationService; }

namespace prosophor {

/// Tool executor callback
using ToolExecutorCallback = std::function<std::string(const std::string& tool_name, const nlohmann::json& args)>;

/// Session output callback - notifies UI of state changes and message output
/// Parameters: session_id, role_id, state, state_msg, reply, delta
/// 流式帧:reply 为空,delta 携带本帧增量(类型由 state 决定:
/// STREAM_CONTENT_TYPING=正文增量,STREAM_THINKING=思考增量);
/// 终结:reply 为终态消息,delta 为空。
using SessionOutputCallback = std::function<void(const std::string& session_id,
                                                  const std::string& role_id,
                                                  AgentRuntimeState state,
                                                  const std::string& state_msg,
                                                  const std::optional<MessageSchema>& reply,
                                                  const std::string& delta)>;

/// TTS speak callback - fires when the session has accumulated enough text to speak.
/// Parameters: text, backend, voice
using TtsSpeakCallback = std::function<void(const std::string& text,
                                             const std::string& backend,
                                             const std::string& voice)>;

/// Agent 会话实例（运行时）
class AgentSession {
public:
    // ── 输入/配置─────────────────────────────────────────────
    inline AgentRole* GetRole() const { return role_; }
    inline void SetRole(AgentRole* r) { role_ = r; }
    inline bool GetUseTools() const { return use_tools_; }
    inline void SetUseTools(bool v) { use_tools_ = v; }
    inline bool GetAutoConfirmTools() const { return auto_confirm_tools_; }
    inline void SetAutoConfirmTools(bool v) { auto_confirm_tools_ = v; }
    inline const std::string& GetWorkingDirectory() const { return working_directory_; }
    inline void SetWorkingDirectory(const std::string& v) { working_directory_ = v; }

    // ── 回调（直接读写）─────────────────────────────────────
    ToolExecutorCallback tool_executor_;
    SessionOutputCallback output_callback_;

    /// Set the callback invoked when the session has text ready for TTS synthesis.
    inline void SetTtsSpeakCallback(TtsSpeakCallback cb) {
        tts_speak_callback_ = std::move(cb);
    }

    inline const std::vector<std::string>& GetRelatedFiles() const { return related_files_; }
    inline void SetRelatedFiles(std::vector<std::string> v) { related_files_ = std::move(v); }

    // ── 控制标志 ────────────────────────────────────────────
    inline bool IsStopRequested() const { return stop_requested_.load(); }
    inline void RequestStop() { stop_requested_.store(true); }
    inline void ClearStopRequested() { stop_requested_.store(false); }

    // ── 元数据 ──────────────────────────────────────────────
    inline const std::string& GetSessionId() const { return session_id_; }
    inline const std::string& GetTaskDescription() const { return task_description_; }
    inline std::shared_ptr<LLMProvider> GetProvider() const { return provider_; }
    inline void SetProvider(std::shared_ptr<LLMProvider> v) { provider_ = std::move(v); }
    inline const std::string& GetBaseUrl() const { return base_url_; }
    inline void SetBaseUrl(const std::string& v) { base_url_ = v; }
    inline const std::string& GetApiKey() const { return api_key_; }
    inline void SetApiKey(const std::string& v) { api_key_ = v; }
    inline int GetTimeout() const { return timeout_; }
    inline void SetTimeout(int v) { timeout_ = v; }
    inline const std::string& GetSessionLogDir() const { return session_log_dir_; }
    inline void SetSessionLogDir(const std::string& v) { session_log_dir_ = v; }

    // ── IM 多用户归属 ─────────────────────────────────────
    inline const std::string& GetOwnerId() const { return owner_id_; }
    inline void SetOwnerId(const std::string& v) { owner_id_ = v; }
    inline const std::string& GetGroupId() const { return group_id_; }
    inline void SetGroupId(const std::string& v) { group_id_ = v; }
    inline SessionType GetSessionType() const { return session_type_; }
    inline void SetSessionType(SessionType t) { session_type_ = t; }
    inline bool IsGroupSession() const { return session_type_ == SessionType::kGroup; }

    /// 把未持久化的消息追加到 sessions/{role_id}[/{owner}]/{date}.jsonl
    /// 在 CompactIfNeeded 和 CloseSession 时调用
    void FlushToDisk();
    inline SteadyClock::TimePoint GetCreatedAt() const { return created_at_; }
    inline bool IsActive() const { return is_active_; }
    inline void SetActive(bool v) { is_active_ = v; }

    // ── 会话锁 ──────────────────────────────────────────────
    inline std::unique_lock<std::mutex> ScopedLock() { return std::unique_lock<std::mutex>(session_mutex_); }

    // ── 活跃时间 ────────────────────────────────────────────
    inline void UpdateLastActive() { last_active_ = SteadyClock::Now(); }
    inline SteadyClock::TimePoint GetLastActive() const { return last_active_; }

    // ── 只读访问器（外部代码通过此处读取内部数据）───────────
    inline const std::vector<MessageSchema>& GetMessages() const { return messages_; }
    inline const std::vector<SystemSchema>& GetSystemPrompt() const { return system_prompt_; }
    inline int GetConsolidatedCount() const { return last_consolidated_count_; }
    inline void SetConsolidatedCount(int n) { last_consolidated_count_ = n; }

    // ── 线程安全接口（内部持渲染锁）────────────────────────
    void SetOutput(AgentRuntimeState new_state,
                   const std::string& state_msg,
                   const std::optional<MessageSchema>& reply = std::nullopt,
                   const std::string& delta = "");
    RenderSnapshot GetSnapshot() const;
    /// 设置本条用户消息的发送者(消费式瞬时状态)
    /// 由 StartChain 在持会话锁、Loop 之前调用;AddUserMessage 读取后自动清空
    void SetCurrentSender(const std::string& sender_id,
                          const std::string& sender_name = "");
    /// 设置本条用户消息随附的图片(消费式瞬时状态, 语义同 SetCurrentSender)
    /// 由调用方在 Loop 之前调用;AddUserMessage 读取后自动清空 —— 用户随时给图,
    /// 图只属于它跟的那一条消息, 不会泄漏到后续轮次
    void SetNextImages(std::vector<std::string> image_paths);
    void AddUserMessage(const std::string& text);
    /// 准备开始新 Loop：如果上一个 Loop 被打断留下孤立的 user 消息，清理掉
    /// 在持 session_mutex 后、ClearStopRequested + Loop 之前调用
    /// 打断时清除孤立的 user 消息（最后一条是 user 且无 assistant 回复时移除）
    void CleanupInterruptedLoop();
    void CompactHistory(const std::vector<MessageSchema>& kept_messages,
                        const std::string& summary);
    void SetSystemPrompt(const std::vector<SystemSchema>& prompt);

    // ── 构造函数 ────────────────────────────────────────────
    inline AgentSession() {
        created_at_ = SteadyClock::Now();
        last_active_ = SteadyClock::Now();
    }
    AgentSession(const std::string& sid,
                 const std::string& task, AgentRole* r);
    AgentSession(AgentSession&& other) noexcept;
    AgentSession& operator=(AgentSession&& other) noexcept;
    AgentSession(const AgentSession&) = delete;
    AgentSession& operator=(const AgentSession&) = delete;

    /// 应用 provider 覆盖（内部持 session_mutex）
    void ApplyProviderOverride(const std::string& provider_name,
                               const std::string& model = "");

    /// Check if provider override has been applied
    inline bool HasProviderOverride() const { return mutable_role_.has_value(); }

private:
    // ── 输入/配置 ──────────────────────────────────────────
    AgentRole* role_ = nullptr;
    bool use_tools_ = true;
    bool auto_confirm_tools_ = false;
    std::string working_directory_;
    std::vector<std::string> related_files_;

    // ── 控制标志 ──────────────────────────────────────────
    std::atomic<bool> stop_requested_{false};

    // ── 记忆 ─────────────────────────────────────────────
    int last_consolidated_count_ = 0;

    // ── 元数据 ────────────────────────────────────────────
    std::string session_id_;
    std::string task_description_;
    std::shared_ptr<LLMProvider> provider_;
    std::string base_url_;
    std::string api_key_;
    int timeout_ = 60;
    std::string session_log_dir_;
    int last_flushed_index_ = 0;
    bool is_active_ = true;

    // ── IM 多用户归属(元数据,不进文件路径)──────────────
    std::string owner_id_;
    std::string group_id_;
    SessionType session_type_ = SessionType::kDirect;

    // ── 本条消息发送者(瞬时输入,AddUserMessage 消费后清空)──
    std::string current_sender_id_;
    std::string current_sender_name_;

    // ── 本条消息随附图片(瞬时输入,AddUserMessage 消费后清空)──
    std::vector<std::string> next_image_paths_;

    // ── 会话锁 ────────────────────────────────────────────
    std::mutex session_mutex_;

    // ── 运行时状态 ────────────────────────────────────────
    SteadyClock::TimePoint created_at_;
    SteadyClock::TimePoint last_active_;

    // ── 渲染锁保护的内部数据 ──────────────────────────────
    std::vector<MessageSchema> messages_;
    std::vector<SystemSchema> system_prompt_;
    AgentRuntimeState state_ = AgentRuntimeState::IDLE;
    std::string state_message_;
    std::string streaming_text_;
    std::string streaming_thinking_;
    // ── TTS pending chunks ──────────────────────────────
    ListBuffer<std::string> tts_chunks_;
    TtsSpeakCallback tts_speak_callback_;
    mutable std::mutex render_mutex_;

    // ── 内部实现 ──────────────────────────────────────────
    std::optional<AgentRole> mutable_role_;

    // ── Token/s 跟踪 ──────────────────────────────────────
    size_t streaming_char_count_ = 0;
    SteadyClock::TimePoint stream_start_time_;
    float streaming_token_speed_ = 0.0f;
};

}  // namespace prosophor

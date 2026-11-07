// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/agent_session.h"

#include <algorithm>
#include <unordered_map>
#include <memory>

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "common/file_utils.h"

namespace {

/// 按路径互斥锁：按 owner 分目录后各会话独享文件(竞争归零),
/// 仍保护共享的平面回退文件(空 owner TUI / 净化失败)与跨进程 append
std::mutex g_flush_locks_mutex;
std::unordered_map<std::string, std::unique_ptr<std::mutex>> g_flush_locks;

std::unique_lock<std::mutex> GetFlushLock(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_flush_locks_mutex);
    auto it = g_flush_locks.find(path);
    if (it == g_flush_locks.end()) {
        g_flush_locks.emplace(path, std::make_unique<std::mutex>());
        it = g_flush_locks.find(path);
    }
    return std::unique_lock<std::mutex>(*it->second);
}


}  // anonymous namespace

namespace prosophor {

AgentSession::AgentSession(const std::string& sid,
                            const std::string& task, AgentRole* r)
    : role_(r), session_id_(sid), task_description_(task) {
    created_at_ = SteadyClock::Now();
    last_active_ = SteadyClock::Now();

    if (r) {
        provider_ = LlmProviderRouter::GetInstance().GetProviderByName(r->provider_prot);
        use_tools_ = r->enable_tools;
        working_directory_.clear();
        messages_.clear();
        system_prompt_.clear();
    }
}

AgentSession::AgentSession(AgentSession&& other) noexcept
    : tool_executor_(std::move(other.tool_executor_)),
      output_callback_(std::move(other.output_callback_)),
      role_(other.role_),
      use_tools_(other.use_tools_),
      stateless_(other.stateless_),
      auto_confirm_tools_(other.auto_confirm_tools_),
      working_directory_(std::move(other.working_directory_)),
      related_files_(std::move(other.related_files_)),
      stop_requested_(other.stop_requested_.load()),
      last_consolidated_count_(other.last_consolidated_count_),
      session_id_(std::move(other.session_id_)),
      task_description_(std::move(other.task_description_)),
      provider_(std::move(other.provider_)),
      base_url_(std::move(other.base_url_)),
      api_key_(std::move(other.api_key_)),
      timeout_(other.timeout_),
      session_log_dir_(std::move(other.session_log_dir_)),
      last_flushed_index_(other.last_flushed_index_),
      is_active_(other.is_active_),
      owner_id_(std::move(other.owner_id_)),
      group_id_(std::move(other.group_id_)),
      session_type_(other.session_type_),
      current_sender_id_(std::move(other.current_sender_id_)),
      current_sender_name_(std::move(other.current_sender_name_)),
      created_at_(other.created_at_),
      last_active_(other.last_active_),
      messages_(std::move(other.messages_)),
      system_prompt_(std::move(other.system_prompt_)),
      state_(other.state_),
      state_message_(std::move(other.state_message_)),
      streaming_text_(std::move(other.streaming_text_)),
      streaming_thinking_(std::move(other.streaming_thinking_)),
      mutable_role_(std::move(other.mutable_role_)) {
}

AgentSession& AgentSession::operator=(AgentSession&& other) noexcept {
    if (this == &other) return *this;

    tool_executor_ = std::move(other.tool_executor_);
    output_callback_ = std::move(other.output_callback_);
    role_ = other.role_;
    use_tools_ = other.use_tools_;
    stateless_ = other.stateless_;
    auto_confirm_tools_ = other.auto_confirm_tools_;
    working_directory_ = std::move(other.working_directory_);
    related_files_ = std::move(other.related_files_);
    stop_requested_.store(other.stop_requested_.load());
    session_id_ = std::move(other.session_id_);
    task_description_ = std::move(other.task_description_);
    provider_ = std::move(other.provider_);
    base_url_ = std::move(other.base_url_);
    api_key_ = std::move(other.api_key_);
    timeout_ = other.timeout_;
    session_log_dir_ = std::move(other.session_log_dir_);
    last_flushed_index_ = other.last_flushed_index_;
    last_consolidated_count_ = other.last_consolidated_count_;
    is_active_ = other.is_active_;
    owner_id_ = std::move(other.owner_id_);
    group_id_ = std::move(other.group_id_);
    session_type_ = other.session_type_;
    current_sender_id_ = std::move(other.current_sender_id_);
    current_sender_name_ = std::move(other.current_sender_name_);
    created_at_ = other.created_at_;
    last_active_ = other.last_active_;
    messages_ = std::move(other.messages_);
    system_prompt_ = std::move(other.system_prompt_);
    state_ = other.state_;
    state_message_ = std::move(other.state_message_);
    streaming_text_ = std::move(other.streaming_text_);
    streaming_thinking_ = std::move(other.streaming_thinking_);
    mutable_role_ = std::move(other.mutable_role_);
    return *this;
}

void AgentSession::SetOutput(AgentRuntimeState new_state,
                              const std::string& state_msg,
                              const std::optional<MessageSchema>& reply,
                              const std::string& delta) {

    // 更新输出信号
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        state_ = new_state;
        state_message_ = state_msg;

        // 流式帧:增量从 delta 取(类型由 state 决定),不再借用 MessageSchema
        if (new_state == AgentRuntimeState::STREAM_CONTENT_TYPING) {
            streaming_text_ += delta;
            // Token/s tracking: count chars, estimate ~4 chars per token
            streaming_char_count_ += delta.size();
            auto elapsed = std::chrono::duration<float>(
                SteadyClock::Now() - stream_start_time_).count();
            if (elapsed > 0.1f) {
                streaming_token_speed_ = (streaming_char_count_ / 4.0f) / elapsed;
            }
        }
        if (new_state == AgentRuntimeState::STREAM_CONTENT_START) {
            stream_start_time_ = SteadyClock::Now();
            streaming_char_count_ = 0;
            streaming_token_speed_ = 0.0f;
            streaming_text_.clear();
        }
        if (new_state == AgentRuntimeState::STREAM_THINKING) {
            streaming_thinking_ += delta;
        }
        if (new_state == AgentRuntimeState::STREAM_THINKING_START) {
            stream_start_time_ = SteadyClock::Now();
            streaming_char_count_ = 0;
            streaming_token_speed_ = 0.0f;
            streaming_thinking_.clear();
        }
        if (reply && (
            new_state == AgentRuntimeState::STREAM_MODE_COMPLETE ||
            new_state == AgentRuntimeState::COMPLETE ||
            new_state == AgentRuntimeState::TOOL_USE ||
            new_state == AgentRuntimeState::STATE_ERROR)) {
            messages_.push_back(*reply);
            streaming_text_.clear();
            streaming_thinking_.clear();
        }
    }

    // 输出信号驱动动作或渲染
    if (output_callback_) {
        output_callback_(session_id_, role_ ? role_->id : "",
                        new_state, state_msg, reply, delta);
    }
}

RenderSnapshot AgentSession::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(render_mutex_);
    RenderSnapshot snap;
    snap.session_id = session_id_;
    snap.role_id = role_ ? role_->id : "";
    snap.state = state_;
    snap.state_message = state_message_;
    // 只拷贝最后 10 条用于 UI 渲染，避免随对话增长的全量拷贝
    constexpr size_t kMaxVisibleMessages = 10;
    if (messages_.size() > kMaxVisibleMessages) {
        snap.messages.assign(messages_.end() - kMaxVisibleMessages, messages_.end());
    } else {
        snap.messages = messages_;
    }
    snap.streaming_text = streaming_text_;
    snap.streaming_thinking = streaming_thinking_;
    snap.streaming_token_speed = streaming_token_speed_;
    return snap;
}

void AgentSession::SetCurrentSender(const std::string& sender_id,
                                    const std::string& sender_name) {
    std::lock_guard<std::mutex> lock(render_mutex_);
    current_sender_id_ = sender_id;
    current_sender_name_ = sender_name;
}

void AgentSession::SetNextImages(std::vector<std::string> image_paths) {
    std::lock_guard<std::mutex> lock(render_mutex_);
    next_image_paths_ = std::move(image_paths);
}

void AgentSession::ClearHistoryImages() {
    std::lock_guard<std::mutex> lock(render_mutex_);
    for (auto& message : messages_) {
        message.content.erase(
            std::remove_if(message.content.begin(), message.content.end(),
                           [](const ContentSchema& block) {
                               return block.type == "image";
                           }),
            message.content.end());
    }
}

void AgentSession::AddUserMessage(const std::string& text) {
    std::lock_guard<std::mutex> lock(render_mutex_);
    // 消费式读取瞬时 sender:右值构造取走并清空,不会泄漏到下一条消息
    MessageSchema message("user", text,
                          std::move(current_sender_id_),
                          std::move(current_sender_name_));
    // 同理消费随附图片:按内容块顺序追加在文本之后, 逐张挂上 image 块
    for (auto& path : std::move(next_image_paths_)) {
        message.AddImageContent(std::move(path));
    }
    next_image_paths_.clear();
    messages_.push_back(std::move(message));
}

void AgentSession::CleanupInterruptedLoop() {
    std::lock_guard<std::mutex> lock(render_mutex_);
    if (!messages_.empty() && messages_.back().role == "user") {
        messages_.pop_back();
    }
}

void AgentSession::CompactHistory(const std::vector<MessageSchema>& kept_messages,
                                   const std::string& summary) {
    std::lock_guard<std::mutex> lock(render_mutex_);
    messages_ = kept_messages;
    if (!summary.empty()) {
        system_prompt_.clear();
        system_prompt_.push_back({"text", summary, false});
    }
}

void AgentSession::SetSystemPrompt(const std::vector<SystemSchema>& prompt) {
    system_prompt_ = prompt;
}

void AgentSession::FlushToDisk() {
    std::lock_guard<std::mutex> lock(render_mutex_);
    if (stateless_) return;   // 无状态会话不落盘，避免感知历史写满 JSONL
    if (session_log_dir_.empty()) return;
    if (last_flushed_index_ >= static_cast<int>(messages_.size())) return;

    std::filesystem::create_directories(session_log_dir_);
    auto filepath = std::filesystem::path(session_log_dir_) /
                    (SystemClock::GetCurrentDate() + ".jsonl");

    std::string batch;
    for (int i = last_flushed_index_; i < static_cast<int>(messages_.size()); ++i) {
        nlohmann::json j;
        j["role"] = messages_[i].role;
        j["ts"] = SystemClock::GetCurrentTimestamp();
        j["sender"] = messages_[i].sender_id;  // 恒定写(sender 空也写空串,兼容旧格式)

        if (!owner_id_.empty()) j["owner"] = owner_id_;
        if (session_type_ == SessionType::kGroup)
            j["group"] = group_id_.empty() ? owner_id_ : group_id_;
        j["session_type"] = SessionTypeToString(session_type_);

        for (const auto& block : messages_[i].content) {
            if (block.type == "text") {
                j["content"] = block.text;
            } else if (block.type == "tool_use") {
                j["tool"] = block.name;
                j["args"] = block.input;
            } else if (block.type == "tool_result") {
                j["result"] = block.content;
            }
        }
        batch += j.dump() + "\n";
    }

    // 同 role 多会话(多用户)并发 append 同一日期文件,按路径互斥
    auto flush_lock = GetFlushLock(filepath.string());
    if (!WriteFile(filepath.string(), batch, true)) {
        LOG_ERROR("Failed to flush session log: {}", filepath.string());
        return;
    }

    last_flushed_index_ = static_cast<int>(messages_.size());
    LOG_DEBUG("Flushed {} messages to {}", last_flushed_index_, filepath.string());
}

void AgentSession::ApplyProviderOverride(const std::string& provider_name,
                                          const std::string& model) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    auto& router = LlmProviderRouter::GetInstance();
    provider_ = router.GetProviderByName(provider_name);

    mutable_role_ = *role_;
    role_ = &mutable_role_.value();
    role_->provider_prot = provider_name;

    auto& config = ProsophorConfig::GetInstance();
    auto prov_it = config.llm_providers.find(provider_name);
    if (prov_it == config.llm_providers.end()) return;

    auto& model_configs = prov_it->second.model_configs;
    base_url_ = prov_it->second.base_url;
    api_key_ = prov_it->second.api_key;
    timeout_ = prov_it->second.timeout;

    const ModelConfig* matched = nullptr;
    auto model_it = model_configs.find(model);
    if (model_it != model_configs.end()) {
        matched = &model_it->second;
    } else if (!model.empty()) {
        for (auto& [k, v] : model_configs) {
            if (v.model == model) {
                matched = &v;
                break;
            }
        }
    }

    if (matched) {
        role_->model = matched->model;
        std::string entry_base_url;
        std::string entry_api_key;
        int entry_timeout = 0;
        if (prov_it->second.FindEntryForModel(provider_name, model,
                                               entry_base_url,
                                               entry_api_key,
                                               entry_timeout)) {
            base_url_ = entry_base_url;
            api_key_ = entry_api_key;
            timeout_ = entry_timeout;
        }
        role_->temperature = matched->temperature;
        role_->max_tokens = matched->max_tokens;
        // Don't override thinking from model config — role JSON is the source of truth
        LOG_DEBUG("Applied provider override: provider={}, model={}, base_url={}",
                 provider_name, matched->model, base_url_);
    } else {
        LOG_WARN("No matching agent for '{}' in provider '{}', using provider defaults",
                 model, provider_name);
    }
}

}  // namespace prosophor

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/agent_session_manager.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>

#include "common/log_wrapper.h"
#include "config/config.h"
#include "managers/agent_role_loader.h"
#include "common/time_wrapper.h"
#include "common/file_utils.h"
#include "managers/permission_manager.h"
#include "core/memory_manager.h"
#include "core/session_key.h"
#include "providers/llm_provider_router.h"

namespace prosophor {

namespace {

/// 读取记忆文件并追加到 prompt;文件不存在/为空时静默跳过
void AppendMemoryFileIfExists(std::ostringstream& prompt,
                              const std::filesystem::path& file) {
    auto content = ReadFile(file.string());
    if (content.has_value() && !content->empty()) {
        prompt << "### " << file.stem().string() << "\n" << content.value() << "\n\n";
    }
}

}  // namespace

AgentSessionManager& AgentSessionManager::GetInstance() {
    static AgentSessionManager instance;
    return instance;
}

void AgentSessionManager::Initialize(ToolExecutorCallback tool_executor) {
    tool_executor_ = tool_executor;
    LOG_DEBUG("AgentSessionManager initialized");

    // Load roles from install config dir (shipped with app), fallback to user data dir
    auto install_roles = prosophor::ProsophorConfig::InstallConfigDir() / "roles";
    if (DirExists(install_roles.string())) {
        LoadRolesFromDirectory(install_roles.string());
    }
    auto user_roles = prosophor::ProsophorConfig::BaseDir() / "roles";
    if (DirExists(user_roles.string())) {
        LoadRolesFromDirectory(user_roles.string());
    }
}

void AgentSessionManager::SetToolExecutor(ToolExecutorCallback tool_executor) {
    tool_executor_ = tool_executor;
}

void AgentSessionManager::SetOutputCallback(SessionOutputCallback callback) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    output_callbacks_.clear();
    output_callbacks_.push_back(std::move(callback));
}

void AgentSessionManager::AddOutputCallback(SessionOutputCallback callback) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    output_callbacks_.push_back(std::move(callback));
}

void AgentSessionManager::ClearOutputCallbacks() {
    std::lock_guard<std::mutex> lock(output_mutex_);
    output_callbacks_.clear();
}

void AgentSessionManager::RegisterRole(const AgentRole& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    roles_[role.id] = role;
    LOG_DEBUG("Registered role: {} ({})", role.name, role.id);
}

void AgentSessionManager::LoadRolesFromDirectory(const std::string& roles_dir) {
    auto& loader = AgentRoleLoader::GetInstance();
    auto roles = loader.LoadAllRoles(roles_dir);

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& role : roles) {
        roles_[role.id] = role;
    }

    LOG_DEBUG("Loaded roles from {}", roles.size(), roles_dir);
}

const AgentRole* AgentSessionManager::GetRole(const std::string& role_id) const {
    auto it = roles_.find(role_id);
    return it != roles_.end() ? &it->second : nullptr;
}

std::vector<std::string> AgentSessionManager::ListRoles() const {
    std::vector<std::string> role_ids;
    for (const auto& [id, role] : roles_) {
        role_ids.push_back(id);
    }
    return role_ids;
}

std::string AgentSessionManager::GenerateSessionId(const std::string& role_id) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, 999999);

    return role_id + "-" + std::to_string(dist(gen));
}

std::string AgentSessionManager::CreateSession(const std::string& role_id,
                                               const std::string& task_desc,
                                               const std::string& owner_id,
                                               SessionType session_type) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = roles_.find(role_id);
    if (it == roles_.end()) {
        throw std::runtime_error("Role not found: " + role_id);
    }

    AgentRole& role = it->second;
    std::string session_id = GenerateSessionId(role_id);

    // settings.json 的 enable_summary 为最终确定值，覆盖 role 的配置
    role.enable_summary = ProsophorConfig::GetInstance().enable_summary;

    AgentSession session(session_id, task_desc, &role);
    session.SetAutoConfirmTools(role.auto_confirm_tools);

    // IM 多用户归属:owner 净化后进日志目录段(sessions/{role}/{owner}/),
    // 同时作为元数据参与记忆注入/提取归属与 JSONL 字段
    session.SetOwnerId(owner_id);
    session.SetSessionType(session_type);
    if (session_type == SessionType::kGroup) {
        session.SetGroupId(owner_id);  // 群会话:owner 即群 ID
    }
    // Per-session tool executor: temporarily elevates permission when auto_confirm is set
    {
        bool auto_confirm = session.GetAutoConfirmTools();
        ToolExecutorCallback base_executor = tool_executor_;
        session.tool_executor_ = [auto_confirm, base_executor](
                const std::string& tool_name, const nlohmann::json& args) -> std::string {
            if (auto_confirm) {
                auto& perm = PermissionManager::GetInstance();
                auto prev = perm.GetMode();
                perm.SetMode("auto");
                auto result = base_executor(tool_name, args);
                perm.SetMode(prev);
                return result;
            }
            return base_executor(tool_name, args);
        };
    }
    // Per-session output callback: set after map insert (see below)

    // Inject memory consolidation service (singleton instance)

    // 初始化 Session 持久化日志目录:sessions/{role}/[/{owner}] 按聊天分文件,
    // 同聊天独享文件免写锁;owner 空(本地 TUI)或净化失败回退平面布局
    auto base_dir = prosophor::ProsophorConfig::BaseDir();
    auto log_dir = base_dir / "sessions" / role_id;
    if (!owner_id.empty()) {
        auto safe_owner = SanitizeIdentity(owner_id);
        if (safe_owner) {
            log_dir /= *safe_owner;
        } else {
            LOG_WARN("Owner failed sanitize, session logs fall back to flat: {}", owner_id);
        }
    }
    session.SetSessionLogDir(log_dir.string());
    std::filesystem::create_directories(log_dir);

    // 初始化工作目录（优先使用配置的 workspace_path）
    {
        auto& cfg = ProsophorConfig::GetInstance();
        std::string wd = cfg.workspace_path.empty()
            ? std::filesystem::current_path().string()
            : cfg.workspace_path;
        session.SetWorkingDirectory(wd);
    }

    // 初始化 base_url/api_key/timeout（从 provider entry 中按 model 查找）
    {
        auto& config = ProsophorConfig::GetInstance();

        bool is_inprocess = (role.provider_prot == "local" || role.provider_prot == "llamacpp");

        // In-process provider — use fake base_url="local" to bypass cloud config and api_key checks
        if (is_inprocess) {
            session.SetBaseUrl("local");
            session.SetApiKey("");
            session.SetTimeout(300);
        } else {
            auto prov_it = config.llm_providers.find(role.provider_prot);
            if (prov_it != config.llm_providers.end()) {
                LOG_INFO("Looking up provider '{}' for model '{}'", role.provider_prot, role.model);
                std::string entry_base_url;
                std::string entry_api_key;
                int entry_timeout = 0;
                if (prov_it->second.FindEntryForModel(role.provider_prot, role.model,
                                                        entry_base_url, entry_api_key, entry_timeout)) {
                    session.SetBaseUrl(entry_base_url);
                    session.SetApiKey(entry_api_key);
                    session.SetTimeout(entry_timeout);
                    LOG_INFO("Found model-specific config: url='{}', api_key='{}...', timeout={}s",
                             entry_base_url,
                             entry_api_key.size() > 8 ? entry_api_key.substr(0, 8) : entry_api_key,
                             entry_timeout);
                }
                // Fallback to provider-level config if model-specific lookup fails
                if (session.GetBaseUrl().empty() && !prov_it->second.base_url.empty()) {
                    session.SetBaseUrl(prov_it->second.base_url);
                }
                if (session.GetApiKey().empty() && !prov_it->second.api_key.empty()) {
                    session.SetApiKey(prov_it->second.api_key);
                }
                if (session.GetTimeout() <= 0 && prov_it->second.timeout > 0) {
                    session.SetTimeout(prov_it->second.timeout);
                }
            } else {
                LOG_ERROR("Provider '{}' not found in config for session '{}'",
                          role.provider_prot, session_id);
            }
            // Validation (cloud providers only)
            if (session.GetBaseUrl().empty()) {
                LOG_FATAL("Failed to set base_url for session '{}' (role: {}, provider: '{}')",
                          session_id, role_id, role.provider_prot);
            }
            bool is_local = session.GetBaseUrl().find("localhost") != std::string::npos
                            || session.GetBaseUrl().find("127.0.0.1") != std::string::npos;
            if (session.GetApiKey().empty() && !is_local) {
                LOG_FATAL("Failed to set api_key for session '{}' (role: {}, provider: '{}'). "
                          "Please check your settings.json provider configuration.",
                          session_id, role_id, role.provider_prot);
            }
        }
    }

    // 构建 system prompt
    session.SetSystemPrompt(BuildSystemPrompt(session));

    sessions_[session_id] = std::make_unique<AgentSession>(std::move(session));

    // Set callbacks after map insert: raw pointer is now stable for the session's lifetime.
    // Session keeps a single std::function that fans out to all registered frontends.
    sessions_[session_id]->output_callback_ =
        [this](const std::string& sid, const std::string& cb_role_id,
               AgentRuntimeState state, const std::string& state_msg,
               const std::optional<MessageSchema>& reply, const std::string& delta) {
            std::vector<SessionOutputCallback> callbacks;
            {
                std::lock_guard<std::mutex> cb_lock(output_mutex_);
                callbacks = output_callbacks_;
            }
            for (const auto& cb : callbacks) {
                if (cb) {
                    cb(sid, cb_role_id, state, state_msg, reply, delta);
                }
            }
        };

    LOG_DEBUG("Created session: {} for role: {} (task: {})",
             session_id, role_id, task_desc);
    LOG_DEBUG("  Role Memory: {}", role.memory_dir);

    return session_id;
}

std::string AgentSessionManager::SendToSession(const std::string& session_id,
                                               const std::string& message,
                                               const std::string& sender_id,
                                               const std::string& sender_name) {
    return SendToSession(session_id, message, std::vector<std::string>{}, sender_id, sender_name);
}

std::string AgentSessionManager::SendToSession(const std::string& session_id,
                                               const std::string& message,
                                               const std::vector<std::string>& image_paths,
                                               const std::string& sender_id,
                                               const std::string& sender_name) {
    auto session = GetSessionShared(session_id);
    if (!session) {
        throw std::runtime_error("Session not found: " + session_id);
    }

    session->UpdateLastActive();
    std::string reply;
    {
        auto lock = session->ScopedLock();
        session->SetCurrentSender(sender_id, sender_name);
        // 与 Loop 同锁设置附件：分两次加锁会留下"设图"与"取图"之间的竞态窗口
        session->SetNextImages(image_paths);
        reply = AgentCore::Loop(message, *session);
    }

    // 回复由 Loop 直接交出：消息列表末条在失败路径下会退回上一轮的回复，
    // 不能用来反推本轮结果
    return reply;
}

void AgentSessionManager::SendToSessionAsync(const std::string& session_id,
                                             const std::string& message,
                                             const std::string& sender_id,
                                             const std::string& sender_name) {
    // pending buffer 模式 —— 连续输入合并为一次 LLM 调用
    //
    // 时序:
    //   输入 A → 追加 → task_active=false → 提交 StartChain（只取 A，跑 Loop(A)）
    //   输入 B → 追加 → task_active=true  → return
    //   输入 C → 追加 → task_active=true  → return
    //   Loop(A) 完成后 → ContinueChain → drain [B,C] 合并 → Loop("B\n\nC")
    bool should_start;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_inputs_[session_id].push_back({message, sender_id, sender_name});
        should_start = !task_active_[session_id];
        task_active_[session_id] = true;
    }
    if (!should_start) return;

    auto session = GetSessionShared(session_id);
    if (!session) {
        LOG_ERROR("Session not found for async task: {}", session_id);
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_inputs_.erase(session_id);
        task_active_.erase(session_id);
        std::vector<SessionOutputCallback> callbacks;
        {
            std::lock_guard<std::mutex> olock(output_mutex_);
            callbacks = output_callbacks_;
        }
        for (const auto& cb : callbacks) {
            if (cb) {
                cb(session_id, "", AgentRuntimeState::STATE_ERROR,
                   "Session not found: " + session_id, std::nullopt, {});
            }
        }
        return;
    }

    StartChain(session_id, std::move(session));
}

void AgentSessionManager::StartChain(const std::string& session_id,
                                      std::shared_ptr<AgentSession> session) {
    GetGlobalThreadPool().Submit([this, session_id, session]() {
        // 线程池会吞掉任务异常:若不兜底,一旦 Loop 抛异常,for 循环中断,
        // task_active_ 永远停在 true → 该会话从此只进缓冲、永不回复。
        // 兜底:重置任务标志 + 通知前端,让会话可以继续接收下一条消息。
        std::string crash;
        try {
        // 生产-消费循环：每次 swap 一批 → 持锁 Loop，中间释放锁
        for (;;) {
            std::vector<PendingInput> batch;
            {
                std::lock_guard<std::mutex> plock(pending_mutex_);
                auto& buf = pending_inputs_[session_id];
                if (buf.empty()) {
                    task_active_[session_id] = false;
                    break;
                }
                std::swap(batch, buf);  // O(1) 取出全部，不持锁迭代
            }
            session->UpdateLastActive();

            // 批内 sender 全相同(含全空)→ 合并为一次 LLM 调用(原有行为);
            // sender 混合(群聊多成员连发)→ 逐条 Loop,保持每条消息的归属
            bool same_sender = true;
            for (size_t i = 1; i < batch.size(); ++i) {
                if (batch[i].sender_id != batch[0].sender_id ||
                    batch[i].sender_name != batch[0].sender_name) {
                    same_sender = false;
                    break;
                }
            }
            if (same_sender) {
                std::string merged;
                for (auto& e : batch) {
                    if (!merged.empty()) merged += "\n\n";
                    merged += std::move(e.text);
                }
                auto lock = session->ScopedLock();
                session->ClearStopRequested();
                session->SetCurrentSender(batch[0].sender_id, batch[0].sender_name);
                AgentCore::Loop(merged, *session);
            } else {
                // 群聊混合 sender:逐条 Loop,每条先设置发送者再消费
                for (auto& e : batch) {
                    auto lock = session->ScopedLock();
                    session->ClearStopRequested();
                    session->SetCurrentSender(e.sender_id, e.sender_name);
                    AgentCore::Loop(e.text, *session);
                }
            }
        }
        } catch (const std::exception& e) {
            crash = e.what();
        } catch (...) {
            crash = "unknown exception";
        }
        if (!crash.empty()) {
            LOG_ERROR("StartChain (session={}) crashed: {}; resetting task flag",
                      session_id, crash);
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                task_active_[session_id] = false;
            }
            // 通知前端:失败可见,而不是静默消失
            std::vector<SessionOutputCallback> callbacks;
            {
                std::lock_guard<std::mutex> olock(output_mutex_);
                callbacks = output_callbacks_;
            }
            for (const auto& cb : callbacks) {
                if (cb) {
                    cb(session_id, "", AgentRuntimeState::STATE_ERROR,
                       "chain crashed: " + crash, std::nullopt, {});
                }
            }
        }
    });
}

AgentSession* AgentSessionManager::GetSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    return it != sessions_.end() ? it->second.get() : nullptr;
}

const AgentSession* AgentSessionManager::GetSession(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    return it != sessions_.end() ? it->second.get() : nullptr;
}

std::shared_ptr<AgentSession> AgentSessionManager::GetSessionShared(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return nullptr;
    }
    // unique_ptr 管理对象生命周期，shared_ptr 用 no-op deleter 共享访问
    // 安全：map rehash 只移动 unique_ptr，对象地址不变
    return std::shared_ptr<AgentSession>(it->second.get(), [](AgentSession*){});
}

std::vector<AgentSession*> AgentSessionManager::GetSessionsByRole(const std::string& role_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AgentSession*> result;
    for (auto& [id, session] : sessions_) {
        if (session->GetRole()->id == role_id && session->IsActive()) {
            result.push_back(session.get());
        }
    }
    return result;
}

std::vector<const AgentSession*> AgentSessionManager::GetSessionsByRole(const std::string& role_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const AgentSession*> result;
    for (const auto& [id, session] : sessions_) {
        if (session->GetRole()->id == role_id && session->IsActive()) {
            result.push_back(session.get());
        }
    }
    return result;
}

std::vector<AgentSession*> AgentSessionManager::GetActiveSessions(int minutes) {
    auto now = SteadyClock::Now();
    auto threshold = std::chrono::minutes(minutes);

    std::vector<AgentSession*> result;
    for (auto& [id, session] : sessions_) {
        if (session->IsActive() && (now - session->GetLastActive()) < threshold) {
            result.push_back(session.get());
        }
    }
    return result;
}

void AgentSessionManager::FlushSession(const std::string& session_id) {
    // 异步执行:FlushSession 可能在 SetOutput 回调链内被调用(调用线程已持
    // session 的 render_mutex_),FlushToDisk 又要拿 render_mutex_ →
    // std::mutex 同线程重入死锁;投递全局线程池避开调用链。
    GetGlobalThreadPool().Submit([this, session_id]() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return;
        auto session_lock = it->second->ScopedLock();
        it->second->FlushToDisk();
    });
}

void AgentSessionManager::CloseSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        AgentSession& session = *it->second;

        {   // session_mutex: 确保与 Loop 路径不并发读写 session 状态
            auto session_lock = session.ScopedLock();

            // 先将未写入的对话刷入持久化日志
            session.FlushToDisk();

            if (auto* role = session.GetRole()) {
                if (role->memory_strategy) {
                    role->memory_strategy->TryExitConsolidation(session);
                }
            }

            session.SetActive(false);
        }
        LOG_INFO("Closed session: {}", session_id);
    }

    // 清理 pending buffer
    std::lock_guard<std::mutex> plock(pending_mutex_);
    pending_inputs_.erase(session_id);
    task_active_.erase(session_id);
}

std::vector<std::string> AgentSessionManager::ListSessions() const {
    std::vector<std::string> session_ids;
    for (const auto& [id, session] : sessions_) {
        if (session->IsActive()) {
            session_ids.push_back(id);
        }
    }
    return session_ids;
}

std::string AgentSessionManager::GetLastSessionId() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string last_id;
    for (const auto& [id, session] : sessions_) {
        if (session->IsActive()) {
            last_id = id;
        }
    }
    return last_id;
}

std::string AgentSessionManager::GetOrCreateSession(const std::string& role_id,
                                                    const std::string& message_hint,
                                                    const std::string& owner_id,
                                                    SessionType session_type) {
    // 复用条件：同 role + 同 owner + 同会话类型 + 活跃 —— 多用户下不能跨用户复用
    auto sessions = GetSessionsByRole(role_id);
    for (auto* s : sessions) {
        if (s->IsActive() && s->GetOwnerId() == owner_id &&
            s->GetSessionType() == session_type) {
            return s->GetSessionId();
        }
    }

    // 没有匹配的活跃会话，创建新的
    return CreateSession(role_id, message_hint, owner_id, session_type);
}

void AgentSessionManager::BroadcastToSessions(const std::vector<std::string>& session_ids,
                                              const std::string& message) {
    // 异步发送所有消息，不等待结果
    // 每个 session 的结果通过 output_callback 通知
    for (const auto& session_id : session_ids) {
        SendToSessionAsync(session_id, message);
    }
}

void AgentSessionManager::BroadcastToRole(
    const std::string& role_id,
    const std::string& message) {

    auto sessions = GetActiveSessions(30);  // 最近 30 分钟
    std::vector<std::string> session_ids;

    for (auto* session : sessions) {
        if (session->GetRole()->id == role_id) {
            session_ids.push_back(session->GetSessionId());
        }
    }

    BroadcastToSessions(session_ids, message);
}

void AgentSessionManager::SwitchRoleForSession(const std::string& session_id,
                                                const std::string& new_role_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        throw std::runtime_error("Session not found: " + session_id);
    }

    auto role_it = roles_.find(new_role_id);
    if (role_it == roles_.end()) {
        throw std::runtime_error("Role not found: " + new_role_id);
    }

    AgentSession& session = *it->second;

    {   // session_mutex: 与 Loop 路径不并发读写 role/provider/base_url/system_prompt
        auto session_lock = session.ScopedLock();
        session.SetRole(&role_it->second);

        // 全局 enable_summary 覆盖角色配置（与 CreateSession 保持一致）
        role_it->second.enable_summary = ProsophorConfig::GetInstance().enable_summary;

        session.SetProvider(LlmProviderRouter::GetInstance().GetProviderByName(session.GetRole()->provider_prot));

        auto& config = ProsophorConfig::GetInstance();
        auto prov_it = config.llm_providers.find(session.GetRole()->provider_prot);
        if (prov_it != config.llm_providers.end()) {
            session.SetBaseUrl(prov_it->second.base_url);
            session.SetApiKey(prov_it->second.api_key);
            session.SetTimeout(prov_it->second.timeout);
        }

        // Update working directory from config
        if (!config.workspace_path.empty())
            session.SetWorkingDirectory(config.workspace_path);

        session.SetSystemPrompt(BuildSystemPrompt(session));
    }

    LOG_INFO("Switched session {} to role: {}", session_id, new_role_id);
    LOG_INFO("  Role Memory (from new role): {}", session.GetRole()->memory_dir);
}

void AgentSessionManager::RebuildSystemPromptForRole(const std::string& role_id) {
    auto sessions = GetSessionsByRole(role_id);
    for (auto* session : sessions) {
        if (!session) continue;
        auto lock = session->ScopedLock();
        session->SetSystemPrompt(BuildSystemPrompt(*session));
    }
    LOG_DEBUG("Rebuilt system prompt for {} sessions of role {}", sessions.size(), role_id);
}

std::vector<SystemSchema> AgentSessionManager::BuildSystemPrompt(const AgentSession& session) {
    std::ostringstream prompt;

    // 0. 项目信息
    prompt << "## 项目信息\n\n"
           << "当前项目：Prosophor\n"
           << "配置目录：~/.prosophor/\n"
           << "  - config/settings.json — 全局配置\n"
           << "  - roles/ — 角色定义\n"
           << "  - skills/ — 技能文件（可用 /skills list 查看）\n"
           << "  - sessions/ — 会话记录\n";

    if (!session.GetWorkingDirectory().empty()) {
        prompt << "当前工作目录：" << session.GetWorkingDirectory() << "\n";
    }

    prompt << "\n";

    // 0.5 IM 多用户记忆分层注入(ADR-IM3)
    // 私聊:users/{owner}/rules → profile → preferences(规则>事实>偏好)
    // 群聊:groups/{gid}/rules → profile + 活跃成员事实性 profile(轻量)
    if (!session.GetOwnerId().empty()) {
        if (session.IsGroupSession()) {
            auto gdir = GroupMemoryDir(session.GetOwnerId());
            if (!gdir.empty()) {
                prompt << "## 群记忆\n\n";
                AppendMemoryFileIfExists(prompt, gdir / "rules.md");
                AppendMemoryFileIfExists(prompt, gdir / "profile.md");
            }
            // 活跃成员简介:从会话消息惰性收集 distinct sender,只注入事实(不注入私人规则/偏好)
            std::vector<std::string> members;
            for (const auto& msg : session.GetMessages()) {
                if (msg.role == "user" && !msg.sender_id.empty() &&
                    std::find(members.begin(), members.end(), msg.sender_id) == members.end()) {
                    members.push_back(msg.sender_id);
                }
            }
            if (!members.empty()) {
                prompt << "### 活跃成员简介\n";
                for (const auto& uid : members) {
                    auto udir = UserMemoryDir(uid);
                    if (udir.empty()) continue;
                    auto content = ReadFile((udir / "profile.md").string());
                    if (content.has_value() && !content->empty()) {
                        std::string text = content.value();
                        if (text.size() > 200) text = text.substr(0, 200) + "…";
                        prompt << "- " << uid << ": " << text << "\n";
                    }
                }
                prompt << "\n";
            }
        } else {
            auto udir = UserMemoryDir(session.GetOwnerId());
            if (!udir.empty()) {
                prompt << "## 用户记忆\n\n";
                AppendMemoryFileIfExists(prompt, udir / "rules.md");
                AppendMemoryFileIfExists(prompt, udir / "profile.md");
                AppendMemoryFileIfExists(prompt, udir / "preferences.md");
            }
        }
    }

    // 1. Role Memory (长期记忆 - 习惯/偏好) - 从 AgentRole 封装方法加载
    if (session.GetRole()) {
        std::string memory_content = session.GetRole()->LoadMemoryContent();
        if (!memory_content.empty()) {
            prompt << memory_content;
        }
    }

    // 2. Role 基础 Prompt（System Prompt + Personality）
    if (session.GetRole()) {
        std::string role_prompt = session.GetRole()->BuildPrompt();
        if (!role_prompt.empty()) {
            prompt << role_prompt << "\n";
        }
    }

    // 3. 行为指令
    prompt << "\n## 行为指令\n\n"
           << "执行完一步后立刻进入下一步，不要重复确认。\n";

    // 4. 对话摘要指令
    if (session.GetRole() && session.GetRole()->enable_summary) {
        prompt << "\n每次对话结束时，请在回复末尾添加 [摘要] 标签，然后是对本轮及历史对话的摘要。\n"
               << "摘要按贝尔曼衰减方式生成：\n"
               << "- 本轮新内容：详细保留（高权重 γ→1）\n"
               << "- 历史摘要：随时间衰减，越久远的越简略（低权重 γ^n）\n"
               << "- 关键决策、未解决问题、重要结论：不衰减，始终保留\n"
               << "- 每轮摘要 ≈ 本轮内容 + γ × 上轮摘要\n"
               << "- 将完整摘要放在回复末尾的 [摘要] 标签中。\n";
    }

    return {{"text", prompt.str(), false}};
}

}  // namespace prosophor

// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/agent_session_manager.h"

#include <algorithm>
#include <chrono>
#include <random>

#include "common/log_wrapper.h"
#include "managers/agent_role_loader.h"
#include "common/time_wrapper.h"
#include "common/file_utils.h"
#include "managers/permission_manager.h"
#include "core/memory_consolidation_service.h"
#include "managers/active_interaction_manager.h"

namespace aicode {

AgentSessionManager& AgentSessionManager::GetInstance() {
    static AgentSessionManager instance;
    return instance;
}

void AgentSessionManager::Initialize(std::shared_ptr<MemoryManager> memory_manager,
                                     ToolExecutorCallback tool_executor) {
    memory_manager_ = memory_manager;
    tool_executor_ = tool_executor;
    LOG_INFO("AgentSessionManager initialized");

    // Load roles from ~/.aicode/roles/
    auto roles_dir = aicode::AiCodeConfig::BaseDir() / "roles";
    LoadRolesFromDirectory(roles_dir.string());

    // 初始化主动交互管理器（使用原始指针，避免 shared_ptr 循环依赖）
    auto& active_interaction = ActiveInteractionManager::GetInstance();
    active_interaction.Initialize(this);

    // 设置 LLM 执行回调（用于后台主动提词）
    active_interaction.SetLlmExecuteCallback(
        [this](const std::string& session_id, const std::string& prompt) -> std::string {
            auto* session = this->GetSession(session_id);
            if (!session) {
                LOG_ERROR("Session not found for active interaction: {}", session_id);
                return "";
            }

            ChatRequest req;
            if (session->role) {
                req.model = session->role->model;
                req.temperature = session->role->temperature;
                req.max_tokens = 4096;
            } else {
                req.model = "claude-sonnet-4-6";
                req.temperature = 0.7;
                req.max_tokens = 4096;
            }

            // 构建上下文：包含会话历史
            std::ostringstream context;
            context << "以下是当前会话的历史记录：\n\n";
            for (size_t i = 0; i < session->messages.size() && i < 20; ++i) {
                const auto& msg = session->messages[i];
                context << (msg.role == "user" ? "用户" : "助手") << ": " << msg.text() << "\n";
            }
            context << "\n" << prompt;

            req.AddUserMessage(context.str());
            return session->provider->Chat(req).content_text;
        });

    LOG_INFO("ActiveInteractionManager initialized with LLM callback");
}

void AgentSessionManager::SetToolExecutor(ToolExecutorCallback tool_executor) {
    tool_executor_ = tool_executor;
}

void AgentSessionManager::SetOutputCallback(SessionOutputCallback callback) {
    output_callback_ = callback;
}

void AgentSessionManager::RegisterRole(const AgentRole& role) {
    std::lock_guard<std::mutex> lock(mutex_);
    roles_[role.id] = role;
    LOG_INFO("Registered role: {} ({})", role.name, role.id);
}

void AgentSessionManager::LoadRolesFromDirectory(const std::string& roles_dir) {
    auto& loader = AgentRoleLoader::GetInstance();
    auto roles = loader.LoadAllRoles(roles_dir);

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& role : roles) {
        roles_[role.id] = role;
    }

    LOG_INFO("Loaded {} roles from {}", roles.size(), roles_dir);
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
                                               const std::string& task_desc) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = roles_.find(role_id);
    if (it == roles_.end()) {
        throw std::runtime_error("Role not found: " + role_id);
    }

    const AgentRole& role = it->second;
    std::string session_id = GenerateSessionId(role_id);

    // 如果角色配置了自动确认工具，设置权限模式为 auto（跳过 Permission Required）
    if (role.auto_confirm_tools) {
        auto& perm_manager = PermissionManager::GetInstance();
        perm_manager.SetMode("auto");
        LOG_INFO("Role {} has auto_confirm_tools=true, setting permission mode to 'auto'", role_id);
    }

    AgentSession session(session_id, role_id, task_desc, &role);
    // Copy runtime dependencies from manager
    session.tool_executor = tool_executor_;
    session.output_callback = output_callback_;

    // Inject memory consolidation service (singleton instance)
    session.consolidation_service = &MemoryConsolidationService::GetInstance();

    // 初始化 Session History 目录
    auto base_dir = aicode::AiCodeConfig::BaseDir();
    session.session_history_dir = (base_dir / "sessions" / session_id / "history").string();
    std::filesystem::create_directories(session.session_history_dir);

    // 初始化工作目录（默认为当前工作目录）
    session.working_directory = std::filesystem::current_path().string();

    // 构建 system prompt
    session.system_prompt = BuildSystemPrompt(session);

    sessions_[session_id] = std::move(session);

    LOG_INFO("Created session: {} for role: {} (task: {})",
             session_id, role_id, task_desc);
    LOG_INFO("  Role Memory: {}", role.memory_dir);
    LOG_INFO("  Session History: {}", session.session_history_dir);

    return session_id;
}

std::string AgentSessionManager::SendToSession(const std::string& session_id,
                                               const std::string& message) {
    AgentSession* session = GetSession(session_id);
    if (!session) {
        throw std::runtime_error("Session not found: " + session_id);
    }

    // 更新活跃时间
    session->last_active = SteadyClock::Now();

    // 切换记忆上下文
    SwitchMemoryContext(*session);

    // 触发主动交互：监听用户消息
    ActiveInteractionManager::GetInstance().OnUserMessage(session_id, message);

    // 根据 role 中的 enable_streaming 配置选择流式或非流式模式
    AgentCore::Loop(message, *session);

    // 返回最后一条消息（assistant 回复）
    if (!session->messages.empty()) {
        return session->messages.back().text();
    }

    return "";
}

void AgentSessionManager::SendToSessionAsync(const std::string& session_id,
                                             const std::string& message) {
    auto session = GetSessionShared(session_id);
    if (!session) {
        LOG_ERROR("Session not found for async task: {}", session_id);
        // 通过 output_callback 通知错误
        if (output_callback_) {
            output_callback_(session_id, "", AgentRuntimeState::STATE_ERROR,
                            "Session not found: " + session_id, std::nullopt);
        }
        return;
    }

    // 更新活跃时间
    session->last_active = SteadyClock::Now();

    // 切换记忆上下文
    SwitchMemoryContext(*session);

    // 触发主动交互：监听用户消息
    ActiveInteractionManager::GetInstance().OnUserMessage(session_id, message);

    // 提交到线程池
    thread_pool_.Submit([this, session, message]() {
        try {
            AgentCore::Loop(message, *session);
        } catch (const std::exception& e) {
            // 通过 output_callback 通知错误
            if (output_callback_) {
                output_callback_(session->session_id, session->role_id,
                                AgentRuntimeState::STATE_ERROR, e.what(), std::nullopt);
            }
            LOG_ERROR("SendToSessionAsync error for session {}: {}", session->session_id, e.what());
        }
    });
}

AgentSession* AgentSessionManager::GetSession(const std::string& session_id) {
    auto it = sessions_.find(session_id);
    return it != sessions_.end() ? &it->second : nullptr;
}

const AgentSession* AgentSessionManager::GetSession(const std::string& session_id) const {
    auto it = sessions_.find(session_id);
    return it != sessions_.end() ? &it->second : nullptr;
}

std::shared_ptr<AgentSession> AgentSessionManager::GetSessionShared(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return nullptr;
    }
    // 返回 shared_ptr 但不删除对象（由 map 管理生命周期）
    return std::shared_ptr<AgentSession>(&it->second, [](AgentSession*){});
}

std::vector<AgentSession*> AgentSessionManager::GetSessionsByRole(const std::string& role_id) {
    std::vector<AgentSession*> result;
    for (auto& [id, session] : sessions_) {
        if (session.role_id == role_id && session.is_active) {
            result.push_back(&session);
        }
    }
    return result;
}

std::vector<const AgentSession*> AgentSessionManager::GetSessionsByRole(const std::string& role_id) const {
    std::vector<const AgentSession*> result;
    for (const auto& [id, session] : sessions_) {
        if (session.role_id == role_id && session.is_active) {
            result.push_back(&session);
        }
    }
    return result;
}

std::vector<AgentSession*> AgentSessionManager::GetActiveSessions(int minutes) {
    auto now = SteadyClock::Now();
    auto threshold = std::chrono::minutes(minutes);

    std::vector<AgentSession*> result;
    for (auto& [id, session] : sessions_) {
        if (session.is_active && (now - session.last_active) < threshold) {
            result.push_back(&session);
        }
    }
    return result;
}

void AgentSessionManager::CloseSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        // 取消该会话的所有待处理主动交互任务
        ActiveInteractionManager::GetInstance().CancelSessionTasks(session_id);

        // Perform exit consolidation using injected service
        auto* consolidation_service = it->second.consolidation_service;

        if (consolidation_service) {
            // Set up LLM callback for consolidation
            auto llm_callback = [session = &it->second](const std::string& prompt) -> std::string {
                ChatRequest req;
                if (session->role) {
                    req.model = session->role->model;
                    req.temperature = session->role->temperature;
                    req.max_tokens = 4096;
                } else {
                    req.model = "claude-sonnet-4-6";
                    req.temperature = 0.7;
                    req.max_tokens = 4096;
                }
                req.AddUserMessage(prompt);
                return session->provider->Chat(req).content_text;
            };

            // Consolidate session exit (with explicit callback)
            auto result = consolidation_service->ConsolidateSessionExit(it->second, llm_callback);

            if (!result.summary.empty()) {
                LOG_INFO("Session exit consolidation completed for {}: {} decisions saved",
                         session_id, result.decisions.size());
            }
        }

        // Mark session as inactive
        it->second.is_active = false;
        LOG_INFO("Closed session: {}", session_id);
    }
}

std::vector<std::string> AgentSessionManager::ListSessions() const {
    std::vector<std::string> session_ids;
    for (const auto& [id, session] : sessions_) {
        if (session.is_active) {
            session_ids.push_back(id);
        }
    }
    return session_ids;
}

std::string AgentSessionManager::GetLastSessionId() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string last_id;
    for (const auto& [id, session] : sessions_) {
        if (session.is_active) {
            last_id = id;
        }
    }
    return last_id;
}

std::string AgentSessionManager::GetOrCreateSession(const std::string& role_id,
                                                    const std::string& message_hint) {
    // 尝试找到活跃的会话
    auto sessions = GetSessionsByRole(role_id);

    // 简单策略：复用最近的活跃会话
    // TODO: 可以用语义相似度判断是否相关
    if (!sessions.empty()) {
        return sessions.back()->session_id;
    }

    // 没有活跃会话，创建新的
    return CreateSession(role_id, message_hint);
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
        if (session->role_id == role_id) {
            session_ids.push_back(session->session_id);
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

    AgentSession& session = it->second;

    // 切换角色
    session.role = &role_it->second;
    session.role_id = new_role_id;

    // Session History 保持不变！✅ 项目上下文连续

    // 更新角色信息和 provider
    session.role_id = new_role_id;
    session.role = &role_it->second;
    session.provider = ProviderRouter::GetInstance().GetProviderByName(session.role->provider_name);

    // 重新构建 system prompt（组合新的 Role Memory + 原有的 Session History）
    session.system_prompt = BuildSystemPrompt(session);

    LOG_INFO("Switched session {} to role: {}", session_id, new_role_id);
    LOG_INFO("  Role Memory (from new role): {}", session.role->memory_dir);
    LOG_INFO("  Session History (unchanged): {}", session.session_history_dir);
}

void AgentSessionManager::SwitchMemoryContext(const AgentSession& session) {
    if (!memory_manager_) {
        return;
    }

    // 优先使用 Session History（项目上下文连续）
    if (!session.session_history_dir.empty()) {
        memory_manager_->SetAgentWorkspace(session.session_history_dir);
        LOG_DEBUG("Switched to Session History: {}", session.session_history_dir);
        return;
    }

    // 降级使用 Role Memory（从 role 动态读取）
    if (session.role && !session.role->memory_dir.empty()) {
        memory_manager_->SetAgentWorkspace(session.role->memory_dir);
        LOG_DEBUG("Switched to Role Memory: {}", session.role->memory_dir);
    }
}

std::vector<SystemSchema> AgentSessionManager::BuildSystemPrompt(const AgentSession& session) {
    std::ostringstream prompt;

    // 1. Role Memory (长期记忆 - 习惯/偏好) - 从 AgentRole 封装方法加载
    if (session.role) {
        std::string memory_content = session.role->LoadMemoryContent();
        if (!memory_content.empty()) {
            prompt << memory_content;
        }
    }

    // 2. Session History (项目上下文 - 决策/待办)
    if (!session.session_history_dir.empty() &&
        std::filesystem::exists(session.session_history_dir)) {
        prompt << "## 项目上下文\n\n";

        // 加载 Session History 文件
        for (const auto& entry : std::filesystem::directory_iterator(session.session_history_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                auto content = ReadFile(entry.path().string());
                if (content.has_value()) {
                    prompt << "### " << entry.path().stem().string() << "\n";
                    prompt << content.value() << "\n\n";
                }
            }
        }
    }

    // 3. Role 基础 Prompt（System Prompt + Personality）
    if (session.role) {
        std::string role_prompt = session.role->BuildPrompt();
        if (!role_prompt.empty()) {
            prompt << role_prompt << "\n";
        }
    }

    return {{"text", prompt.str(), false}};
}

}  // namespace aicode

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "agent_engine.h"

#include <iostream>
#include <filesystem>

#include "common/constants.h"
#include "common/log_wrapper.h"
#include "common/string_utils.h"
#include "common/file_utils.h"
#include "core/memory_manager.h"
#include "core/session_key.h"
#include "managers/agent_session_manager.h"
#include "managers/agent_role_loader.h"
#include "managers/active_trigger_manager.h"
#include "command_registry.h"
#include "tools/tool_registry.h"
#include "providers/llm_provider_router.h"
#include "services/lsp_manager.h"

namespace prosophor {

AgentEngine& AgentEngine::GetInstance() {
    static AgentEngine instance;
    return instance;
}

AgentEngine::AgentEngine()
    : workspace_path_(std::filesystem::current_path().string()) {
    InitializeComponents();
}

void AgentEngine::SetUseDefaultTools(bool enabled) {
    if (tool_registry_ != nullptr) tool_registry_->SetUseDefaultTools(enabled);
}

void AgentEngine::ReloadRoles() {
    if (session_manager_ != nullptr) session_manager_->ReloadRoles();
}

std::string AgentEngine::GetRoleProviderName(const std::string& role_id) const {
    return session_manager_ != nullptr
               ? session_manager_->GetRoleProviderName(role_id)
               : std::string();
}

AgentEngine::~AgentEngine() {
    // 先停主动调度线程，再收起它创建的常驻会话（避免与调度线程并发）。
    ActiveTriggerManager::GetInstance().Stop();

    if (session_manager_) {
        std::lock_guard<std::mutex> lock(active_sessions_mutex_);
        for (const auto& [key, session_id] : active_session_ids_) {
            try {
                session_manager_->CloseSession(session_id);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to close active session {}: {}", session_id, e.what());
            } catch (...) {
                LOG_ERROR("Failed to close active session {}", session_id);
            }
        }
        active_session_ids_.clear();
    }

    if (memory_manager_) {
        memory_manager_->StopFileWatcher();
    }
}

void AgentEngine::InitializeComponents() {
    LOG_DEBUG("Initializing AgentEngine components...");

    config_ = prosophor::ProsophorConfig::GetInstance();

    EnsureDirectory(workspace_path_);

    memory_manager_ = std::make_shared<MemoryManager>(workspace_path_);
    memory_manager_->LoadWorkspaceFiles();
    memory_manager_->StartFileWatcher();

    tool_registry_ = &ToolRegistry::GetInstance();
    tool_registry_->SetWorkspace(workspace_path_);

    session_manager_ = &AgentSessionManager::GetInstance();

    ToolExecutorCallback tool_executor =
        [this](const std::string& tool_name, const nlohmann::json& args) -> std::string {
            return tool_registry_->ExecuteTool(tool_name, args);
        };

    session_manager_->Initialize(tool_executor);

    provider_router_ = &LlmProviderRouter::GetInstance();
    provider_router_->Initialize(config_);
    if (!config_.default_role.empty()) {
        std::string role_provider =
            session_manager_->GetRoleProviderName(config_.default_role[0]);
        if (!role_provider.empty()) {
            provider_router_->SetDefaultProvider(role_provider);
        }
    }

    auto& lsp_manager = prosophor::LspManager::GetInstance();
    lsp_manager.Initialize();
    LOG_DEBUG("LSP integration initialized with {} servers",
              lsp_manager.GetRegisteredServers().size());

    command_registry_ = &CommandRegistry::GetInstance();
    command_registry_->Initialize();

    // ── 主动触发管理器 ──────────────────────────────────────────
    auto& active_trigger = ActiveTriggerManager::GetInstance();
    active_trigger.Initialize("~/.prosophor/active");

    active_trigger.SetLlmExecuteCallback(
        [this](const ActiveTriggerRequest& request) {
            RunActiveTurn(request.plugin, request.role,
                          request.reason, request.prompt_md);
        });

    // 常驻主动会话不能算作"用户活跃"，否则 idle 插件永远等不到空闲。
    active_trigger.SetUserInteractionCallback([this]() -> bool {
        for (auto* session : session_manager_->GetActiveSessions(5)) {
            if (!IsActiveOwner(session->GetOwnerId())) return true;
        }
        return false;
    });

    active_trigger.Start();

    LOG_DEBUG("ActiveTriggerManager initialized and started");
    LOG_DEBUG("AgentEngine initialized");
}

void AgentEngine::SetOutputCallback(OutputCallback cb) {
    session_manager_->SetOutputCallback(std::move(cb));
}

void AgentEngine::SetPermissionCallback(PermissionCallback cb) {
    tool_registry_->SetPermissionConfirmCallback(std::move(cb));
}

void AgentEngine::AddOutputCallback(OutputCallback cb) {
    session_manager_->AddOutputCallback(std::move(cb));
}

void AgentEngine::SetProactiveMessageCallback(ProactiveMessageCallback cb) {
    std::lock_guard<std::mutex> lock(active_sessions_mutex_);
    proactive_callback_ = std::move(cb);
}

namespace {

/// 主动触发角色：插件覆盖 > active_trigger.role > default_role[0] > "default"。
std::string ResolveActiveRole(const std::string& plugin_role,
                              const ProsophorConfig& config) {
    if (!plugin_role.empty()) return plugin_role;
    if (!config.active_trigger.role.empty()) return config.active_trigger.role;
    if (!config.default_role.empty()) return config.default_role[0];
    return "default";
}

}  // namespace

void AgentEngine::RunActiveTurn(const std::string& plugin, const std::string& role_id,
                                const std::string& reason, const std::string& prompt_md) {
    // 用实时配置解析，跟随 /role 修改 default_role 后的当前主角色
    const std::string role = ResolveActiveRole(role_id, ProsophorConfig::GetInstance());
    const std::string session_key = plugin + "|" + role;

    std::string session_id;
    {
        std::lock_guard<std::mutex> lock(active_sessions_mutex_);
        auto it = active_session_ids_.find(session_key);
        if (it != active_session_ids_.end()) {
            session_id = it->second;
        }
        if (session_id.empty() || session_manager_->GetSession(session_id) == nullptr) {
            try {
                session_id = CreateSession(role, "proactive: " + plugin,
                                           kActiveOwner, SessionType::kDirect);
                active_session_ids_[session_key] = session_id;
                LOG_INFO("[active] created session {} for plugin '{}' (role={})",
                         session_id, plugin, role);
            } catch (const std::exception& e) {
                LOG_ERROR("[active] create session failed (plugin={}, role={}): {}",
                          plugin, role, e.what());
                return;
            }
        }
    }

    std::string message = "触发事件：" + reason;
    if (!prompt_md.empty()) {
        message += "\n\n" + prompt_md;
    }

    // 主动会话不向前端 fanout：Loop 在调度线程上，前端回调不是线程安全的。
    if (auto* active = session_manager_->GetSession(session_id)) {
        active->output_callback_ = nullptr;
    }

    try {
        session_manager_->SendToSession(session_id, message);
    } catch (const std::exception& e) {
        LOG_ERROR("[active] turn failed (plugin={}, session={}): {}",
                  plugin, session_id, e.what());
        return;
    }

    // 结果通过会话状态取：失败轮次不读消息（末条可能退回上一轮）。
    // active session 只有本调度线程写（单写者），发送后读快照不会被下一轮覆盖；
    // 若未来允许并发触发/用户直接写 active session，需改为锁内取结果。
    std::string reply;
    if (auto snap = GetSessionSnapshot(session_id)) {
        if (snap->state != AgentRuntimeState::STATE_ERROR &&
            !snap->messages.empty() && snap->messages.back().role == "assistant") {
            reply = snap->messages.back().reply_text();
        } else if (snap->state == AgentRuntimeState::STATE_ERROR) {
            LOG_WARN("[active] turn error (plugin={}, role={}): {}",
                     plugin, role, snap->state_message);
        }
    }

    FlushSession(session_id);

    ProactiveMessageCallback callback;
    {
        std::lock_guard<std::mutex> lock(active_sessions_mutex_);
        callback = proactive_callback_;
    }
    if (reply.empty()) {
        LOG_WARN("[active] empty reply (plugin={}, role={})", plugin, role);
    } else if (callback) {
        callback(plugin, role, session_id, reply);
    } else {
        LOG_INFO("[active] {} ({}): {}", plugin, role, reply);
    }
}

void AgentEngine::SendUserMessage(const std::string& session_id, const std::string& text,
                                  const std::string& sender_id,
                                  const std::string& sender_name) {
    if (!text.empty() && text[0] == '/') {
        // 命令是系统级操作,不携带 sender; 其结果不是一条助手回复
        HandleCommand(text, session_id);
        return;
    }
    try {
        session_manager_->SendToSession(session_id, text, sender_id, sender_name);
    } catch (const std::exception& e) {
        LOG_ERROR("SendUserMessage error (session={}): {}", session_id, e.what());
    }
}

void AgentEngine::SendUserMessage(const std::string& session_id, const std::string& text,
                                  const std::vector<std::string>& image_paths,
                                  const std::string& sender_id,
                                  const std::string& sender_name) {
    if (!text.empty() && text[0] == '/') {
        HandleCommand(text, session_id);
        return;
    }
    try {
        session_manager_->SendToSession(session_id, text, image_paths,
                                        sender_id, sender_name);
    } catch (const std::exception& e) {
        LOG_ERROR("SendUserMessage error (session={}): {}", session_id, e.what());
    }
}

void AgentEngine::SetClearImagesAfterTurn(bool enabled) {
    if (session_manager_) session_manager_->SetClearImagesAfterTurn(enabled);
}

bool AgentEngine::GetSessionStateless(const std::string& session_id) const {
    auto* session = session_manager_->GetSession(session_id);
    return session != nullptr && session->IsStateless();
}

void AgentEngine::SetSessionStateless(const std::string& session_id, bool enabled) {
    auto* session = session_manager_->GetSession(session_id);
    if (session) session->SetStateless(enabled);
}

void AgentEngine::SendUserMessageAsync(const std::string& session_id, const std::string& text,
                                  const std::string& sender_id,
                                  const std::string& sender_name) {
    if (!text.empty() && text[0] == '/') {
        // 命令是系统级操作,不携带 sender
        HandleCommand(text, session_id);
        return;
    }
    try {
        session_manager_->SendToSessionAsync(session_id, text, sender_id, sender_name);
    } catch (const std::exception& e) {
        LOG_ERROR("SendToSessionAsync error (session={}): {}", session_id, e.what());
    }
}

bool AgentEngine::HandleCommand(const std::string& line, const std::string& session_id) {
    if (line.empty() || line[0] != '/') {
        return false;
    }

    std::vector<std::string> args = CommandRegistry::ParseCommandLine(line);
    if (args.empty()) {
        return false;
    }

    std::string cmd_name = args[0].substr(1);
    std::vector<std::string> cmd_args(args.begin() + 1, args.end());

    if (cmd_name == "role" && !cmd_args.empty()) {
        SwitchRole(session_id, cmd_args[0]);
        return true;
    }

    CommandContext ctx;
    ctx.workspace   = workspace_path_;
    ctx.session_id  = session_id;
    ctx.user_data   = this;
    ctx.agent_session = session_manager_->GetSession(session_id);

    std::cout << ColorCode::kGreen << "/" << cmd_name;
    for (const auto& a : cmd_args) std::cout << " " << a;
    std::cout << ColorCode::kReset << std::endl;

    auto result = CommandRegistry::GetInstance().ExecuteCommand(cmd_name, cmd_args, ctx);
    if (!result.output.empty()) {
        std::cout << ColorCode::kGray << result.output << ColorCode::kReset << std::endl;
    } else if (!result.success) {
        std::cout << ColorCode::kRed << result.error << ColorCode::kReset << "\n";
    }
    return true;
}

std::string AgentEngine::CreateSession(const std::string& role_id,
                                        const std::string& task_desc,
                                        const std::string& owner_id,
                                        SessionType session_type) {
    return session_manager_->CreateSession(role_id, task_desc, owner_id, session_type);
}

std::optional<RenderSnapshot> AgentEngine::GetFocusedSessionSnapshot() {
    return GetSessionSnapshot(session_manager_->GetLastSessionId());
}

std::optional<RenderSnapshot> AgentEngine::GetSessionSnapshot(const std::string& session_id) {
    auto* session = session_manager_->GetSession(session_id);
    if (!session) return std::nullopt;
    return session->GetSnapshot();
}

void AgentEngine::StopSession(const std::string& session_id) {
    auto* session = session_manager_->GetSession(session_id);
    if (session) {
        session->RequestStop();
    }
}

void AgentEngine::StopActiveTriggers() {
    ActiveTriggerManager::GetInstance().Stop();
}

void AgentEngine::FlushSession(const std::string& session_id) {
    session_manager_->FlushSession(session_id);
}

void AgentEngine::ChangeWorkspace(const std::string& new_path) {
    if (!DirExists(new_path)) {
        LOG_ERROR("Workspace path does not exist: {}", new_path);
        return;
    }

    if (memory_manager_) {
        memory_manager_->StopFileWatcher();
    }

    workspace_path_ = new_path;
    EnsureDirectory(workspace_path_);

    // Re-create memory manager with new path
    memory_manager_ = std::make_shared<MemoryManager>(workspace_path_);
    memory_manager_->LoadWorkspaceFiles();
    memory_manager_->StartFileWatcher();

    tool_registry_->SetWorkspace(workspace_path_);

    LOG_INFO("Workspace changed to: {}", workspace_path_);
}

void AgentEngine::SwitchRole(const std::string& session_id, const std::string& new_role_id) {
    session_manager_->SwitchRoleForSession(session_id, new_role_id);
}

std::vector<std::string> AgentEngine::ListRoles() const {
    return session_manager_->ListRoles();
}

std::vector<std::string> AgentEngine::ListSessions() const {
    return session_manager_->ListSessions();
}

std::optional<RoleTtsConfig> AgentEngine::GetRoleTtsConfig(const std::string& role_id) const {
    auto tts = session_manager_->GetRoleTtsConfig(role_id);
    if (!tts) return std::nullopt;
    return RoleTtsConfig{tts->first, tts->second};
}

void AgentEngine::RegisterTool(
    const std::string& name, const std::string& description,
    nlohmann::json parameters,
    std::function<std::string(const nlohmann::json&)> executor) {
    tool_registry_->RegisterTool(name, description, std::move(parameters),
                                 std::move(executor));
}

bool AgentEngine::GetSessionUseTools(const std::string& session_id) const {
    auto* session = session_manager_->GetSession(session_id);
    return session != nullptr && session->GetUseTools();
}

void AgentEngine::SetSessionUseTools(const std::string& session_id, bool enabled) {
    auto* session = session_manager_->GetSession(session_id);
    if (session) session->SetUseTools(enabled);
}

ProviderStatus AgentEngine::GetProviderStatus(const std::string& provider_name) const {
    ProviderStatus status;
    auto& config = ProsophorConfig::GetInstance();
    if (config.llm_providers.find(provider_name) == config.llm_providers.end()) {
        status.error = "provider not configured: " + provider_name;
        return status;
    }

    auto provider = provider_router_->GetProviderByNameExact(provider_name);
    if (!provider) {
        status.error = "provider not available: " + provider_name;
        return status;
    }

    status.loaded = provider->IsLoaded();
    status.loading = provider->IsLoading();
    status.has_vision = provider->HasVisionConfig();
    status.error = provider->GetLoadError();
    return status;
}

std::vector<ActiveTriggerInfo> AgentEngine::ListActiveTriggerPlugins() const {
    std::vector<ActiveTriggerInfo> result;
    for (const auto& plugin : ActiveTriggerManager::GetInstance().ListPlugins()) {
        ActiveTriggerInfo info;
        info.name = plugin.name;
        info.role = plugin.role;
        info.mode = plugin.mode;
        info.interval = plugin.interval;
        info.threshold = plugin.threshold;
        info.cooldown = plugin.cooldown;
        info.priority = plugin.priority;
        info.enabled = plugin.enabled;
        result.push_back(std::move(info));
    }
    return result;
}

}  // namespace prosophor

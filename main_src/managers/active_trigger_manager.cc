// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#include "managers/active_trigger_manager.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "common/file_utils.h"
#include "common/subprocess_wrapper.h"

namespace prosophor {

ActiveTriggerManager& ActiveTriggerManager::GetInstance() {
    static ActiveTriggerManager instance;
    return instance;
}

void ActiveTriggerManager::Initialize(const std::string& active_dir) {
    active_dir_ = ExpandHome(active_dir);

    // 确保目录存在
    if (!std::filesystem::exists(active_dir_)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(active_dir_, ec)) {
            LOG_WARN("Failed to create active plugins directory: {}", ec.message());
        } else {
            LOG_INFO("Created active plugins directory: {}", active_dir_);
        }
    }

    LOG_DEBUG("ActiveTriggerManager initialized: {}", active_dir_);
    ScanAndLoadPlugins();
}

void ActiveTriggerManager::Start() {
    if (running_) {
        LOG_WARN("ActiveTriggerManager already running");
        return;
    }

    running_ = true;
    scheduler_thread_ = std::thread(&ActiveTriggerManager::SchedulerLoop, this);
    LOG_DEBUG("ActiveTriggerManager scheduler started");
}

void ActiveTriggerManager::Stop() {
    running_ = false;
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
    LOG_INFO("ActiveTriggerManager scheduler stopped");
}

void ActiveTriggerManager::SetLlmExecuteCallback(LlmExecuteCallback cb) {
    llm_callback_ = std::move(cb);
}

void ActiveTriggerManager::SetUserInteractionCallback(UserInteractionCallback cb) {
    user_interaction_callback_ = std::move(cb);
}

void ActiveTriggerManager::SetSessionManager(AgentSessionManager* session_manager) {
    session_manager_ = session_manager;
}

std::string ActiveTriggerManager::ExpandHome(const std::string& path) {
    if (path.find("~") == 0) {
        const char* home = getenv("USERPROFILE");
        if (!home) home = getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

void ActiveTriggerManager::ScanAndLoadPlugins() {
    std::lock_guard<std::mutex> lock(mutex_);

    periodic_plugins_.clear();
    idle_plugins_.clear();

    if (!std::filesystem::exists(active_dir_)) {
        LOG_INFO("Active plugins directory does not exist: {}", active_dir_);
        return;
    }

    int loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(active_dir_)) {
        if (entry.is_directory()) {
            if (LoadPlugin(entry.path().string())) {
                loaded++;
            }
        }
    }

    // 按优先级排序（数字越小优先级越高）
    std::sort(periodic_plugins_.begin(), periodic_plugins_.end(),
              [](const auto& a, const auto& b) { return a.priority < b.priority; });
    std::sort(idle_plugins_.begin(), idle_plugins_.end(),
              [](const auto& a, const auto& b) { return a.priority < b.priority; });

    LOG_DEBUG("Loaded active plugins ({} periodic, {} idle)",
             loaded, periodic_plugins_.size(), idle_plugins_.size());
}

bool ActiveTriggerManager::LoadPlugin(const std::string& plugin_dir) {
    std::string config_path = plugin_dir + "/trigger_mode.cfg";
    if (!std::filesystem::exists(config_path)) {
        LOG_DEBUG("No trigger_mode.cfg found in {}", plugin_dir);
        return false;
    }

    ActiveTriggerPlugin plugin;
    plugin.path = plugin_dir;
    plugin.name = std::filesystem::path(plugin_dir).filename().string();
    plugin.script = "trigger";  // 默认值

    if (!ParseTriggerModeConfig(config_path, plugin)) {
        LOG_ERROR("Failed to parse trigger_mode.cfg for {}", plugin.name);
        return false;
    }

    // 使用配置的 script 字段构建路径
    std::string trigger_path = plugin_dir + "/" + plugin.script;

    // 如果配置的文件不存在，尝试自动检测
    if (!std::filesystem::exists(trigger_path)) {
        // 尝试常见的脚本文件名
        std::vector<std::string> candidates = {
            "trigger", "trigger.bat", "trigger.exe", "trigger.py",
            "main.py", "main.sh", "main.bat"
        };

        bool found = false;
        for (const auto& candidate : candidates) {
            std::string candidate_path = plugin_dir + "/" + candidate;
            if (std::filesystem::exists(candidate_path)) {
                trigger_path = candidate_path;
                plugin.script = candidate;
                found = true;
                LOG_INFO("Auto-detected trigger script: {}", candidate);
                break;
            }
        }

        if (!found) {
            LOG_ERROR("No trigger script found in {} (tried: {}, trigger.bat, trigger.exe, trigger.py, main.py, etc.)",
                     plugin_dir, plugin.script);
            return false;
        }
    }

    if (plugin.mode == "periodic") {
        periodic_plugins_.push_back(plugin);
        LOG_DEBUG("Loaded periodic plugin: {} (interval={}s, priority={})",
                 plugin.name, plugin.interval, plugin.priority);
    } else if (plugin.mode == "idle") {
        idle_plugins_.push_back(plugin);
        LOG_DEBUG("Loaded idle plugin: {} (threshold={}s, priority={})",
                 plugin.name, plugin.threshold, plugin.priority);
    }

    return true;
}

bool ActiveTriggerManager::ParseTriggerModeConfig(const std::string& config_path,
                                                   ActiveTriggerPlugin& plugin) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // 跳过注释和空行
        if (line.empty() || line[0] == '#') continue;

        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // 去除空白
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        if (key == "mode") {
            plugin.mode = value;
        } else if (key == "interval") {
            plugin.interval = std::stoi(value);
        } else if (key == "threshold") {
            plugin.threshold = std::stoi(value);
        } else if (key == "priority") {
            plugin.priority = std::stoi(value);
        } else if (key == "timeout") {
            plugin.timeout = std::stoi(value);
        } else if (key == "script") {
            plugin.script = value;
        }
    }

    // 默认脚本文件名为 "trigger"
    if (plugin.script.empty()) {
        plugin.script = "trigger";
    }

    return !plugin.mode.empty();
}

std::string ActiveTriggerManager::ReadPromptMd(const std::string& plugin_path) const {
    std::string prompt_path = plugin_path + "/ACTIVE.md";
    if (!std::filesystem::exists(prompt_path)) {
        LOG_WARN("ACTIVE.md not found in {}", plugin_path);
        return "";
    }

    auto content = ReadFile(prompt_path);
    return content.value_or("");
}

void ActiveTriggerManager::SchedulerLoop() {
    auto now_sec = []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    };

    // 空闲状态机状态
    enum class IdleState { Monitoring, Pending, Triggering };
    IdleState idle_state = IdleState::Monitoring;
    int64_t last_interaction_time = now_sec();

    LOG_DEBUG("ActiveTriggerManager scheduler loop started");

    while (running_) {
        try {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            int64_t current_time = now_sec();

            // 复制插件列表以避免锁竞争
            std::vector<ActiveTriggerPlugin> periodic_plugins_copy;
            std::vector<ActiveTriggerPlugin> idle_plugins_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                periodic_plugins_copy = periodic_plugins_;
                idle_plugins_copy = idle_plugins_;
            }

            // 1. 执行周期性插件（优先级高，不受用户状态影响）
            for (auto& plugin : periodic_plugins_copy) {
                if (!plugin.enabled) continue;
                if (current_time - plugin.last_run >= plugin.interval) {
                    std::string trigger_reason;
                    if (ExecuteTrigger(plugin, trigger_reason)) {
                        std::string prompt_md = ReadPromptMd(plugin.path);
                        std::string session_id = GenerateSessionId();
                        try {
                            ExecuteLlmCallback(session_id, trigger_reason, prompt_md);
                        } catch (const std::exception& e) {
                            LOG_ERROR("LLM callback exception for periodic plugin {}: {}", plugin.name, e.what());
                        }
                    }
                    // 更新原插件的 last_run
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (auto& p : periodic_plugins_) {
                        if (p.name == plugin.name && p.path == plugin.path) {
                            p.last_run = current_time;
                            break;
                        }
                    }
                }
            }

            // 2. 检测用户交互状态（更新最后交互时间）
            bool is_user_active = false;
            if (user_interaction_callback_) {
                try {
                    is_user_active = user_interaction_callback_();
                } catch (const std::exception& e) {
                    LOG_ERROR("User interaction callback exception: {}", e.what());
                }
            }
            if (is_user_active) {
                last_interaction_time = current_time;
                idle_state = IdleState::Monitoring;
            }

            // 3. 空闲状态机：Monitoring → Pending → Triggering
            int64_t idle_duration = current_time - last_interaction_time;
            int64_t max_threshold = 0;
            for (const auto& p : idle_plugins_copy) {
                if (p.enabled && p.threshold > max_threshold) max_threshold = p.threshold;
            }

            if (idle_duration >= max_threshold && max_threshold > 0) {
                idle_state = IdleState::Pending;
            }

            // 4. 执行空闲插件（包括 idle 和 idle_once 模式）
            if (idle_state != IdleState::Monitoring) {
                for (auto& plugin : idle_plugins_copy) {
                    if (!plugin.enabled) continue;

                    // idle_once 模式：已触发过则跳过
                    if (plugin.mode == "idle_once" && plugin.triggered_count > 0) {
                        continue;
                    }

                    // 检查是否达到阈值
                    if (idle_duration >= plugin.threshold) {
                        std::string trigger_reason;
                        if (ExecuteTrigger(plugin, trigger_reason)) {
                            std::string prompt_md = ReadPromptMd(plugin.path);
                            std::string session_id = GenerateSessionId();
                            try {
                                ExecuteLlmCallback(session_id, trigger_reason, prompt_md);
                            } catch (const std::exception& e) {
                                LOG_ERROR("LLM callback exception for idle plugin {}: {}", plugin.name, e.what());
                            }

                            // idle_once 模式：触发成功后 auto-disable
                            if (plugin.mode == "idle_once") {
                                std::lock_guard<std::mutex> lock(mutex_);
                                for (auto& p : idle_plugins_) {
                                    if (p.name == plugin.name && p.path == plugin.path) {
                                        p.triggered_count++;
                                        p.enabled = false;
                                        break;
                                    }
                                }
                                LOG_INFO("idle_once plugin {} triggered once, auto-disabled", plugin.name);
                            }
                        }
                        // 更新原插件的 last_run
                        std::lock_guard<std::mutex> lock(mutex_);
                        for (auto& p : idle_plugins_) {
                            if (p.name == plugin.name && p.path == plugin.path) {
                                p.last_run = current_time;
                                break;
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("SchedulerLoop exception: {}", e.what());
        } catch (...) {
            LOG_ERROR("SchedulerLoop unknown exception");
        }
    }

    LOG_INFO("ActiveTriggerManager scheduler loop stopped");
}

bool ActiveTriggerManager::ExecuteTrigger(const ActiveTriggerPlugin& plugin,
                                           std::string& trigger_reason) {
    std::string trigger_path = plugin.path + "/" + plugin.script;

    auto result = ExecuteScriptWithTimeout(trigger_path, plugin.timeout);

    if (result.timeout) {
        LOG_WARN("Trigger script timeout: {}", plugin.name);
        return false;
    }

    if (result.return_code == 0) {
        return false;  // 返回 0 = 不触发
    }

    // 返回非 0 = 触发，output 作为触发原因
    trigger_reason = result.output;
    if (trigger_reason.empty()) {
        trigger_reason = "Plugin " + plugin.name + " triggered";
    }

    LOG_DEBUG("Trigger executed: {} - {}", plugin.name, trigger_reason);
    return true;
}

std::string ActiveTriggerManager::ExecuteLlmCallback(const std::string& session_id,
                                                      const std::string& trigger_reason,
                                                      const std::string& prompt_md) {
    if (!llm_callback_) {
        LOG_ERROR("LLM callback not set");
        return "";
    }

    try {
        return llm_callback_(session_id, trigger_reason, prompt_md);
    } catch (const std::exception& e) {
        LOG_ERROR("LLM callback failed: {}", e.what());
        return "";
    }
}

std::string ActiveTriggerManager::GenerateSessionId() const {
    return "active-" + std::to_string(SystemClock::GetCurrentTimeMillis());
}

void ActiveTriggerManager::ReloadPlugins() {
    std::lock_guard<std::mutex> lock(mutex_);
    periodic_plugins_.clear();
    idle_plugins_.clear();
    ScanAndLoadPlugins();
}

std::vector<ActiveTriggerPlugin> ActiveTriggerManager::ListPlugins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ActiveTriggerPlugin> result;
    result.insert(result.end(), periodic_plugins_.begin(), periodic_plugins_.end());
    result.insert(result.end(), idle_plugins_.begin(), idle_plugins_.end());
    return result;
}

ActiveTriggerPlugin ActiveTriggerPlugin::FromJson(const nlohmann::json& json,
                                                   const std::string& path) {
    ActiveTriggerPlugin plugin;
    plugin.path = path;
    plugin.name = json.value("name", "");
    plugin.mode = json.value("mode", "");
    plugin.interval = json.value("interval", 0);
    plugin.threshold = json.value("threshold", 0);
    plugin.priority = json.value("priority", 50);
    plugin.timeout = json.value("timeout", 100);
    plugin.enabled = json.value("enabled", true);
    return plugin;
}

ActiveTriggerManager::~ActiveTriggerManager() {
    Stop();
}

}  // namespace prosophor

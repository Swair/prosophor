// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#include "managers/active_interaction_manager.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "common/file_utils.h"

namespace prosophor {

namespace {
// 延迟时间（秒）
constexpr int RECOMMEND_DELAY_SEC = 5 * 60;      // 5 分钟
constexpr int SUMMARY_DELAY_SEC = 10 * 60;       // 10 分钟
}  // namespace

ActiveInteractionManager& ActiveInteractionManager::GetInstance() {
    static ActiveInteractionManager instance;
    return instance;
}

void ActiveInteractionManager::Initialize(AgentSessionManager* session_manager) {
    session_manager_ = session_manager;
    running_ = true;
    scheduler_thread_ = std::thread(&ActiveInteractionManager::SchedulerLoop, this);
    LOG_DEBUG("ActiveInteractionManager initialized");
}

std::string ActiveInteractionManager::GetChangelogDir() {
    auto home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (!home) return "";

    std::string dir = std::string(home) + "/.prosophor/changelog";
    EnsureChangelogDir();
    return dir;
}

bool ActiveInteractionManager::EnsureChangelogDir() {
    auto home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (!home) return false;

    std::string dir = std::string(home) + "/.prosophor/changelog";
    try {
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        return true;
    } catch (...) {
        LOG_ERROR("Failed to create changelog dir: {}", dir);
        return false;
    }
}

void ActiveInteractionManager::OnUserMessage(const std::string& session_id, const std::string& message) {
    if (!running_) return;

    // 忽略空消息或系统消息
    if (message.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // 取消该会话之前的所有待处理任务（避免重复）
    auto it = session_tasks_.find(session_id);
    if (it != session_tasks_.end()) {
        for (const auto& task_id : it->second) {
            auto task_it = all_tasks_.find(task_id);
            if (task_it != all_tasks_.end() && !task_it->second.executed) {
                LOG_DEBUG("Cancel pending task {} for session {}", task_id, session_id);
                task_it->second.executed = true;  // 标记为已取消
            }
        }
    }

    // 调度新任务：5 分钟推荐 + 10 分钟总结
    ScheduleTask(session_id, ActiveInteractionType::RECOMMEND_QUESTION, RECOMMEND_DELAY_SEC);
    ScheduleTask(session_id, ActiveInteractionType::SESSION_SUMMARY, SUMMARY_DELAY_SEC);

    LOG_DEBUG("Scheduled active interaction tasks for session {}", session_id);
}

void ActiveInteractionManager::ScheduleTask(const std::string& session_id, ActiveInteractionType type, int delay_seconds) {
    ActiveInteractionTask task;
    task.id = GenerateTaskId();
    task.session_id = session_id;
    task.type = type;
    task.created_at = std::time(nullptr);
    task.scheduled_at = task.created_at + delay_seconds;
    task.executed = false;

    all_tasks_[task.id] = task;
    session_tasks_[session_id].push_back(task.id);

    std::string type_str = (type == ActiveInteractionType::RECOMMEND_QUESTION) ? "recommend" : "summary";
    LOG_DEBUG("Scheduled {} task {} for session {} (delay={}s)", type_str, task.id, session_id, delay_seconds);
}

void ActiveInteractionManager::CancelSessionTasks(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = session_tasks_.find(session_id);
    if (it == session_tasks_.end()) return;

    for (const auto& task_id : it->second) {
        auto task_it = all_tasks_.find(task_id);
        if (task_it != all_tasks_.end() && !task_it->second.executed) {
            task_it->second.executed = true;
            LOG_INFO("Cancelled task {} for session {}", task_id, session_id);
        }
    }
    session_tasks_.erase(it);
}

std::vector<ActiveInteractionTask> ActiveInteractionManager::ListTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ActiveInteractionTask> result;
    for (const auto& [id, task] : all_tasks_) {
        result.push_back(task);
    }
    return result;
}

void ActiveInteractionManager::SchedulerLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(10));  // 每 10 秒检查一次

        auto now = std::time(nullptr);

        std::vector<std::string> tasks_to_execute;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, task] : all_tasks_) {
                if (!task.executed && now >= task.scheduled_at) {
                    tasks_to_execute.push_back(id);
                }
            }
        }

        for (const auto& task_id : tasks_to_execute) {
            ActiveInteractionTask* task_ptr = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = all_tasks_.find(task_id);
                if (it != all_tasks_.end() && !it->second.executed) {
                    it->second.executed = true;
                    task_ptr = &it->second;
                }
            }

            if (task_ptr) {
                LOG_INFO("Executing active interaction task {} (type={})",
                         task_ptr->id,
                         task_ptr->type == ActiveInteractionType::RECOMMEND_QUESTION ? "recommend" : "summary");
                ExecuteTask(*task_ptr);
            }
        }
    }
}

void ActiveInteractionManager::ExecuteTask(ActiveInteractionTask& task) {
    if (!session_manager_) {
        LOG_ERROR("SessionManager not initialized");
        return;
    }

    auto* session = session_manager_->GetSession(task.session_id);
    if (!session || !session->is_active) {
        LOG_INFO("Session {} no longer active, skipping task {}", task.session_id, task.id);
        return;
    }

    std::string result;
    if (task.type == ActiveInteractionType::RECOMMEND_QUESTION) {
        result = ExecuteRecommendQuestion(task.session_id);
    } else {
        result = ExecuteSessionSummary(task.session_id);
    }

    task.result = result;

    // 将结果发送到会话（作为 AI 主动发送的消息）
    if (!result.empty() && session_manager_) {
        if (task.type == ActiveInteractionType::RECOMMEND_QUESTION) {
            // 推荐问题：直接发送到会话
            session_manager_->SendToSessionAsync(task.session_id,
                "[主动推荐] " + result);
        }
        // 总结不发送到会话，已保存到文件
    }
}

std::string ActiveInteractionManager::ExecuteRecommendQuestion(const std::string& session_id) {
    if (!llm_execute_callback_) {
        LOG_ERROR("LLM execute callback not set");
        return "";
    }

    std::string prompt = "根据用户之前的对话内容，你有什么想和用户说的吗？有什么建议或想推荐的吗？请提出 1-3 个有深度的问题或建议，帮助用户继续深入探索。";

    try {
        return llm_execute_callback_(session_id, prompt);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to execute recommend question: {}", e.what());
        return "";
    }
}

std::string ActiveInteractionManager::ExecuteSessionSummary(const std::string& session_id) {
    if (!llm_execute_callback_) {
        LOG_ERROR("LLM execute callback not set");
        return "";
    }

    std::string prompt = "根据用户本次会话的内容，请总结关键点和决策，按以下格式输出：\n\n## 讨论主题\n[一句话概括]\n\n## 关键发现\n- 要点 1\n- 要点 2\n\n## 达成的结论\n- 结论 1\n- 结论 2\n\n## 后续行动\n- [ ] 待办 1\n- [ ] 待办 2";

    try {
        std::string summary = llm_execute_callback_(session_id, prompt);

        // 保存到 changelog 目录
        if (!summary.empty()) {
            auto now_time = std::time(nullptr);
            std::tm tm = *std::localtime(&now_time);
            char timestamp[64];
            std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);

            // 从总结中提取标题（第一行）
            std::istringstream iss(summary);
            std::string title_line;
            std::getline(iss, title_line);

            // 清理标题作为文件名
            std::string safe_title;
            for (char c : title_line) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == ' ') {
                    safe_title += c;
                }
            }
            if (safe_title.length() > 50) safe_title = safe_title.substr(0, 50);

            std::string filename = safe_title + "_" + timestamp + ".md";
            SaveToChangelog(filename, summary);

            LOG_INFO("Saved session summary to changelog: {}", filename);
        }

        return summary;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to execute session summary: {}", e.what());
        return "";
    }
}

bool ActiveInteractionManager::SaveToChangelog(const std::string& filename, const std::string& content) {
    auto changelog_dir = GetChangelogDir();
    if (changelog_dir.empty()) return false;

    std::string filepath = changelog_dir + "/" + filename;
    return prosophor::WriteFile(filepath, content);
}

std::string ActiveInteractionManager::GenerateTaskId() const {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::stringstream ss;
    ss << "active-" << now;
    return ss.str();
}

ActiveInteractionManager::~ActiveInteractionManager() {
    running_ = false;
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
}

}  // namespace prosophor

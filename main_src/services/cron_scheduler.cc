// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "services/cron_scheduler.h"

#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

#include "common/log_wrapper.h"

namespace aicode {

namespace fs = std::filesystem;

CronScheduler& CronScheduler::GetInstance() {
    static CronScheduler instance;
    return instance;
}

CronScheduler::~CronScheduler() {
    Shutdown();
}

void CronScheduler::Initialize() {
    if (running_) {
        return;
    }

    running_ = true;
    scheduler_thread_ = std::thread(&CronScheduler::SchedulerLoop, this);
    LOG_INFO("CronScheduler initialized and started");
}

void CronScheduler::Shutdown() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }

    // Save durable tasks
    SaveToFile();
    LOG_INFO("CronScheduler shut down");
}

std::string CronScheduler::GenerateTaskId() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return "cron_" + std::to_string(millis);
}

std::string CronScheduler::GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::string CronScheduler::Schedule(const std::string& cron,
                                     const std::string& prompt,
                                     bool recurring,
                                     bool durable) {
    std::lock_guard<std::mutex> lock(mutex_);

    ScheduledTask task;
    task.id = GenerateTaskId();
    task.cron_expression = cron;
    task.prompt = prompt;
    task.recurring = recurring;
    task.durable = durable;
    task.status = CronJobStatus::Active;
    task.created_at = GetTimestamp();
    task.next_execution_at = GetNextExecutionTime(cron);

    tasks_[task.id] = task;
    LOG_INFO("Scheduled task: {} - cron={}", task.id, cron);

    if (durable) {
        SaveToFile();
    }

    return task.id;
}

ScheduledTask CronScheduler::GetTask(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return it->second;
    }
    return ScheduledTask();
}

std::vector<ScheduledTask> CronScheduler::ListTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ScheduledTask> result;
    for (const auto& [id, task] : tasks_) {
        result.push_back(task);
    }
    return result;
}

bool CronScheduler::Pause(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }
    it->second.status = CronJobStatus::Paused;
    LOG_INFO("Paused task: {}", task_id);
    return true;
}

bool CronScheduler::Resume(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }
    it->second.status = CronJobStatus::Active;
    it->second.next_execution_at = GetNextExecutionTime(it->second.cron_expression);
    LOG_INFO("Resumed task: {}", task_id);
    return true;
}

bool CronScheduler::Delete(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }
    tasks_.erase(it);
    LOG_INFO("Deleted task: {}", task_id);

    SaveToFile();
    return true;
}

std::string CronScheduler::RunNow(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return "Task not found";
    }

    ExecuteTask(it->second);
    return it->second.last_result;
}

std::string CronScheduler::GetNextExecutionTime(const std::string& cron_expression) const {
    auto next_time = CalculateNextTime(cron_expression);
    auto time_t_next = std::chrono::system_clock::to_time_t(next_time);
    std::tm tm_next;
    localtime_r(&time_t_next, &tm_next);

    std::ostringstream oss;
    oss << std::put_time(&tm_next, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::chrono::system_clock::time_point CronScheduler::CalculateNextTime(const std::string& cron) const {
    // Parse cron expression: M H DoM Mon DoW
    std::istringstream iss(cron);
    std::string minute, hour, dom, month, dow;
    iss >> minute >> hour >> dom >> month >> dow;

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_r(&time_t_now, &tm);

    // Start from next minute
    tm.tm_sec = 0;
    tm.tm_min += 1;
    tm.tm_hour = 0;
    tm.tm_mday = 0;
    tm.tm_mon = 0;
    tm.tm_year = 0;

    // Simple implementation: just add 1 minute for now
    // A full implementation would parse all cron fields
    auto next = now + std::chrono::minutes(1);

    // Handle special expressions
    if (cron == "@hourly" || cron == "0 * * * *") {
        // Next hour
        next = now + std::chrono::hours(1);
        next = std::chrono::system_clock::from_time_t(
            std::chrono::system_clock::to_time_t(next) / 3600 * 3600);
    } else if (cron.find("*/") == 0) {
        // Handle */N expressions (e.g., */5 for every 5 minutes)
        int interval = std::stoi(cron.substr(2));
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(
            now.time_since_epoch()).count();
        auto next_multiple = ((minutes / interval) + 1) * interval;
        auto offset = next_multiple - minutes;
        next = now + std::chrono::minutes(offset);
    }

    return next;
}

bool CronScheduler::ShouldRunNow(const ScheduledTask& task) const {
    if (task.status != CronJobStatus::Active) {
        return false;
    }

    auto now = std::chrono::system_clock::now();
    auto next_time = CalculateNextTime(task.cron_expression);

    // Check if we've passed the next execution time
    return now >= next_time;
}

void CronScheduler::ExecuteTask(ScheduledTask& task) {
    if (!execute_callback_) {
        LOG_ERROR("No execute callback set for cron task");
        task.last_result = "Error: No execute callback configured";
        return;
    }

    LOG_INFO("Executing scheduled task {}: {}", task.id, task.prompt);

    task.execution_count++;
    task.last_executed_at = GetTimestamp();

    try {
        task.last_result = execute_callback_(task.prompt);
        LOG_INFO("Task {} completed: {}", task.id, task.last_result);
    } catch (const std::exception& e) {
        task.last_result = std::string("Error: ") + e.what();
        LOG_ERROR("Task {} failed: {}", task.id, task.last_result);
    }

    if (task.recurring) {
        task.next_execution_at = GetNextExecutionTime(task.cron_expression);

        // Check max executions
        if (task.max_executions > 0 && task.execution_count >= task.max_executions) {
            task.status = CronJobStatus::Completed;
            LOG_INFO("Task {} completed max executions", task.id);
        }
    } else {
        // One-shot tasks auto-delete after execution
        task.status = CronJobStatus::Completed;
    }

    if (task.durable) {
        SaveToFile();
    }
}

void CronScheduler::SchedulerLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(30));  // Check every 30 seconds

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, task] : tasks_) {
            if (ShouldRunNow(task)) {
                // Execute in a separate thread to not block the scheduler
                std::thread([this, id]() {
                    ExecuteTask(tasks_[id]);
                }).detach();
            }
        }
    }
}

bool CronScheduler::SaveToFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Create parent directory if needed
    fs::path p(path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }

    nlohmann::json json = nlohmann::json::array();

    for (const auto& [id, task] : tasks_) {
        if (task.durable) {
            nlohmann::json j;
            j["id"] = task.id;
            j["cron_expression"] = task.cron_expression;
            j["prompt"] = task.prompt;
            j["description"] = task.description;
            j["recurring"] = task.recurring;
            j["durable"] = task.durable;
            j["max_executions"] = task.max_executions;
            j["execution_count"] = task.execution_count;
            j["status"] = static_cast<int>(task.status);
            j["created_at"] = task.created_at;
            j["last_executed_at"] = task.last_executed_at;
            j["next_execution_at"] = task.next_execution_at;
            j["last_result"] = task.last_result;
            json.push_back(j);
        }
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        LOG_ERROR("Failed to open file for writing: {}", path);
        return false;
    }

    ofs << json.dump(2);
    ofs.close();

    LOG_INFO("Saved {} durable tasks to {}", json.size(), path);
    return true;
}

bool CronScheduler::LoadFromFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!fs::exists(path)) {
        LOG_DEBUG("No scheduled tasks file found at {}", path);
        return false;
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        LOG_ERROR("Failed to open file for reading: {}", path);
        return false;
    }

    try {
        nlohmann::json json;
        ifs >> json;

        int loaded = 0;
        for (const auto& j : json) {
            ScheduledTask task;
            task.id = j.value("id", "");
            task.cron_expression = j.value("cron_expression", "");
            task.prompt = j.value("prompt", "");
            task.description = j.value("description", "");
            task.recurring = j.value("recurring", true);
            task.durable = j.value("durable", false);
            task.max_executions = j.value("max_executions", 0);
            task.execution_count = j.value("execution_count", 0);
            task.status = static_cast<CronJobStatus>(j.value("status", 0));
            task.created_at = j.value("created_at", "");
            task.last_executed_at = j.value("last_executed_at", "");
            task.next_execution_at = j.value("next_execution_at", "");
            task.last_result = j.value("last_result", "");

            tasks_[task.id] = task;
            loaded++;
        }

        LOG_INFO("Loaded {} durable tasks from {}", loaded, path);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse scheduled tasks file: {}", e.what());
        return false;
    }
}

}  // namespace aicode

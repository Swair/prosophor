// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>

#include <nlohmann/json.hpp>

namespace prosophor {

/// 主动触发插件信息
struct ActiveTriggerPlugin {
    std::string name;
    std::string path;
    std::string role;           // 覆盖 active_trigger.role；空 = 全局缺省
    std::string mode;           // "periodic" / "idle" / "idle_once"
    int interval = 0;           // periodic 模式：间隔秒数
    int threshold = 0;          // idle 模式：空闲阈值秒数
    int cooldown = 300;         // idle 模式：两次触发的最小间隔（秒）
    int priority = 50;          // 优先级 0-100
    int timeout = 100;          // 脚本超时毫秒
    std::string script;         // 脚本文件名，默认 "trigger"
    int64_t last_run = 0;       // 上次运行时间戳（秒）
    bool enabled = true;
    int triggered_count = 0;    // 已触发次数（idle_once 模式用）

    /// 从 JSON 配置加载
    static ActiveTriggerPlugin FromJson(const nlohmann::json& json, const std::string& path);
};

/// 一次主动触发的上下文（交给 LLM 执行回调）
struct ActiveTriggerRequest {
    std::string plugin;         // 插件名，用于会话隔离与投递来源
    std::string role;           // 插件配置的角色；空 = 全局缺省
    std::string reason;         // 触发脚本 stdout（或兜底文案）
    std::string prompt_md;      // ACTIVE.md / prompt.md
};

/// 主动触发管理器 - 调度层核心
/// 功能：
/// 1. 扫描并加载 active/ 目录下的插件
/// 2. 双模式调度：周期性强制触发 + 用户空闲触发
/// 3. 优先级调度 + 超时保护
/// 4. 触发后调用 LLM 生成回复
class ActiveTriggerManager {
public:
    static ActiveTriggerManager& GetInstance();

    /// 初始化（指定 active/ 目录）
    void Initialize(const std::string& active_dir = "~/.prosophor/active");

    /// 启动调度器
    void Start();

    /// 停止调度器
    void Stop();

    /// 设置 LLM 执行回调
    using LlmExecuteCallback = std::function<void(const ActiveTriggerRequest& request)>;
    void SetLlmExecuteCallback(LlmExecuteCallback cb);

    /// 设置用户交互回调（用于空闲检测）
    using UserInteractionCallback = std::function<bool()>;
    void SetUserInteractionCallback(UserInteractionCallback cb);

    /// 重新扫描插件目录（热加载）
    void ReloadPlugins();

    /// 获取所有插件状态
    std::vector<ActiveTriggerPlugin> ListPlugins() const;

    ~ActiveTriggerManager();

private:
    ActiveTriggerManager() = default;

    /// 展开路径中的 ~
    static std::string ExpandHome(const std::string& path);

    /// 扫描并加载插件
    void ScanAndLoadPlugins();

    /// 加载单个插件
    bool LoadPlugin(const std::string& plugin_dir);

    /// 解析 trigger_mode.cfg
    bool ParseTriggerModeConfig(const std::string& config_path, ActiveTriggerPlugin& plugin);

    /// 读取 prompt：ACTIVE.md → SKILLS.md/SKILL.md（剥 frontmatter）→ prompt.md
    std::string ReadPromptMd(const std::string& plugin_path) const;

    /// 调度器循环
    void SchedulerLoop();

    /// 执行插件触发脚本
    bool ExecuteTrigger(const ActiveTriggerPlugin& plugin, std::string& trigger_reason);

    /// 执行 LLM 回调（结果由 ProactiveMessageCallback 投递）
    void ExecuteLlmCallback(const ActiveTriggerRequest& request);

    std::string active_dir_;
    std::vector<ActiveTriggerPlugin> periodic_plugins_;  // 周期性插件池
    std::vector<ActiveTriggerPlugin> idle_plugins_;      // 空闲插件池

    LlmExecuteCallback llm_callback_;
    UserInteractionCallback user_interaction_callback_;

    std::atomic<bool> running_{false};
    std::thread scheduler_thread_;
    mutable std::mutex mutex_;
};

}  // namespace prosophor

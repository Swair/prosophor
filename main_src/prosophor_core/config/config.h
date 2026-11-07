// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "voice_interaction/voice_config.h"

namespace prosophor {

// Forward declarations
struct ModelConfig;
struct ModelCost;
struct ModelDefinition;
struct LlmProviderConfig;
struct ToolConfig;
struct SkillEntryConfig;
struct SkillsLoadConfig;
struct SkillsConfig;
struct SecurityConfig;
struct ProsophorConfig;

/// Cost information for a model
struct ModelCost {
    double input = 0;
    double output = 0;
    double cache_read = 0;
    double cache_write = 0;

    static ModelCost FromJson(const nlohmann::json& json);
};

/// Definition of an LLM model
struct ModelDefinition {
    std::string id;
    std::string name;
    bool reasoning = false;
    std::vector<std::string> input = {"text"};
    ModelCost cost;
    int context_window = 0;
    int max_tokens = 0;

    static ModelDefinition FromJson(const nlohmann::json& json);
};

/// Model behavior configuration
struct ModelConfig {
    std::string name = "default";
    std::string model = "claude-sonnet-4-6";
    double temperature = 0.7;
    int max_tokens = 8192;
    int context_window = 128000;
    bool thinking = false;
    int thinking_budget_tokens = 4096;
    std::string reasoning_effort = "medium";
    bool use_tools = true;
    bool enable_streaming = true;  // Whether to use streaming for responses

    // Auto-compaction
    bool auto_compact = true;
    int compact_max_messages = 100;
    int compact_keep_recent = 20;
    int compact_max_tokens = 100000;

    static ModelConfig FromJson(const nlohmann::json& json);
    int DynamicMaxIterations() const;
};

/// Configuration for a single provider entry (each array item keeps its own api_key/base_url)
struct LlmProviderEntryConfig {
    std::string api_key;
    std::string base_url;
    int timeout = 30;
    bool thinking = false;
    std::unordered_map<std::string, ModelConfig> models;
};

/// Configuration for local NPU model (aidgen in-process, QCS8550 等)
/// aidgen-vl 与纯文本同构：多一份 vlm_model 段与视觉后端，由配置内容分流。
struct AidGenModelConfig {
    static AidGenModelConfig FromJson(const nlohmann::json& json);
    nlohmann::ordered_json ToJson() const;

    std::string config_file;        // aidgen 模型配置 JSON 路径
    std::string backend = "auto";   // aidgen 推理后端: "auto"|"qnn236"|"qnn240"|"qnn248"
    bool auto_start = true;         // Load model on startup

    // aidgen-vl 专用：VL 模型配置 JSON（含 vlm_model 段）；空则回落 config_file
    std::string vl_config_file;
    std::string vit_backend = "auto";   // 视觉编码器(aidlite)后端

    // 提示词标记覆盖点：默认 ChatML + Qwen-VL 视觉界标
    std::string im_start     = "<|im_start|>";
    std::string im_end       = "<|im_end|>";
    std::string vision_start = "<|vision_start|>";
    std::string vision_end   = "<|vision_end|>";
};

/// Configuration for local model (llama.cpp in-process)
struct LlamacppModelConfig {
    static LlamacppModelConfig FromJson(const nlohmann::json& json);
    nlohmann::ordered_json ToJson() const;
    std::string model_path;         // Path to GGUF model file
    std::string mmproj_path;        // Path to multimodal projector (mmproj) file
    int port = 8080;                // Legacy: kept for config compatibility
    int n_gpu_layers = -1;      // Layers to offload to GPU (-1 = all, 0 = CPU only, N = N layers)
    int threads = 0;                // CPU threads (0 = auto-detect)
    bool auto_start = true;         // Load model on startup
    int start_timeout_ms = 300000;  // Legacy: kept for config compatibility
    std::string server_path;        // Legacy: kept for config compatibility

    // Inference parameters
    int   context_window = 32768; // Context window size (tokens)
    int   max_tokens = 2048; // Max generated tokens per response
    float temperature  = 0.7f;   // Sampling temperature (0.0 = greedy, 1.0 = creative)
    float top_p        = 0.95f;  // Nucleus sampling probability
    std::string thinking_start;  // Thinking start marker (model-specific, e.g. <|channel|>)
    std::string thinking_end;    // Thinking end marker (model-specific, e.g. <channel|>)
    std::string tool_call_start = "<|tool_call>";   // Tool call open marker
    std::string tool_call_end   = "<tool_call|>";   // Tool call close marker
    std::string end_of_turn     = "<end_of_turn>";  // End-of-turn marker (Gemma-style)
    std::string start_of_turn   = "<start_of_turn>";// Start-of-turn marker (Gemma-style)

    // KV cache quantization type: "f16", "q8_0", "q4_0" (default)
    // Maps to ggml_type: F16=2 bytes/value, Q8_0=1 byte, Q4_0=0.5 byte
    // Lower = less VRAM but slightly lower generation quality
    std::string type_k = "q4_0";
    std::string type_v = "q4_0";

    // Performance tuning
    int  n_batch         = 1024;   // Prompt processing batch size (larger = faster but more VRAM)
    int  n_ubatch        = 512;    // GPU micro-batch size (keep <= n_batch)
    int  n_threads_batch = 0;      // Batch/prefix threads (0 = auto: threads * 4, min 32)
    bool offload_kqv     = true;   // Offload KQV matrices to GPU (false saves VRAM at speed cost)
    bool flash_attn      = true;   // Flash Attention (always enable on modern GPUs)

    // MoE routing (Qwen 35B-A3B etc.)
    bool cpu_moe = false; // Keep MoE expert weights on CPU to save VRAM
    bool no_mmap = false; // Disable mmap (needed when cpu_moe=true for performance)

    // Sampling
    float min_p = 0.05f;  // Min-P threshold (0.0 = off, higher = more aggressive filtering)
    int   seed  = -1;     // Random seed (-1 = LLAMA_DEFAULT_SEED)
};

/// Configuration for an LLM provider
/// models key format: "{provider_name}/{model_name}" → model params
struct LlmProviderConfig {
    bool enabled = true;   // false = 保留配置但不创建/装载 provider
    std::string api_key;
    std::string base_url;
    int timeout = 30;

    // models key: "{provider_name}/{model_name}"
    std::unordered_map<std::string, ModelConfig> model_configs;

    std::vector<ModelDefinition> models;

    // llamacpp-specific config (model_path, gpu_enable, etc.) — only valid
    // when this LlmProviderConfig is for the "llamacpp" provider type.
    LlamacppModelConfig llamacpp_cfg;

    // aidgen(NPU) 专用配置 — only valid when provider type is "aidgen"/"aidgen-vl"
    AidGenModelConfig aidgen_cfg;

    // All entries from the config array (each keeps its own api_key/base_url)
    std::vector<LlmProviderEntryConfig> entries;

    static LlmProviderConfig FromJson(const nlohmann::json& json);

    // Get default model config
    const ModelConfig& GetDefaultModel() const;

    // Find entry by model name and return its base_url and api_key
    bool FindEntryForModel(const std::string& provider_name,
                           const std::string& model,
                           std::string& out_base_url,
                           std::string& out_api_key,
                           int& out_timeout) const;
};

/// Tool configuration settings
struct ToolConfig {
    bool enabled = true;
    std::vector<std::string> allowed_paths;
    std::vector<std::string> denied_paths;
    std::vector<std::string> allowed_cmds;
    std::vector<std::string> denied_cmds;
    int timeout = 60;

    static ToolConfig FromJson(const nlohmann::json& json);
};

/// Configuration for a single skill
struct SkillEntryConfig {
    bool enabled = true;

    static SkillEntryConfig FromJson(const nlohmann::json& json);
};

/// Configuration for loading skills
struct SkillsLoadConfig {
    std::vector<std::string> extra_dirs;

    static SkillsLoadConfig FromJson(const nlohmann::json& json);
};

/// Configuration for all skills
struct SkillsConfig {
    std::string path;
    std::vector<std::string> auto_approve;
    SkillsLoadConfig load;
    std::unordered_map<std::string, SkillEntryConfig> entries;
    nlohmann::json configs;

    static SkillsConfig FromJson(const nlohmann::json& json);
};

/// Security configuration settings
struct SecurityConfig {
    std::string permission_level = "auto";
    bool allow_local_execute = true;

    static SecurityConfig FromJson(const nlohmann::json& json) {
        SecurityConfig c;
        c.permission_level = json.value("permission_level", json.value("permissionLevel", "auto"));
        c.allow_local_execute = json.value("allow_local_execute", json.value("allowLocalExecute", true));
        return c;
    }
};

/// 摄像头抓帧配置（nutribot 常驻形态：ffmpeg 定期单帧抓拍落盘，供 VL 看菜）
struct CameraConfig {
    std::string device = "/dev/video2";                          // v4l2 节点（Astra Pro 板上为 video2）
    std::string output_dir = "~/.prosophor/nutribot_camera";   // 帧落盘目录（~ 启动时展开）
    int interval_ms = 1500;                                      // 抓帧节拍（帧新鲜度；抓拍本身 ~1.2s）
    int perception_interval_ms = 3000;                           // 看菜（VL）节拍（用餐/备餐态）
    int idle_interval_ms = 15000;                                // 空闲/餐后态的看菜（VL）节拍
    int timeout_ms = 8000;                                       // 单次抓拍超时（含设备唤醒）
    int keep_frames = 3;                                         // 落盘保留帧数（防 VL 读时被删）

    static CameraConfig FromJson(const nlohmann::json& json);
};

/// 饭点窗口配置（"HH:MM" 原文；解析与判定在 main_src/nutribot/meal_schedule）
struct MealWindowConfig {
    std::string name;
    std::string start;
    std::string end;
};

/// 饭点日程配置（进入窗口时主动播报问候）
struct MealConfig {
    bool announce = true;
    std::vector<MealWindowConfig> windows = {
        {"早餐", "07:00", "09:00"},
        {"午餐", "11:30", "13:30"},
        {"晚餐", "17:30", "19:30"},
    };

    static MealConfig FromJson(const nlohmann::json& json);
};

/// nutribot 特有装配：成员档案目录与菜品营养库
struct NutribotConfig {
    std::string member_store_dir;            // 空 = <BaseDir>/nutribot_members
    std::string nutrition_file;              // 菜品营养库；空 = <BaseDir>/nutribot_dishes.json

    // 唤醒词：整句归一化匹配；应答后进入聆听窗口，窗口内可直接追问
    std::vector<std::string> wake_words = {"你好瓦力"};
    std::string wake_ack_text = "我在";
    int awake_window_ms = 8000;

    // 思考填充语：语音派发后、LLM 首句到手前先播的固定话术（空 = 关闭）。
    // vision_fillers 用于看菜类问题；两组各自轮转，只进 TTS，不进对话历史。
    std::vector<std::string> thinking_fillers = {
        "嗯……我想想", "让我想想啊", "嗯，我看看"};
    std::vector<std::string> vision_fillers = {
        "让我看看桌上有什么", "我瞅瞅这盘菜", "嗯，我瞧瞧"};

    static NutribotConfig FromJson(const nlohmann::json& json);
};

/// 机器人底盘/舵机控制（远程主控的 HTTP 接口）
///
/// 安全默认：enabled=false —— 运动是物理动作，不配就不注册工具。
/// move_pulse_ms 是固定脉冲（工具内计时自动 stop），模型改不了这个数。
struct RobotControlConfig {
    bool enabled = false;                            // 总开关：关则控制工具不注册
    std::string base_url = "http://192.168.0.202";   // 主控地址（无尾斜杠）
    int default_speed = 50;                          // speed 缺省值（0~100）
    int move_pulse_ms = 1000;                        // 每次移动的固定脉冲时长
    long command_timeout_ms = 2000;                  // 单次 HTTP 超时（毫秒）

    static RobotControlConfig FromJson(const nlohmann::json& json);
};

/// Configuration for the Web server (multi-user browser frontend)
/// 独立可执行 prosophor_web 使用;桌面端默认关闭。
struct WebConfig {
    bool enabled = false;             // master switch: start HTTP + WebSocket server
    std::string host = "0.0.0.0";     // 默认绑定所有网卡(局域网设备可访问;仅本机用 127.0.0.1)
    int port = 8787;
    std::string web_root;             // empty = exe 相邻 web-dist 目录
    std::string role = "default";     // Web 渠道私聊绑定角色(群聊可在 groups.json 覆盖)
    std::string data_dir;             // empty = ~/.prosophor/web(users/groups/tokens)
    int token_ttl_hours = 168;        // 认证 token TTL(小时,默认 7 天)

    static WebConfig FromJson(const nlohmann::json& json);
};

/// Configuration for active triggers (plugins under ~/.prosophor/active)
struct ActiveTriggerConfig {
    std::string role;   // 缺省角色；空 = default_role[0]，插件 trigger_mode.cfg 可覆盖

    static ActiveTriggerConfig FromJson(const nlohmann::json& json);
};

/// Top-level Prosophor configuration
struct ProsophorConfig {
    std::string log_level = "info";
    std::vector<std::string> default_role = {"default"};  // Default roles (SDL: one sprite per role, TUI: first only)

    bool enable_summary = false;     // 是否启用对话摘要（system prompt 指令 + 摘要提取循环）

    // ── 字体缩放常量（与 MediaCore::kFontScaleMin/Max 同步）──
    static constexpr float kFontScaleSmall  = 0.8f;
    static constexpr float kFontScaleLarge  = 1.1f;
    static constexpr float kFontScaleSwitch = 0.95f; // 切换阈值 (< → small, ≥ → large)
    std::string sprite_assets_dir;   // sprite 资源基目录 ~/.prosophor/assets，按 {sprite_id}/ 组织
    std::string workspace_path;      // 工作目录路径（用于 system prompt 和文件操作）
    float font_scale = kFontScaleLarge;      // 当前字体缩放倍率

    SecurityConfig security;
    std::unordered_map<std::string, LlmProviderConfig> llm_providers;
    ToolConfig tools;
    SkillsConfig skills;
    TtsConfig tts;
    AsrConfig asr;
    VoiceConfig voice;
    CameraConfig camera;
    MealConfig meal;
    NutribotConfig nutribot;
    RobotControlConfig robot;
    WebConfig web;
    ActiveTriggerConfig active_trigger;
    std::vector<LlamacppModelConfig> llamacpp_models;

    /// Get singleton instance
    static ProsophorConfig& GetInstance();

    /// Get current agent config from default provider
    const ModelConfig& GetModelConfig() const;

    /// Get provider config
    const LlmProviderConfig& GetProvider(const std::string& name = "anthropic") const;

    /// Load config from file
    static ProsophorConfig FromJson(const nlohmann::json& json);
    static ProsophorConfig LoadFromFile(const std::string& filepath);
    static std::string DefaultConfigPath();
    static std::filesystem::path BaseDir();
    /// Directory containing shipped read-only config (exe-adjacent .prosophor/)
    static std::filesystem::path InstallConfigDir();
    static void CreateDefaultConfig(const std::string& filepath = DefaultConfigPath());

    /// Save config to file
    void SaveToFile(const std::string& filepath = DefaultConfigPath()) const;

    /// Convert to JSON (ordered_json for readable key order)
    nlohmann::ordered_json ToJson() const;

 private:
    static std::string config_path_override_;
    static ProsophorConfig* instance_;
};

}  // namespace prosophor

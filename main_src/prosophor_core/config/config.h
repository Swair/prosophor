// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

#include <nlohmann/json.hpp>

namespace prosophor {

// Forward declarations
struct ModelConfig;
struct ModelCost;
struct ModelDefinition;
struct ProviderConfig;
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
struct ProviderEntryConfig {
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
struct ProviderConfig {
    std::string api_key;
    std::string base_url;
    int timeout = 30;

    // models key: "{provider_name}/{model_name}"
    std::unordered_map<std::string, ModelConfig> model_configs;

    std::vector<ModelDefinition> models;

    // llamacpp-specific config (model_path, gpu_enable, etc.) — only valid
    // when this ProviderConfig is for the "llamacpp" provider type.
    LlamacppModelConfig llamacpp_cfg;

    // aidgen(NPU) 专用配置 — only valid when provider type is "aidgen"/"aidgen-vl"
    AidGenModelConfig aidgen_cfg;

    // All entries from the config array (each keeps its own api_key/base_url)
    std::vector<ProviderEntryConfig> entries;

    static ProviderConfig FromJson(const nlohmann::json& json);

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

/// Configuration for TTS (Text-to-Speech)
struct TtsConfig {
    bool enabled = true;
    std::string backend = "edge-tts";  // "edge-tts" | "gpt-sovits" | "sherpa-onnx"
    std::string voice = "zh-CN-XiaoxiaoNeural";

    // GPT-SoVITS settings
    std::string gs_url = "http://127.0.0.1:9880";
    std::string gs_install_path;
    bool gs_auto_start = true;
    int gs_port = 9880;
    std::string gs_ref_audio_path;
    std::string gs_ref_audio_text;
    std::string gs_ref_audio_lang = "zh";
    std::string gs_text_lang = "zh";

    // sherpa-onnx VITS settings
    std::string sherpa_model_dir;   // dir with model.onnx + tokens.txt [+ lexicon.txt]
    int   sherpa_speaker_id = 0;
    float sherpa_speed      = 1.0f;

    // aidvoice (MeloTTS) 本地 NPU 后端：backend == "aidvoice" 时生效。
    // SDK 1.5.0 无发音人参数，只有语速可调
    std::string aidvoice_model_dir;   // 模型目录（config.json 的 type 决定中/英）
    double aidvoice_speed = 1.0;      // 语速倍率（1.0 = 模型原生）

    // edge-tts voice list
    std::vector<std::string> voice_list;

    static TtsConfig FromJson(const nlohmann::json& json);
};

/// Configuration for ASR (Automatic Speech Recognition)
struct AsrConfig {
    bool enabled = false;
    bool push_to_talk = true;         // true: hold M to talk; false: VAD full-duplex always-on
    std::string backend = "http";     // "http"（whisper server）| "aidvoice"（本地 NPU，nutribot 形态）
    std::string server_url = "http://127.0.0.1:9100";  // whisper server endpoint

    // aidvoice 本地后端：backend == "aidvoice" 时生效
    std::string model_dir;            // 模型目录（含 config.json 与 *.aidem）
    int echo_ms = 15000;              // 单轮推理窗口；SenseVoice 上限 15000
    int timeout_ms = 20000;           // 单句等待 FINAL 上限（超时回空串）
    bool reuse_instance = true;       // false = 每句重建实例（1.6s/句 退路开关）

    static AsrConfig FromJson(const nlohmann::json& json);
};

/// Configuration for the resident voice loop（nutribot 常驻语音回路）
struct VoiceConfig {
    std::string capture_device = "plughw:CARD=PowerConf,DEV=0";
    std::string playback_device = "plughw:0,0";
    double vad_rms_dbfs = -38.0;      // 能量 VAD 门限（dBFS）
    int start_confirm_frames = 3;     // 起判确认帧数（90ms）
    int end_silence_ms = 700;         // 句尾静音判定
    int max_utterance_ms = 12000;     // 单句上限（须 < asr.echo_ms）
    int pre_roll_frames = 5;          // 起始回捞帧数（150ms）
    int warmup_discard_frames = 8;    // 开流后丢弃帧数（设备唤醒期可能是数字静音）
    int speak_max_chars = 120;        // 单次播报截断（按字符）
    int speak_min_gap_ms = 10000;     // 主动播报最小间隔

    static VoiceConfig FromJson(const nlohmann::json& json);
};

/// 摄像头抓帧配置（nutribot 常驻形态：ffmpeg 定期单帧抓拍落盘，供 VL 看菜）
struct CameraConfig {
    std::string device = "/dev/video2";                          // v4l2 节点（Astra Pro 板上为 video2）
    std::string output_dir = "/tmp/prosophor/nutribot_camera";   // 帧落盘目录
    int interval_ms = 3000;                                      // 抓帧节拍（≈ 用餐态感知节拍）
    int idle_interval_ms = 15000;                                // 空闲/餐后态的感知（VL）节拍
    int timeout_ms = 8000;                                       // 单次抓拍超时（含设备唤醒）
    int keep_frames = 3;                                         // 落盘保留帧数（防 VL 读时被删）
    // 场景帧差的像素差二值化阈值（0-255）：静态画面噪声高（过曝/自动曝光泵动）
    // 时会误判"运动"，板上标定优先调它（见 scene_monitor.h）
    int motion_pixel_threshold = 12;

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

/// nutribot 特有装配：成员档案目录与"常在场"桩成员（人脸识别接入前的演示链路）
struct NutribotConfig {
    std::string member_store_dir;            // 空 = <BaseDir>/nutribot_members
    std::string stub_member_name = "小明";    // 桩人脸常驻成员名（须在档案库中）
    std::string nutrition_file;              // 菜品营养库；空 = <BaseDir>/nutribot_dishes.json

    static NutribotConfig FromJson(const nlohmann::json& json);
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
    std::unordered_map<std::string, ProviderConfig> llm_providers;
    ToolConfig tools;
    SkillsConfig skills;
    TtsConfig tts;
    AsrConfig asr;
    VoiceConfig voice;
    CameraConfig camera;
    MealConfig meal;
    NutribotConfig nutribot;
    WebConfig web;
    std::vector<LlamacppModelConfig> llamacpp_models;

    /// Get singleton instance
    static ProsophorConfig& GetInstance();

    /// Get current agent config from default provider
    const ModelConfig& GetModelConfig() const;

    /// Get provider config
    const ProviderConfig& GetProvider(const std::string& name = "anthropic") const;

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

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "config/config.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "common/constants.h"
#include "common/log_wrapper.h"
#include "common/file_utils.h"
#include "managers/agent_role_loader.h"
#include "platform/platform.h"

namespace prosophor {

std::string ProsophorConfig::config_path_override_;
ProsophorConfig* ProsophorConfig::instance_ = nullptr;

ProsophorConfig& ProsophorConfig::GetInstance() {
    static ProsophorConfig instance;
    static bool initialized = false;

    if (!initialized) {
        std::string config_path = DefaultConfigPath();
        CreateDefaultConfig(config_path);

        try {
            instance = LoadFromFile(config_path);
        } catch (const std::exception& e) {
            LOG_WARN("Failed to load config, using defaults: {}", e.what());
        }
        initialized = true;
    }

    return instance;
}

const ModelConfig& ProsophorConfig::GetModelConfig() const {
    // Load default role to get its provider and agent config
    std::string primary_role = default_role.empty() ? "default" : default_role[0];
    std::string role_path = "config/.prosophor/roles/" + primary_role + ".json";
    if (FileExists(role_path)) {
        auto& loader = AgentRoleLoader::GetInstance();
        try {
            AgentRole role = loader.LoadRole(role_path);
            // Find the agent config from the role's provider
            auto prov_it = llm_providers.find(role.provider_prot);
            if (prov_it != llm_providers.end()) {
                auto& agent_map = prov_it->second.model_configs;
                // Try 1: role.model as key (agent name)
                auto agent_it = agent_map.find(role.model);
                if (agent_it == agent_map.end()) {
                    // Try 2: provider_name/model_name key
                    std::string full_key = role.provider_prot + "/" + role.model;
                    agent_it = agent_map.find(full_key);
                }
                if (agent_it != agent_map.end()) {
                    return agent_it->second;
                }
                // Fall back to provider's default model
                return prov_it->second.GetDefaultModel();
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to load default role '{}', using fallback: {}", primary_role, e.what());
        }
    }

    // Fallback: use first provider's default model
    if (!llm_providers.empty()) {
        return llm_providers.begin()->second.GetDefaultModel();
    }

    static ModelConfig fallback_model;
    return fallback_model;
}

const ProviderConfig& ProsophorConfig::GetProvider(const std::string& name) const {
    auto it = llm_providers.find(name);
    if (it != llm_providers.end()) {
        return it->second;
    }
    static ProviderConfig default_provider;
    return default_provider;
}

const ModelConfig& ProviderConfig::GetDefaultModel() const {
    if (model_configs.empty()) {
        static ModelConfig default_model;
        return default_model;
    }
    // Array format: use first model; object format: try "default" key first, then first entry
    auto it = model_configs.find("default");
    if (it != model_configs.end()) {
        return it->second;
    }
    return model_configs.begin()->second;
}

bool ProviderConfig::FindEntryForModel(const std::string& provider_name,
                                        const std::string& model,
                                        std::string& out_base_url,
                                        std::string& out_api_key,
                                        int& out_timeout) const {
    for (const auto& entry : entries) {
        auto it = entry.models.find(model);
        if (it == entry.models.end()) {
            // Also try searching by model name in agent.config.model
            for (const auto& [k, v] : entry.models) {
                if (v.model == model) {
                    it = entry.models.find(k);
                    break;
                }
            }
        }
        if (it != entry.models.end()) {
            out_base_url = entry.base_url;
            out_api_key = entry.api_key;
            out_timeout = entry.timeout;
            return true;
        }
    }
    // Fallback: search by full key provider_name/model
    std::string full_key = provider_name + "/" + model;
    auto model_it = model_configs.find(full_key);
    if (model_it != model_configs.end()) {
        // Find matching entry
        for (const auto& entry : entries) {
            for (const auto& [k, v] : entry.models) {
                if (v.model == model || k == model) {
                    out_base_url = entry.base_url;
                    out_api_key = entry.api_key;
                    out_timeout = entry.timeout;
                    return true;
                }
            }
        }
    }
    return false;
}

// Substitutes environment variables in the form ${VAR_NAME}
static std::string SubstituteEnvVars(const std::string& input) {
    static const std::regex env_re(R"(\$\{([^}]+)\})");
    std::string result;
    auto begin = std::sregex_iterator(input.begin(), input.end(), env_re);
    auto end = std::sregex_iterator();

    size_t last_pos = 0;
    for (auto it = begin; it != end; ++it) {
        auto& match = *it;
        result.append(input, last_pos, match.position() - last_pos);
        std::string var_name = match[1].str();
        const char* env_val = std::getenv(var_name.c_str());
        if (env_val) {
            result.append(env_val);
        }
        last_pos = match.position() + match.length();
    }
    result.append(input, last_pos, std::string::npos);
    return result;
}

// Strips comments and trailing commas from JSON5
static std::string StripJson5(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    size_t i = 0;
    const size_t len = input.size();

    while (i < len) {
        if (input[i] == '"') {
            out += '"';
            ++i;
            while (i < len && input[i] != '"') {
                if (input[i] == '\\' && i + 1 < len) {
                    out += input[i];
                    out += input[i + 1];
                    i += 2;
                } else {
                    out += input[i];
                    ++i;
                }
            }
            if (i < len) {
                out += '"';
                ++i;
            }
            continue;
        }

        if (i + 1 < len && input[i] == '/' && input[i + 1] == '/') {
            i += 2;
            while (i < len && input[i] != '\n') ++i;
            continue;
        }

        if (i + 1 < len && input[i] == '/' && input[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(input[i] == '*' && input[i + 1] == '/')) ++i;
            if (i + 1 < len) i += 2;
            continue;
        }

        out += input[i];
        ++i;
    }

    std::string result;
    result.reserve(out.size());
    for (size_t j = 0; j < out.size(); ++j) {
        if (out[j] == ',') {
            size_t k = j + 1;
            while (k < out.size() && (out[k] == ' ' || out[k] == '\t' ||
                                      out[k] == '\n' || out[k] == '\r')) {
                ++k;
            }
            if (k < out.size() && (out[k] == '}' || out[k] == ']')) {
                continue;
            }
        }
        result += out[j];
    }

    return result;
}

// Expands environment variables in JSON
static void ExpandEnvInJson(nlohmann::json& j) {
    if (j.is_string()) {
        auto& s = j.get_ref<std::string&>();
        if (s.find("${") != std::string::npos) {
            s = SubstituteEnvVars(s);
        }
    } else if (j.is_object()) {
        for (auto& [key, value] : j.items()) {
            ExpandEnvInJson(value);
        }
    } else if (j.is_array()) {
        for (auto& element : j) {
            ExpandEnvInJson(element);
        }
    }
}

ModelConfig ModelConfig::FromJson(const nlohmann::json& json) {
    ModelConfig config;
    config.name = json.value("name", "default");
    config.model = json.value("model", "claude-sonnet-4-6");
    config.temperature = json.value("temperature", kDefaultTemperature);
    config.max_tokens = json.value("max_tokens", json.value("maxTokens", kDefaultMaxTokens));
    config.context_window = json.value("context_window", json.value("contextWindow", kDefaultContextWindow));
    config.thinking = json.value("thinking", false);
    config.thinking_budget_tokens = json.value("thinking_budget_tokens", json.value("thinkingBudgetTokens", 4096));
    config.reasoning_effort = json.value("reasoning_effort", json.value("reasoningEffort", "medium"));
    config.use_tools = json.value("use_tools", json.value("useTools", true));
    config.enable_streaming = json.value("enable_streaming", json.value("enableStreaming", true));
    config.auto_compact = json.value("auto_compact", json.value("autoCompact", true));
    config.compact_max_messages = json.value("compact_max_messages", json.value("compactMaxMessages", kDefaultCompactMaxMessages));
    config.compact_keep_recent = json.value("compact_keep_recent", json.value("compactKeepRecent", kDefaultCompactKeepRecent));
    config.compact_max_tokens = json.value("compact_max_tokens", json.value("compactMaxTokens", kDefaultCompactMaxTokens));
    return config;
}

ModelCost ModelCost::FromJson(const nlohmann::json& json) {
    ModelCost c;
    c.input = json.value("input", 0.0);
    c.output = json.value("output", 0.0);
    c.cache_read = json.value("cacheRead", json.value("cache_read", 0.0));
    c.cache_write = json.value("cacheWrite", json.value("cache_write", 0.0));
    return c;
}

ModelDefinition ModelDefinition::FromJson(const nlohmann::json& json) {
    ModelDefinition m;
    m.id = json.value("id", "");
    m.name = json.value("name", "");
    m.reasoning = json.value("reasoning", false);
    m.input = json.value("input", std::vector<std::string>{"text"});
    if (json.contains("cost") && json["cost"].is_object()) {
        m.cost = ModelCost::FromJson(json["cost"]);
    }
    m.context_window = json.value("contextWindow", json.value("context_window", 0));
    m.max_tokens = json.value("maxTokens", json.value("max_tokens", 0));
    return m;
}

ProviderConfig ProviderConfig::FromJson(const nlohmann::json& json) {
    ProviderConfig config;
    config.api_key = json.value("api_key", json.value("apiKey", ""));
    config.base_url = json.value("base_url", json.value("baseUrl", ""));
    config.timeout = json.value("timeout", kDefaultProviderTimeoutSec);
    config.aidgen_cfg = AidGenModelConfig::FromJson(json);

    // Parse models — supports array (preferred) and object (legacy) formats
    const auto& models_json_key = json.contains("models") ? "models" : "model";
    if (json.contains(models_json_key)) {
        const auto& aj = json[models_json_key];
        if (aj.is_array()) {
            for (const auto& item : aj) {
                ModelConfig agent = ModelConfig::FromJson(item);
                if (!agent.model.empty()) {
                    config.model_configs[agent.model] = agent;
                }
            }
        } else if (aj.is_object()) {
            for (const auto& [key, value] : aj.items()) {
                ModelConfig agent = ModelConfig::FromJson(value);
                agent.name = key;
                if (json.contains("tools_use")) {
                    agent.use_tools = json.value("tools_use", true);
                }
                config.model_configs[key] = agent;
            }
        }
    }

    if (json.contains("models") && json["models"].is_array()) {
        for (const auto& m : json["models"]) {
            config.models.push_back(ModelDefinition::FromJson(m));
        }
    }
    return config;
}

AidGenModelConfig AidGenModelConfig::FromJson(const nlohmann::json& json) {
    AidGenModelConfig config;
    // 兼容旧的散列键：aidgen_config / aidgen_vl_config / aidgen_vit_backend
    config.config_file = Platform::NormalizePath(
        json.value("config_file", json.value("aidgen_config", "")));
    config.vl_config_file = Platform::NormalizePath(
        json.value("vl_config_file", json.value("aidgen_vl_config", "")));
    config.backend = json.value("backend", json.value("aidgen_backend", "auto"));
    config.vit_backend = json.value("vit_backend",
                                    json.value("aidgen_vit_backend", "auto"));
    config.auto_start = json.value("auto_start", true);

    // 提示词标记：模型定制时在此覆盖，缺省为 ChatML + Qwen-VL 视觉界标
    if (json.contains("chat_markers") && json["chat_markers"].is_object()) {
        const auto& markers = json["chat_markers"];
        config.im_start     = markers.value("im_start",     config.im_start);
        config.im_end       = markers.value("im_end",       config.im_end);
        config.vision_start = markers.value("vision_start", config.vision_start);
        config.vision_end   = markers.value("vision_end",   config.vision_end);
    }
    return config;
}

nlohmann::ordered_json AidGenModelConfig::ToJson() const {
    nlohmann::ordered_json j;
    j["config_file"] = config_file;
    j["backend"] = backend;
    j["auto_start"] = auto_start;
    j["vl_config_file"] = vl_config_file;
    j["vit_backend"] = vit_backend;
    j["chat_markers"] = {
        {"im_start", im_start}, {"im_end", im_end},
        {"vision_start", vision_start}, {"vision_end", vision_end},
    };
    return j;
}

LlamacppModelConfig LlamacppModelConfig::FromJson(const nlohmann::json& json) {
    LlamacppModelConfig config;
    config.model_path = Platform::NormalizePath(json.value("model_path", ""));
    config.mmproj_path = Platform::NormalizePath(json.value("mmproj_path", ""));
    config.port = json.value("port", 8080);
    // Backward compat: gpu_enable=true → -1, gpu_enable=false → 0; else use n_gpu_layers directly
    if (json.contains("gpu_enable")) {
        config.n_gpu_layers = json.value("gpu_enable", true) ? -1 : 0;
    } else {
        config.n_gpu_layers = json.value("n_gpu_layers", -1);
    }
    config.threads      = json.value("threads", json.value("n_threads", json.value("nThreads", 0)));
    config.auto_start = json.value("auto_start", json.value("autoStart", true));
    config.start_timeout_ms = json.value("start_timeout_ms", json.value("startTimeoutMs", 60000));
    config.server_path = json.value("server_path", json.value("serverPath", ""));
	    // Inference parameters
    config.context_window  = json.value("context_window", json.value("n_ctx", 32768));
    config.max_tokens = json.value("max_tokens", json.value("max_new_tokens", 2048));
    config.temperature   = json.value("temperature",    0.7f);
    config.top_p         = json.value("top_p",          0.95f);
    config.thinking_start = json.value("thinking_start",  "<|channel>");
    config.thinking_end   = json.value("thinking_end",    "<channel|");
    config.tool_call_start = json.value("tool_call_start", "<|tool_call>");
    config.tool_call_end   = json.value("tool_call_end",   "<tool_call|>");
    config.end_of_turn   = json.value("end_of_turn",   "<end_of_turn>");
    config.start_of_turn = json.value("start_of_turn", "<start_of_turn>");
    config.type_k = json.value("type_k", "q4_0");
    config.type_v = json.value("type_v", "q4_0");
    config.n_batch         = json.value("n_batch", 1024);
    config.n_ubatch        = json.value("n_ubatch", 512);
    config.n_threads_batch = json.value("n_threads_batch", 0);
    config.offload_kqv     = json.value("offload_kqv", true);
    config.flash_attn      = json.value("flash_attn", true);
    config.cpu_moe         = json.value("cpu_moe", false);
    config.no_mmap         = json.value("no_mmap", false);
    config.min_p           = json.value("min_p", 0.05f);
    config.seed            = json.value("seed", -1);
    return config;
}

nlohmann::ordered_json LlamacppModelConfig::ToJson() const {
    nlohmann::ordered_json j;
    j["model_path"] = model_path;
    j["mmproj_path"] = mmproj_path;
    j["n_gpu_layers"] = n_gpu_layers;
    j["threads"] = threads;
    j["auto_start"] = auto_start;
    j["context_window"] = context_window;
    j["max_tokens"] = max_tokens;
    j["temperature"] = temperature;
    j["top_p"] = top_p;
    j["type_k"] = type_k;
    j["type_v"] = type_v;
    j["n_batch"] = n_batch;
    j["n_ubatch"] = n_ubatch;
    j["offload_kqv"] = offload_kqv;
    j["flash_attn"] = flash_attn;
    j["cpu_moe"] = cpu_moe;
    j["no_mmap"] = no_mmap;
    j["min_p"] = min_p;
    j["seed"] = seed;
    j["thinking_start"] = thinking_start;
    j["thinking_end"] = thinking_end;
    j["end_of_turn"] = end_of_turn;
    j["start_of_turn"] = start_of_turn;
    j["tool_call_start"] = tool_call_start;
    j["tool_call_end"] = tool_call_end;
    return j;
}

TtsConfig TtsConfig::FromJson(const nlohmann::json& json) {
    TtsConfig config;
    config.enabled = json.value("enabled", true);

    // Read from provider array (preferred) or flat fields (legacy)
    if (json.contains("provider") && json["provider"].is_array() && !json["provider"].empty()) {
        const auto& p = json["provider"][0];
        config.backend = p.value("backend", "edge-tts");
        config.gs_auto_start = p.value("auto_start", config.gs_auto_start);
        if (p.contains("voice_list") && p["voice_list"].is_array()) {
            for (const auto& v : p["voice_list"])
                config.voice_list.push_back(v.get<std::string>());
        }
    } else {
        config.backend = json.value("backend", "edge-tts");
        config.voice = json.value("voice", "zh-CN-XiaoxiaoNeural");
        if (json.contains("voice_list") && json["voice_list"].is_array()) {
            for (const auto& v : json["voice_list"])
                config.voice_list.push_back(v.get<std::string>());
        }
    }
    // Always include "none" option to disable TTS per role
    config.voice_list.push_back("none");
    config.gs_url = json.value("gs_url", json.value("gsUrl", "http://127.0.0.1:9880"));
    config.gs_install_path = json.value("gs_install_path", json.value("gsInstallPath", ""));
    config.gs_auto_start = json.value("gs_auto_start", json.value("gsAutoStart", true));
    config.gs_port = json.value("gs_port", json.value("gsPort", 9880));
    config.gs_ref_audio_path = json.value("gs_ref_audio_path", json.value("gsRefAudioPath", ""));
    config.gs_ref_audio_text = json.value("gs_ref_audio_text", json.value("gsRefAudioText", ""));
    config.gs_ref_audio_lang = json.value("gs_ref_audio_lang", json.value("gsRefAudioLang", "zh"));
    config.gs_text_lang = json.value("gs_text_lang", json.value("gsTextLang", "zh"));
    // sherpa-onnx TTS
    config.sherpa_model_dir  = json.value("sherpa_model_dir",  "");
    config.sherpa_speaker_id = json.value("sherpa_speaker_id", 0);
    config.sherpa_speed      = json.value("sherpa_speed",       1.0f);
    // aidvoice (MeloTTS) 本地 NPU 后端
    config.aidvoice_model_dir = json.value("aidvoice_model_dir", std::string(""));
    config.aidvoice_speed     = json.value("aidvoice_speed", 1.0);
    return config;
}

AsrConfig AsrConfig::FromJson(const nlohmann::json& json) {
    AsrConfig config;
    config.enabled        = json.value("enabled",        false);
    config.push_to_talk   = json.value("push_to_talk",   true);
    config.backend        = json.value("backend",        std::string("http"));
    config.server_url     = json.value("server_url",     std::string("http://127.0.0.1:9100"));
    // aidvoice 本地后端（settings.json 的历史键名 model_path 一并兼容）
    config.model_dir      = json.value("model_dir", json.value("model_path", std::string("")));
    config.echo_ms        = json.value("echo_ms", 15000);
    config.timeout_ms     = json.value("timeout_ms", 20000);
    config.reuse_instance = json.value("reuse_instance", true);
    return config;
}

VoiceConfig VoiceConfig::FromJson(const nlohmann::json& json) {
    VoiceConfig config;
    config.capture_device  = json.value("capture_device", config.capture_device);
    config.playback_device = json.value("playback_device", config.playback_device);
    config.vad_rms_dbfs    = json.value("vad_rms_dbfs", config.vad_rms_dbfs);
    config.start_confirm_frames = json.value("start_confirm_frames", config.start_confirm_frames);
    config.end_silence_ms  = json.value("end_silence_ms", config.end_silence_ms);
    config.max_utterance_ms = json.value("max_utterance_ms", config.max_utterance_ms);
    config.pre_roll_frames = json.value("pre_roll_frames", config.pre_roll_frames);
    config.warmup_discard_frames = json.value("warmup_discard_frames", config.warmup_discard_frames);
    config.speak_max_chars = json.value("speak_max_chars", config.speak_max_chars);
    config.speak_min_gap_ms = json.value("speak_min_gap_ms", config.speak_min_gap_ms);
    return config;
}

CameraConfig CameraConfig::FromJson(const nlohmann::json& json) {
    CameraConfig config;
    config.device      = json.value("device", config.device);
    config.output_dir  = json.value("output_dir", config.output_dir);
    config.interval_ms = json.value("interval_ms", config.interval_ms);
    config.scene_interval_ms =
        json.value("scene_interval_ms", config.scene_interval_ms);
    config.perception_interval_ms =
        json.value("perception_interval_ms", config.perception_interval_ms);
    config.idle_interval_ms = json.value("idle_interval_ms", config.idle_interval_ms);
    config.timeout_ms  = json.value("timeout_ms", config.timeout_ms);
    config.keep_frames = json.value("keep_frames", config.keep_frames);
    config.motion_pixel_threshold =
        json.value("motion_pixel_threshold", config.motion_pixel_threshold);
    return config;
}

MealConfig MealConfig::FromJson(const nlohmann::json& json) {
    MealConfig config;
    config.announce = json.value("announce", config.announce);
    if (json.contains("windows") && json["windows"].is_array()) {
        config.windows.clear();   // 配置给出的窗口整体替换默认三餐
        for (const auto& w : json["windows"]) {
            if (!w.is_object()) continue;
            MealWindowConfig window;
            window.name  = w.value("name", std::string(""));
            window.start = w.value("start", std::string(""));
            window.end   = w.value("end", std::string(""));
            config.windows.push_back(std::move(window));
        }
    }
    return config;
}

NutribotConfig NutribotConfig::FromJson(const nlohmann::json& json) {
    NutribotConfig config;
    config.member_store_dir = json.value("member_store_dir", config.member_store_dir);
    config.stub_member_name = json.value("stub_member_name", config.stub_member_name);
    config.nutrition_file   = json.value("nutrition_file", config.nutrition_file);
    return config;
}

RobotControlConfig RobotControlConfig::FromJson(const nlohmann::json& json) {
    RobotControlConfig config;
    config.enabled            = json.value("control_enabled", config.enabled);
    config.base_url           = json.value("base_url", config.base_url);
    config.default_speed      = json.value("default_speed", config.default_speed);
    config.move_pulse_ms      = json.value("move_pulse_ms", config.move_pulse_ms);
    config.command_timeout_ms = json.value("command_timeout_ms", config.command_timeout_ms);
    return config;
}

WebConfig WebConfig::FromJson(const nlohmann::json& json) {
    WebConfig config;
    config.enabled        = json.value("enabled", false);
    config.host           = json.value("host", "127.0.0.1");
    config.port           = json.value("port", 8787);
    config.web_root       = json.value("web_root", "");
    config.role           = json.value("role", "default");
    config.data_dir       = json.value("data_dir", "");
    config.token_ttl_hours = json.value("token_ttl_hours", 168);
    return config;
}

ToolConfig ToolConfig::FromJson(const nlohmann::json& json) {
    ToolConfig config;
    config.enabled = json.value("enabled", true);
    config.allowed_paths = json.value("allowed_paths", std::vector<std::string>{});
    config.denied_paths = json.value("denied_paths", std::vector<std::string>{});
    config.allowed_cmds = json.value("allowed_cmds", std::vector<std::string>{});
    config.denied_cmds = json.value("denied_cmds", std::vector<std::string>{});
    config.timeout = json.value("timeout", kDefaultToolTimeoutSec);
    return config;
}

SkillEntryConfig SkillEntryConfig::FromJson(const nlohmann::json& json) {
    SkillEntryConfig config;
    config.enabled = json.value("enabled", true);
    return config;
}

SkillsLoadConfig SkillsLoadConfig::FromJson(const nlohmann::json& json) {
    SkillsLoadConfig config;
    config.extra_dirs = json.value("extraDirs", std::vector<std::string>{});
    return config;
}

SkillsConfig SkillsConfig::FromJson(const nlohmann::json& json) {
    SkillsConfig config;
    config.path = json.value("path", "");
    config.auto_approve = json.value("auto_approve", json.value("autoApprove", std::vector<std::string>{}));
    if (json.contains("configs") && json["configs"].is_object()) {
        config.configs = json["configs"];
    }
    if (json.contains("load") && json["load"].is_object()) {
        config.load = SkillsLoadConfig::FromJson(json["load"]);
    }
    if (!config.path.empty() && config.load.extra_dirs.empty()) {
        config.load.extra_dirs.push_back(config.path);
    }
    if (json.contains("entries") && json["entries"].is_object()) {
        for (const auto& [key, value] : json["entries"].items()) {
            config.entries[key] = SkillEntryConfig::FromJson(value);
        }
    }
    return config;
}

ProsophorConfig ProsophorConfig::FromJson(const nlohmann::json& json) {
    nlohmann::json expanded = json;
    ExpandEnvInJson(expanded);

    ProsophorConfig config;
    config.log_level = expanded.value("log_level", expanded.value("logLevel", "info"));
    // default_role can be string (backwards compat) or array of strings
    if (expanded.contains("default_role") && expanded["default_role"].is_array()) {
        config.default_role = expanded["default_role"].get<std::vector<std::string>>();
    } else {
        std::string single = expanded.value("default_role", expanded.value("defaultRole", "default"));
        config.default_role = {single};
    }
    config.enable_summary = expanded.value("enable_summary", false);
    config.sprite_assets_dir = ExpandHome(expanded.value("sprite_assets_dir", "~/.prosophor/assets"));
    config.workspace_path = ExpandHome(expanded.value("workspace_path", ""));
    config.font_scale = expanded.value("font_scale", ProsophorConfig::kFontScaleLarge);

    if (expanded.contains("llm_providers") && expanded["llm_providers"].is_object()) {
        for (const auto& [key, value] : expanded["llm_providers"].items()) {
            if (value.is_array()) {
                // Array format: keep all entries, key models by provider_name/model_name
                ProviderConfig merged_config;
                bool first = true;
                for (const auto& entry : value) {
                    ProviderConfig entry_config = ProviderConfig::FromJson(entry);

                    if (first) {
                        merged_config = entry_config;
                        first = false;
                    }

                    // For llamacpp — extract LlamacppModelConfig from model entries.
                    // MUST be after `merged_config = entry_config` so it doesn't get overwritten.
                    if (key == "llamacpp" && entry.contains("models")) {
                        bool entry_auto_start = entry.value("auto_start", true);
                        for (const auto& m : entry["models"]) {
                            auto lcfg = LlamacppModelConfig::FromJson(m);
                            lcfg.auto_start = (config.llamacpp_models.empty())
                                              ? entry_auto_start
                                              : false;
                            if (!lcfg.model_path.empty()) {
                                config.llamacpp_models.push_back(lcfg);
                                merged_config.llamacpp_cfg = lcfg;
                            }
                        }
                    }

                    // Store entry for lookup by FindEntryForModel
                    ProviderEntryConfig e;
                    e.api_key = entry_config.api_key;
                    e.base_url = entry_config.base_url;
                    e.timeout = entry_config.timeout;
                    e.models = entry_config.model_configs;
                    merged_config.entries.push_back(std::move(e));
                    // Key models as provider_name/model_name
                    for (auto& [model_name, model_config] : entry_config.model_configs) {
                        std::string model_key = key + "/" + model_config.model;
                        merged_config.model_configs[model_key] = model_config;
                    }
                    // Merge models
                    for (auto& model : entry_config.models) {
                        merged_config.models.push_back(model);
                    }
                }
                config.llm_providers[key] = merged_config;
            } else {
                // Object format: use directly
                config.llm_providers[key] = ProviderConfig::FromJson(value);
            }
        }
    }
    if (expanded.contains("security") && expanded["security"].is_object()) {
        config.security = SecurityConfig::FromJson(expanded["security"]);
    }
    if (expanded.contains("tools") && expanded["tools"].is_object()) {
        config.tools = ToolConfig::FromJson(expanded["tools"]);
    }
    if (expanded.contains("skills") && expanded["skills"].is_object()) {
        config.skills = SkillsConfig::FromJson(expanded["skills"]);
    }
    if (expanded.contains("tts") && expanded["tts"].is_object()) {
        config.tts = TtsConfig::FromJson(expanded["tts"]);
    }
    if (expanded.contains("asr") && expanded["asr"].is_object()) {
        config.asr = AsrConfig::FromJson(expanded["asr"]);
    }
    if (expanded.contains("voice") && expanded["voice"].is_object()) {
        config.voice = VoiceConfig::FromJson(expanded["voice"]);
    }
    if (expanded.contains("camera") && expanded["camera"].is_object()) {
        config.camera = CameraConfig::FromJson(expanded["camera"]);
    }
    if (expanded.contains("meal") && expanded["meal"].is_object()) {
        config.meal = MealConfig::FromJson(expanded["meal"]);
    }
    if (expanded.contains("nutribot") && expanded["nutribot"].is_object()) {
        config.nutribot = NutribotConfig::FromJson(expanded["nutribot"]);
    }
    if (expanded.contains("robot") && expanded["robot"].is_object()) {
        config.robot = RobotControlConfig::FromJson(expanded["robot"]);
    }
    if (expanded.contains("web") && expanded["web"].is_object()) {
        config.web = WebConfig::FromJson(expanded["web"]);
    }
    return config;
}

ProsophorConfig ProsophorConfig::LoadFromFile(const std::string& filepath) {
    std::string expanded_path = ExpandHome(filepath);

    if (!FileExists(expanded_path)) {
        throw std::runtime_error("Config file not found: " + expanded_path);
    }

    std::ifstream file(expanded_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + expanded_path);
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    std::string clean = StripJson5(content);
    nlohmann::json json = nlohmann::json::parse(clean);

    return FromJson(json);
}

std::string ProsophorConfig::DefaultConfigPath() {
    if (!config_path_override_.empty()) {
        return config_path_override_;
    }
    // Support environment variable override: export PROSOPHOR_CONFIG="/path/to/config.json"
    const char* env_path = std::getenv("PROSOPHOR_CONFIG");
    if (env_path != nullptr && env_path[0] != '\0') {
        return env_path;
    }
    const auto user_config = ExpandHome("~/.prosophor/settings.json");
    if (FileExists(user_config)) {
        return user_config;
    }

    std::string exe_path = Platform::GetSelfExePath();
    if (!exe_path.empty()) {
        auto local_config = std::filesystem::path(exe_path).parent_path() / ".prosophor" / "settings.json";
        if (FileExists(local_config.string())) {
            return local_config.string();
        }
    }
    return user_config;
}

std::filesystem::path ProsophorConfig::InstallConfigDir() {
    std::string exe_path = Platform::GetSelfExePath();
    if (!exe_path.empty()) {
        auto local_dir = std::filesystem::path(exe_path).parent_path() / ".prosophor";
        if (DirExists(local_dir.string())) {
            return local_dir;
        }
    }
    return {};
}

std::filesystem::path ProsophorConfig::BaseDir() {
    // Support environment variable override
    const char* env_path = std::getenv("PROSOPHOR_CONFIG");
    if (env_path != nullptr && env_path[0] != '\0') {
        return std::filesystem::path(env_path).parent_path();
    }
    // Use user home directory (always writable)
    return ExpandHome("~/.prosophor");
}

// Creates a default config file with documentation comments
void ProsophorConfig::CreateDefaultConfig(const std::string& filepath) {
    std::string expanded_path = ExpandHome(filepath);

    // Create directory if it doesn't exist
    std::filesystem::path parent_dir = std::filesystem::path(expanded_path).parent_path();
    std::filesystem::create_directories(parent_dir);

    // Don't overwrite existing config
    if (FileExists(expanded_path)) {
        return;
    }

    // Check if there's a demo config at config/.prosophor/settings.json
    std::string demo_config_path = "config/.prosophor/settings.json";
    if (FileExists(demo_config_path)) {
        // Copy demo config to ~/.prosophor/settings.json
        try {
            std::filesystem::copy_file(demo_config_path, expanded_path,
                                        std::filesystem::copy_options::overwrite_existing);
            LOG_DEBUG("Created config from demo: {}", demo_config_path);
            return;
        } catch (const std::exception& e) {
            LOG_WARN("Failed to copy demo config: {}", e.what());
            // Fall through to create default config
        }
    }

    std::ofstream file(expanded_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create config file: " + expanded_path);
    }

    // Write config template with comments
    file << R"(// Prosophor Configuration File
// Path: ~/.prosophor/settings.json
//
// Structure:
//   llm_providers.<provider_name>.models.<model_name> = { model, temperature, ... }
//   Roles are defined in config/.prosophor/roles/*.json
//
// Example:
//   llm_providers.anthropic.models.default.model = "qwen3.5-plus"
//   llm_providers.deepseek.models.pro.model = "deepseek-v4-pro"

{
  "default_role": ["default"],        // Default roles (SDL: one sprite per role, TUI: first only)
  "log_level": "info",

  // Provider configuration (array format supports multiple instances)
  "llm_providers": {
    "anthropic": [
      {
        "api_key": "${ANTHROPIC_API_KEY}",
        "base_url": "https://api.anthropic.com",
        "timeout": 60,

        // Multiple model configurations
        "models": {
          "default": {
            "model": "claude-sonnet-4-6",
            "temperature": 0.7,
            "max_tokens": 8192,
            "context_window": 128000,
            "use_tools": true,
            "thinking": false,
            "thinking_budget_tokens": 4096,
            "enable_streaming": true
          }
        }
      }
    ],

    // DeepSeek (OpenAI-compatible protocol)
    "deepseek": [
      {
        "api_key": "${DEEPSEEK_API_KEY}",
        "base_url": "https://api.deepseek.com/chat/completions",
        "timeout": 60,
        "models": {
          "default": {
            "model": "deepseek-chat",
            "temperature": 0.7,
            "max_tokens": 8192,
            "context_window": 128000,
            "use_tools": true
          }
        }
      }
    ]
  },

  "security": {
    "permission_level": "auto",
    "allow_local_execute": true
  },

  "tools": {
    "enabled": true,
    "timeout": 60
  },

  "skills": {
    "path": "./skills",
    "auto_approve": ["read_file", "grep"]
  },

  // Web server (multi-user browser frontend, prosophor_web executable)
  "web": {
    "enabled": false,
    "host": "127.0.0.1",
    "port": 8787,
    "web_root": "",
    "role": "default",
    "data_dir": "",
    "token_ttl_hours": 168
  }
}
)";

    file.close();
}

int ModelConfig::DynamicMaxIterations() const {
    if (context_window <= kContextWindow32K) return kMinMaxIterations;
    if (context_window >= kContextWindow200K) return kMaxMaxIterations;

    double ratio = static_cast<double>(context_window - kContextWindow32K) /
                   (kContextWindow200K - kContextWindow32K);
    return kMinMaxIterations +
           static_cast<int>(ratio * (kMaxMaxIterations - kMinMaxIterations));
}

// ==================== ProsophorConfig JSON Serialization ====================

nlohmann::ordered_json ProsophorConfig::ToJson() const {
    nlohmann::ordered_json json;

    json["default_role"] = default_role;
    json["log_level"] = log_level;
    json["enable_summary"] = enable_summary;
    json["sprite_assets_dir"] = sprite_assets_dir;
    json["workspace_path"] = workspace_path;
    json["font_scale"] = font_scale;

    // Serialize llm_providers in fixed order: anthropic, ollama, openai, llamacpp
    static const char* kProviderOrder[] = {"anthropic", "ollama", "openai", "llamacpp"};
    nlohmann::ordered_json llm_providers_json;
    for (const char* name : kProviderOrder) {
        auto it = llm_providers.find(name);
        if (it == llm_providers.end()) continue;
        const auto& config = it->second;
        nlohmann::ordered_json entries_json = nlohmann::ordered_json::array();

        if (!config.entries.empty()) {
            for (size_t ei = 0; ei < config.entries.size(); ++ei) {
                const auto& entry = config.entries[ei];
                nlohmann::ordered_json models_json = nlohmann::ordered_json::array();
                for (const auto& [model_name, model_config] : entry.models) {
                    nlohmann::ordered_json model_json;
                    bool is_llamacpp = (strcmp(name, "llamacpp") == 0 && ei == 0 && !llamacpp_models.empty());
                    if (is_llamacpp) {
                        // Build from llamacpp_models in correct field order
                        const auto& lc = llamacpp_models[0];
                        model_json["model"] = model_config.model;
                        model_json["model_path"] = lc.model_path;
                        model_json["mmproj_path"] = lc.mmproj_path;
                        model_json["n_gpu_layers"] = lc.n_gpu_layers;
                        model_json["threads"] = lc.threads;
                        model_json["context_window"] = lc.context_window;
                        model_json["max_tokens"] = lc.max_tokens;
                        model_json["temperature"] = lc.temperature;
                        model_json["top_p"] = lc.top_p;
                        model_json["type_k"] = lc.type_k;
                        model_json["type_v"] = lc.type_v;
                        model_json["n_batch"] = lc.n_batch;
                        model_json["n_ubatch"] = lc.n_ubatch;
                        model_json["offload_kqv"] = lc.offload_kqv;
                        model_json["flash_attn"] = lc.flash_attn;
                        model_json["cpu_moe"] = lc.cpu_moe;
                        model_json["no_mmap"] = lc.no_mmap;
                        model_json["min_p"] = lc.min_p;
                        model_json["seed"] = lc.seed;
                        model_json["thinking_start"] = lc.thinking_start;
                        model_json["thinking_end"] = lc.thinking_end;
                        model_json["end_of_turn"] = lc.end_of_turn;
                        model_json["start_of_turn"] = lc.start_of_turn;
                        model_json["tool_call_start"] = lc.tool_call_start;
                        model_json["tool_call_end"] = lc.tool_call_end;
                    } else {
                        model_json["model"] = model_config.model;
                        model_json["temperature"] = model_config.temperature;
                        model_json["max_tokens"] = model_config.max_tokens;
                        model_json["context_window"] = model_config.context_window;
                    }
                    if (model_config.thinking) {
                        model_json["thinking"] = true;
                        model_json["thinking_budget_tokens"] = model_config.thinking_budget_tokens;
                        if (model_config.reasoning_effort != "medium") {
                            model_json["reasoning_effort"] = model_config.reasoning_effort;
                        }
                    }
                    models_json.push_back(model_json);
                }

                nlohmann::ordered_json entry_json;
                entry_json["api_key"] = entry.api_key;
                entry_json["base_url"] = entry.base_url;
                entry_json["timeout"] = entry.timeout;
                if (strcmp(name, "llamacpp") == 0 && !llamacpp_models.empty()) {
                    entry_json["auto_start"] = llamacpp_models[0].auto_start;
                }
                entry_json["models"] = models_json;
                entries_json.push_back(entry_json);
            }
        } else {
            // Fallback for entries that were never parsed (shouldn't happen)
            nlohmann::ordered_json models_json = nlohmann::ordered_json::array();
            for (const auto& [model_name, model_config] : config.model_configs) {
                nlohmann::ordered_json model_json;
                model_json["model"] = model_config.model;
                model_json["temperature"] = model_config.temperature;
                model_json["max_tokens"] = model_config.max_tokens;
                model_json["context_window"] = model_config.context_window;
                if (model_config.thinking) {
                    model_json["thinking"] = true;
                    model_json["thinking_budget_tokens"] = model_config.thinking_budget_tokens;
                }
                models_json.push_back(model_json);
            }
            nlohmann::ordered_json entry_json;
            entry_json["api_key"] = config.api_key;
            entry_json["base_url"] = config.base_url;
            entry_json["timeout"] = config.timeout;
            entry_json["models"] = models_json;
            entries_json.push_back(entry_json);
        }
        llm_providers_json[name] = entries_json;
    }
    json["llm_providers"] = llm_providers_json;

    // Serialize TTS (simplified)
    nlohmann::ordered_json tts_json;
    tts_json["enabled"] = tts.enabled;
    nlohmann::ordered_json tts_provider;
    tts_provider["backend"] = tts.backend;
    tts_provider["auto_start"] = tts.gs_auto_start;
    tts_provider["voice_list"] = tts.voice_list;
    tts_json["provider"] = nlohmann::ordered_json::array({tts_provider});
    json["tts"] = tts_json;

    // Serialize security
    nlohmann::ordered_json security_json;
    security_json["allow_local_execute"] = security.allow_local_execute;
    security_json["permission_level"] = security.permission_level;
    json["security"] = security_json;

    // Serialize web (must round-trip, otherwise SaveToFile drops the section)
    nlohmann::ordered_json web_json;
    web_json["enabled"] = web.enabled;
    web_json["host"] = web.host;
    web_json["port"] = web.port;
    web_json["web_root"] = web.web_root;
    web_json["role"] = web.role;
    web_json["data_dir"] = web.data_dir;
    web_json["token_ttl_hours"] = web.token_ttl_hours;
    json["web"] = web_json;

    // Note: tools and asr sections intentionally omitted from settings output

    return json;
}

void ProsophorConfig::SaveToFile(const std::string& filepath) const {
    prosophor::WriteOrderedJson(filepath, ToJson(), 2);
    LOG_DEBUG("Configuration saved to {}", filepath);
}

}  // namespace prosophor

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// 语音相关配置：ASR/TTS provider 与语音闭环共用。
// 从 prosophor_core/config/config.h 下沉，使 base/voice_interaction 不反向依赖 core。

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace prosophor {

/// Configuration for aidvoice (MeloTTS) local NPU TTS backend
struct AidVoiceTtsConfig {
    std::string model_dir;    // 模型目录（config.json 里的 type 决定中/英模型）
    double speed = 1.0;       // 语速倍率（>0；1.0 = 模型原生速度）
    int timeout_ms = 30000;   // 单次合成等待上限（长句 + 板载负载需要余量）
};

/// Configuration for TTS (Text-to-Speech)
struct TtsConfig {
    bool enabled = true;
    std::string backend = "sherpa-onnx";  // "sherpa-onnx" | "aidvoice" | "edge-tts"（由配置显式指定）
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

    // sherpa-onnx VITS settings（Windows 本地：MeloTTS）
    std::string sherpa_model_dir;   // dir with model.onnx + tokens.txt + lexicon.txt
    int   sherpa_speaker_id = 0;
    float sherpa_speed      = 1.0f;
    int   sherpa_num_threads = 0;   // 0 = auto

    // aidvoice (MeloTTS) 本地 NPU 后端：backend == "aidvoice" 时生效
    AidVoiceTtsConfig aidvoice_cfg;

    // edge-tts voice list
    std::vector<std::string> voice_list;

    static TtsConfig FromJson(const nlohmann::json& json);
    nlohmann::ordered_json ToJson() const;
};

/// Configuration for aidvoice (SenseVoice) local NPU ASR backend
struct AidVoiceAsrConfig {
    std::string model_dir;       // 模型目录（含 config.json 与 *.aidem）
    int echo_ms = 15000;         // 单轮推理窗口；SenseVoice 上限 15000
    int timeout_ms = 20000;      // 单句等待 FINAL 上限（超时返回空串，不阻塞主循环）
    bool reuse_instance = true;  // false = 每句重建实例（1.6s/句，E1 反例时的退路开关）
};

/// Configuration for ASR (Automatic Speech Recognition)
struct AsrConfig {
    bool enabled = false;
    bool push_to_talk = true;         // true: hold M to talk; false: VAD full-duplex always-on
    std::string backend = "sherpa-onnx";  // "sherpa-onnx"（本地）| "aidvoice"（板端 NPU）

    // aidvoice 本地后端：backend == "aidvoice" 时生效
    AidVoiceAsrConfig aidvoice_cfg;

    // sherpa-onnx 本地后端（SenseVoice）：目录含 model.int8.onnx|model.onnx + tokens.txt
    std::string sherpa_model_dir;
    int sherpa_num_threads = 0;       // 0 = auto
    bool sherpa_prefer_fp32 = false;  // true = 优先 model.onnx（fp32），否则优先 int8

    // ── 会话级识别参数（云端/流式后端使用；本地不支持的项忽略）──
    std::string language;              // 空 = 自动
    std::string prompt;                // 热词/上下文
    bool interim_results = true;       // 中间结果（intermediate_result）
    bool punctuation = true;
    bool inverse_text_normalization = true;
    int max_sentence_silence_ms = 800;
    std::string customization_id;
    std::string vocabulary_id;

    static AsrConfig FromJson(const nlohmann::json& json);
    nlohmann::ordered_json ToJson() const;
};

/// Configuration for the resident voice loop（常驻语音回路）
struct VoiceConfig {
    std::string capture_device = "plughw:CARD=PowerConf,DEV=0";
    std::string playback_device = "plughw:0,0";
    // auto = nutribot 用 energy（板上已调参）、SDL 用 webrtc；也可显式指定
    std::string vad_backend = "auto";  // "auto" | "energy" | "webrtc"
    int webrtc_vad_mode = 3;           // WebRTC VAD 激进度 0(宽松)~3(激进)
    double vad_rms_dbfs = -38.0;      // 能量 VAD 门限（dBFS）
    int start_confirm_frames = 3;     // 起判确认帧数（90ms）
    int end_silence_ms = 700;         // 句尾静音判定
    int max_utterance_ms = 12000;     // 单句上限（须 < asr.echo_ms）
    int pre_roll_frames = 5;          // 起始回捞帧数（150ms）
    int warmup_discard_frames = 8;    // 开流后丢弃帧数（设备唤醒期可能是数字静音）
    int speak_max_chars = 120;        // 单次播报截断（按字符）
    int speak_min_gap_ms = 10000;     // 主动播报最小间隔
    size_t max_pending_speech = 32;   // TTS 待播队列上限（1 = 只保留最新）
    size_t max_pending_asr_jobs = 8;  // ASR 队列上限（满时拒绝新句）
    size_t tts_cache_bytes = 8 * 1024 * 1024;  // TTS 音频缓存上限；0 = 关闭

    static VoiceConfig FromJson(const nlohmann::json& json);
    nlohmann::ordered_json ToJson() const;
};

}  // namespace prosophor

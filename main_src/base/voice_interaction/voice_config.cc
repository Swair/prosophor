// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "voice_interaction/voice_config.h"

#include "common/file_utils.h"

namespace prosophor {

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
    config.aidvoice_cfg.model_dir = ExpandHome(
        json.value("aidvoice_model_dir", std::string("")));
    config.aidvoice_cfg.speed = json.value("aidvoice_speed", 1.0);
    return config;
}

AsrConfig AsrConfig::FromJson(const nlohmann::json& json) {
    AsrConfig config;
    config.enabled        = json.value("enabled",        false);
    config.push_to_talk   = json.value("push_to_talk",   true);
    config.backend        = json.value("backend",        std::string("http"));
    config.server_url     = json.value("server_url",     std::string("http://127.0.0.1:9100"));
    // aidvoice 本地后端（settings.json 的历史键名 model_path 一并兼容）
    config.aidvoice_cfg.model_dir = ExpandHome(
        json.value("model_dir", json.value("model_path", std::string(""))));
    config.aidvoice_cfg.echo_ms        = json.value("echo_ms", 15000);
    config.aidvoice_cfg.timeout_ms     = json.value("timeout_ms", 20000);
    config.aidvoice_cfg.reuse_instance = json.value("reuse_instance", true);
    // 会话级识别参数（兼容 sibot 的 intermediate_result 键名）
    config.language        = json.value("language", std::string(""));
    config.prompt          = json.value("prompt", std::string(""));
    config.interim_results = json.value(
        "interim_results", json.value("intermediate_result", true));
    config.punctuation     = json.value("punctuation", true);
    config.inverse_text_normalization =
        json.value("inverse_text_normalization", true);
    config.max_sentence_silence_ms = json.value("max_sentence_silence_ms", 800);
    config.customization_id = json.value("customization_id", std::string(""));
    config.vocabulary_id    = json.value("vocabulary_id", std::string(""));
    return config;
}

VoiceConfig VoiceConfig::FromJson(const nlohmann::json& json) {
    VoiceConfig config;
    config.capture_device  = json.value("capture_device", config.capture_device);
    config.playback_device = json.value("playback_device", config.playback_device);
    config.vad_backend     = json.value("vad_backend", config.vad_backend);
    config.webrtc_vad_mode = json.value("webrtc_vad_mode", config.webrtc_vad_mode);
    config.vad_rms_dbfs    = json.value("vad_rms_dbfs", config.vad_rms_dbfs);
    config.start_confirm_frames = json.value("start_confirm_frames", config.start_confirm_frames);
    config.end_silence_ms  = json.value("end_silence_ms", config.end_silence_ms);
    config.max_utterance_ms = json.value("max_utterance_ms", config.max_utterance_ms);
    config.pre_roll_frames = json.value("pre_roll_frames", config.pre_roll_frames);
    config.warmup_discard_frames = json.value("warmup_discard_frames", config.warmup_discard_frames);
    config.speak_max_chars = json.value("speak_max_chars", config.speak_max_chars);
    config.speak_min_gap_ms = json.value("speak_min_gap_ms", config.speak_min_gap_ms);
    config.max_pending_speech =
        json.value("max_pending_speech", config.max_pending_speech);
    config.max_pending_asr_jobs =
        json.value("max_pending_asr_jobs", config.max_pending_asr_jobs);
    config.tts_cache_bytes = json.value("tts_cache_bytes", config.tts_cache_bytes);
    return config;
}

}  // namespace prosophor

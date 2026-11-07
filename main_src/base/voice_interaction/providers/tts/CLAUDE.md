# 模块: voice_interaction/providers/tts — 语音合成（TTS）

## 用途
文本转语音后端抽象层。当前实现：
- `SherpaTtsProvider`：sherpa-onnx MeloTTS（Windows SDL 构建，ONNX CPU，44.1kHz）
- `AidVoiceTtsProvider`：本地 NPU MeloTTS（nutribot 形态，aidvoice SDK）
- `EdgeTtsProvider`：WebSocket → Microsoft Bing TTS（需 libcurl ≥ 7.86；Windows SDL 不提供）

## 接口

```
TtsRequest{text, voice, sample_rate, channels} → Synthesize() →
TtsResponse{success, error_msg, pcm, sample_rate, channels}
```

- 全量合成，非流式（EdgeTTS 的 MP3 解码器不支持增量解码）。
- `WarmUp()` 让本地后端在启动期装载模型；无状态后端默认无操作。
- 合成结果的 LRU 缓存在上层 `voice_interaction/tts_audio_cache.{h,cc}`（sibot 风格）。

## 关键文件

| 文件 | 职责 |
|------|------|
| `tts_provider.h/.cc` | `TtsProvider` 纯虚基类 + `TtsRequest`/`TtsResponse`，`SynthesizeToFile` 默认实现 |
| `sherpa_tts_provider.h/.cc` | sherpa-onnx MeloTTS 本地实现（Windows，`PROSOPHOR_HAS_SHERPA`） |
| `edge_tts_provider.h/.cc` | Edge TTS WebSocket 实现 |
| `aidvoice_tts_provider.h/.cc` | 本地 NPU MeloTTS 实现（SDK 唯一接入点，文件名供 CMake 过滤） |
| `../asr_tts_provider_factory.{h,cc}` | ASR/TTS 工厂；按 backend 创建，不持有实例 |

## 依赖
- `common/`（日志、crypto、mp3 解码、文件工具）
- `base/network/websocket_client`（Edge）
- `voice_interaction/voice_config.h`（`TtsConfig`）
- aidvoice SDK（仅 nutribot 形态，`.cc` 内包含）

## 责任约束
- **不**包含音频播放代码，**不**管理音频设备
- **不**引用 `media_engine/`、`virtual_sprite/`、`nutribot/`

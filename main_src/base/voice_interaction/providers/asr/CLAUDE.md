# 模块: voice_interaction/providers/asr — 语音识别（ASR）

## 用途
语音识别后端抽象层。当前实现（全部离线本地）：
- `SherpaAsrProvider`：sherpa-onnx SenseVoice（Windows SDL 构建，ONNX CPU）
- `AidVoiceAsrProvider`：本地 NPU SenseVoice（nutribot 形态，aidvoice SDK）

## 接口

```cpp
// 会话级参数（sibot 的 Asr::__start 口径）：每句话只传一次
struct AsrOptions {
    int sample_rate = 16000; int channels = 1;
    std::string language, prompt;
    bool interim_results = true, punctuation = true, inverse_text_normalization = true;
    int max_sentence_silence_ms = 800;
    std::string customization_id, vocabulary_id;
};

struct AsrRequest { const int16_t* pcm; size_t sample_size; AsrOptions options; };
struct AsrResponse { bool success; std::string text, error_msg; float confidence; };

AsrResponse TranscribeFile(path);                 // WAV → 结果
AsrResponse AsrProcess(const AsrRequest&);        // 一句话 PCM(16k mono int16) → 结果
```

当前只做**一句话识别**，没有流式 provider。`AsrOptions` 里的
`language`/`prompt`/`interim_results`/`punctuation`/ITN/定制模型等是会话级参数；
本地 provider（`SherpaAsrProvider`/`AidVoiceAsrProvider`）只支持 16k 单声道，
不支持的字段会被忽略。provider 实例由 `VoiceInteraction::asr_mutex_` 串行化。
SDL 对本地后端关闭 interim（对累积 PCM 重复跑整句识别成本过高），只出 final。

## 关键文件

| 文件 | 职责 |
|------|------|
| `asr_provider.h` | `AsrProvider` 纯虚接口 + `AsrOptions/AsrRequest/AsrResponse` |
| `sherpa_asr_provider.h/.cc` | sherpa-onnx SenseVoice 本地实现（Windows，`PROSOPHOR_HAS_SHERPA`） |
| `aidvoice_asr_provider.h/.cc` | 本地 NPU SenseVoice（SDK 唯一接入点，文件名供 CMake 过滤） |
| `../asr_tts_provider_factory.{h,cc}` | ASR/TTS 工厂（backend → provider，可用性归一化） |

## 依赖
- `common/`（日志、文件工具）
- sherpa-onnx C API（仅 `PROSOPHOR_HAS_SHERPA`）
- `voice_interaction/voice_config.h`（`AsrConfig`）
- aidvoice SDK（仅 nutribot 形态，`.cc` 内包含）

## 责任约束
- **不**管理音频采集/播放（VAD 与设备在 `voice_interaction/vad`、`audio`）
- **不**引用 `media_engine/`、`virtual_sprite/`、`nutribot/`

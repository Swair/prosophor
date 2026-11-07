# 模块: voice_interaction — 统一语音交互

## 用途
ASR / TTS / VAD / 音频 I/O 的统一抽象与本地闭环。nutribot 与 virtual_sprite 共用同一套
`VoicePipeline` 状态机与 provider，各自只保留音频适配器与形态参数。

## 目录

```
voice_interaction/
├── voice_config.{h,cc}          # AsrConfig/TtsConfig/VoiceConfig/AidVoice* + FromJson
├── voice_pipeline.{h,cc}        # 核心状态机：VAD 切句 + ASR/TTS worker + 队列
├── voice_interaction.{h,cc}     # provider + pipeline + 音频端口 + 闭麦/预热/两阶段退出
├── speech_text.{h,cc}           # 播报净化、沉默标记
├── tts_audio_cache.{h,cc}       # sibot 风格 LRU（按字节上限）
├── vad/
│   ├── vad.h                    # IVad 接口
│   ├── energy_vad.{h,cc}        # 能量 VAD（nutribot 默认）
│   └── webrtc_vad.{h,cc}        # WebRTC VAD 策略（SDL 默认，包 rtc_wrapper/rtc）
├── audio/
│   ├── audio_io.h               # AudioInput / AudioOutput 端口（含 PullFrame/QueuedSamples）
│   ├── alsa_audio_io.{h,cc}     # ALSA 直连（仅 nutribot 编译）
│   ├── frame_queue_audio_output.{h,cc}  # 拉取式双工输出（SIP/设备 send 回调）
│   └── resampler.{h,cc}         # 任意率/声道 → 16k 单声道
├── providers/
│   ├── asr_tts_provider_factory.{h,cc}
│   ├── asr/                     # AsrProvider + sherpa(SenseVoice)/aidvoice 实现
│   └── tts/                     # TtsProvider + sherpa(MeloTTS)/aidvoice/edge 实现
├── rtc_wrapper/rtc/             # WebRTC VAD / NS 的 C 算法（所有形态编译）
└── remote/duplex_voice_link.h   # 云端全双工接口（iiix_ws 流程参考，未接协议）
```

Include 根：`main_src/base/voice_interaction`。因此 `"providers/asr/asr_provider.h"`、
`"rtc_wrapper/vad_processor.h"` 等写法可直接使用。

## 依赖约束

- 可依赖：`common/`、`network/`（已下沉到 base）、标准库、curl/OpenSSL/nlohmann。
- `audio/alsa_audio_io.*` 仅 nutribot 编译；`vad/webrtc_vad.*` 与 `rtc_wrapper/rtc/` 所有形态都编译。
- **不**依赖 `config/config.h`、`prosophor_core/`、`media_engine/`、`virtual_sprite/`、`nutribot/`。
- `voice_config.h` 由 core 的 `config/config.h` 反向 include，配置聚合仍由 `ProsophorConfig` 持有。
- SDL 单例门面 `VoiceEngine` 与 `MediaEngineAudioOutput` 保留在 `virtual_sprite/`
  （依赖 media_engine 与 ProsophorConfig），本模块只提供通用 `VoiceInteraction`。

## 两个形态的参数差异

| 参数 | nutribot | virtual_sprite |
|------|----------|----------------|
| VAD | `EnergyVad(vad_rms_dbfs)`（`auto`） | `WebRtcVad`（`auto`） |
| 采集 | `AlsaAudioInput` + 内部采集线程 | media_engine `VoiceChannel` 回调驱动 |
| 播放 | `AlsaAudioOutput`（按响应采样率开） | `MediaEngineAudioOutput`（重采样到 16k） |
| provider 生命周期 | Init 创建 + 预热 | Init 创建、不预热 |
| 收句静音 | `end_silence_ms`（默认 700ms） | 20 帧（约 600ms） |
| 最短句/峰值闸 | 开启 | 关闭 |
| interim | 关闭（只要 final） | 开启（输入框实时显示） |
| 单飞 | 上一句在飞丢弃新句 | final 不丢 |
| 播报闭麦 | `AudioInput::Mute`（硬件流保持） | VoiceChannel 回调丢帧 |
| 文本净化 | 话轮侧 `SanitizeForSpeech` | `SanitizeForSpeech/SanitizeTtsText` |

## VAD 选型

`voice.vad_backend`：`auto`（nutribot=energy、SDL=webrtc）、`energy`、`webrtc`。
`webrtc_vad_mode` 控制激进度（0 宽松 ~ 3 激进）。一般噪声环境 WebRTC 更准；
近讲安静场景能量 VAD 足够且更可预测。详见 `vad/CLAUDE.md`。

## 线程与退出顺序

```
采集线程 FeedFrame → VoicePipeline VAD 状态机 → ASR worker → final 文本队列
主循环 PollAsrResult → 应用 → VoicePipeline::EnqueueSpeech → TTS worker → SpeakFn（合成+播放）
```

- provider 在 `Init` 按配置创建；`asr_mutex_` 串行 AsrProcess（aidvoice 单缓冲）。
- `tts_mutex_` 串行缓存 + Synthesize；**播放时不持锁**。
- 退出：`StopCapture`（mute → join 采集 → close input）→ `StopAudio`
  （output.Abort → pipeline.Stop → asr.reset → tts.reset → output.Close）。
  nutribot 的 `_Exit` 路径必须在前后显式调用。

## 借鉴的流程

- **sibot**：TTS 音频 LRU 缓存；ASR 当前采用一句话识别，没有流式 provider。
- **sibot SIP 双工 PCM**：`FrameQueueAudioOutput` 承接 TTS 帧队列，应用把设备
  send/recv 回调直接映射到 `PullPlaybackFrame` / `FeedFrame`：

  ```cpp
  // recv：麦克风帧 → VAD/ASR
  voice.FeedFrame(reinterpret_cast<const int16_t*>(buffer), frame_size / 2);

  // send：从 TTS 队列拉一帧；false = 队列空 → 重新开始听（reListen）
  const bool had_audio = voice.PullPlaybackFrame(
      reinterpret_cast<int16_t*>(buffer), frame_size / 2);
  if (!had_audio) voice.ResetUtterance();
  ```

  `IsSpeaking()` 在 TTS worker 写完但传输层未拉完时仍返回 true；`QueuedPlaybackSamples()`
  可用于低水位判定。NLP 不内置：应用循环为 `PollAsrResult() → NLP/LLM → Speak()`。
- **iiix_ws**：`remote/duplex_voice_link.h` 定义云端全双工状态与 PCM 流向；
  当前不移植 xiaozhi WebSocket/Opus 协议，接入时由应用层实现该接口。

## 测试
- `tests/voice_interaction/`：VoicePipeline 状态机、interim、VAD 注入、TTS 缓存、文本净化。

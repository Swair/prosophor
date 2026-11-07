# 模块: voice_interaction/vad — VAD 策略

## 接口
`IVad`（`vad.h`）：`ProcessFrame(pcm, samples) → voiced` + `Reset()`。
`VoicePipeline` 只认这个接口，切句状态机（回捞/起判/收句/闸门）只有一份。

## 两个实现

| 实现 | 算法 | 默认形态 | 优点 | 局限 |
|------|------|----------|------|------|
| `EnergyVad` | 帧 RMS 与固定 dBFS 门限比较 | nutribot（`auto`） | 极便宜、可解释、板上已按 -38dBFS 调参 | 噪声底/增益变化会误触或漏判；无频谱区分 |
| `WebRtcVad` | WebRTC GMM VAD（多频带 + 自适应），mode 0~3 | SDL（`auto`） | 噪声/非稳态环境下更稳，误触少 | 需要 C 算法；mode 3 激进，轻声可能漏判；CPU 略高 |

## 准确度结论

- 一般室内/桌面环境：**WebRTC VAD 更准**，尤其有风扇、空调、系统回声时。
- 近讲、安静、固定增益（nutribot 的 PowerConf 桌面场景）：能量 VAD 经过
  `start_confirm_frames` / `min_peak` / `end_silence` 调参后足够，且行为可预测。
- 没有标注数据集时不做数值承诺；板上若误触多，优先切 `vad_backend=webrtc`，
  再从 `webrtc_vad_mode=2`（比默认 3 宽松）开始试。

## 配置

```json
"voice": {
  "vad_backend": "auto",     // auto | energy | webrtc
  "webrtc_vad_mode": 3,      // 0 宽松 ~ 3 激进
  "vad_rms_dbfs": -38.0      // 仅 energy
}
```

`auto`：nutribot → energy，SDL → webrtc。`webrtc` 在 nutribot 也可用
（`rtc_wrapper/rtc` 已支持 aarch64；ai_coding 形态剔除算法源）。

## 依赖
- `energy_vad`：仅标准库。
- `webrtc_vad`：`rtc_wrapper/rtc` 的 C 算法（零第三方）。

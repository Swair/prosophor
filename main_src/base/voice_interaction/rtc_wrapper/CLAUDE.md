# 模块: voice_interaction/rtc_wrapper — WebRTC VAD 算法

## 用途
WebRTC VAD / 噪声抑制的 C 算法移植（零外部依赖，纯标准库）。
VAD 的上层策略包装在 `../vad/webrtc_vad.{h,cc}`（实现 `IVad`），本目录只保留算法本体。

## 关键文件
- `rtc/` — WebRTC VAD / NS C 算法移植
  - `webrtc_vad.{h,c}`、`vad_core`、`vad_filterbank`、`vad_gmm`、`vad_sp`
  - `ns.{h,cc}`、`signal_processing_library.h`、`typedefs.h` 等

## 依赖
- 仅标准库（`<cstring>` 等），不依赖 SDL / common / prosophor_core。

## 责任约束
- **不**引用 `prosophor_core/`、`media_engine/`、`virtual_sprite/`、`nutribot/`。
- 算法无状态或自管理状态，线程安全由调用方保证。
- 已支持 aarch64（`typedefs.h` 增加 `WEBRTC_ARCH_ARM64`，走标量 C 路径）。
- 所有形态都编译本目录（C 算法自包含，已支持 aarch64）。
  nutribot 默认仍用 `EnergyVad`，可通过 `voice.vad_backend=webrtc` 切换。

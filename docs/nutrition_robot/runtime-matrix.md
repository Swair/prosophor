# Rhino Pi-X1 运行时版本矩阵（2026-09-08 实测）

目标：为 nutrition_robot C++ 版确定"官方依赖"底座，避免再踩 Aidlite ABI 断裂。

## 1. 组件实况

| 组件 | 角色 | 本机版本 | aid-pkg 仓库最新 | C++ API | Python API |
|---|---|---|---|---|---|
| aidgen-sdk | 生成式 AI C++ SDK | **2.4.3.166（已装）** | 2.4.3.166 | ✅ aidgen.hpp | ❌ |
| aidgen-qnn236 | AidGen 后端(QNN2.36) | **2.4.0.139（已装）** | 2.4.0.139 | — | — |
| aidgen-qnn240 | AidGen 后端(QNN2.40) | **2.4.3.166（已装）** | 2.4.3.166 | — | — |
| aidgen-qnn248 | AidGen 后端(QNN2.48) | 未装 | 2.4.3.166 | — | — |
| aidgense (aidllm) | 生成式 AI HTTP 服务 | **3.3.0（已装）** | 3.3.0 | —(HTTP) | HTTP |
| aidlite-sdk | AI 推理 SDK | **2.4.1.278（ABI 0x2040100）** | **2.5.0.284（ABI 0x2050000）** | ✅ | ✅ |
| aidlite-qnn236 | aidlite QNN2.36 插件 | **2.4.1.278（已装）** | 2.4.1.278（**无 2.5 新版**） | — | — |
| aidlite-qnn231 | aidlite QNN2.31 插件 | 未装 | 2.3.1.240 | — | — |
| aidlite-qnn240/248 | aidlite 插件 | 未装 | 2.5.0.284 | — | — |
| aidvoice-sdk | 语音 C++ SDK | 未装 | **1.4.2** | ✅ | 🚧计划 |

SDK 安装位置（aidgen-sdk 已装，官方文件）：
- 头文件：`/usr/local/include/aidlux/aidgen/aidgen.hpp`
- 库：`/usr/local/lib/libaidgen.so.2.4.3`
- 示例 + API 文档：`/usr/local/share/aidgen/{examples, docs/api_reference.md}`

## 2. 已踩的坑：aidgense 3.3.0 与 Aidlite ABI

- aidgense 3.3.0（2026-09-04 构建）要求 Aidlite SDK ABI **0x2050000**
- 本机 aidlite-sdk 2.4.1.278（2026-07-30）ABI **0x2040100** → `aidllm start/restart api` 直接 exit 255
- **修复候选**：`sudo aid-pkg -i aidlite-sdk -v 2.5.0.284`
- **风险点**：aidlite-qnn236 插件没有 2.5 版 → 需实测 2.5.0.284 SDK 能否继续加载 2.4.1.278 的 qnn236 插件（本项目的 face_det/food 模型都是 qnn236）

## 3. 模型 ↔ 运行时对照（nutrition_robot 需要）

| 模型 | 精度/QNN | 运行时 | 当前状态 |
|---|---|---|---|
| Lightweight-Face-Detection | INT8 QNN2.31 / FP16 QNN2.36 | aidlite-qnn236/231 | qnn236 插件已装 ✅ |
| YOLOv8s-worldv2 (food) | FP16/INT8 QNN2.36 | aidlite-qnn236 | 插件已装 ✅ |
| SenseVoiceSmall / Whisper* | FP16 QNN2.40 | aidvoice-sdk + aidlite-qnn240 | **需 root 安装** |
| MeloTTS-Chinese/English | FP16 QNN2.31 | aidvoice-sdk + aidlite-qnn231 | **需 root 安装** |
| Qwen3-1.7B (cl4096) | W4A16 QNN2.36 | aidgen-qnn236（已装）或 aidgense | 走 aidgen C++ 直连候选 |
| qwen3-8b-cl16384 | W4A16 QNN2.36 | aidgen-qnn236 / aidgense | 同上，aidgense 需先修 ABI |

## 4. 底座建议（C++ 版默认）

1. **LLM：优先 AidGen C++ SDK 直连**（aidgen-sdk 2.4.3.166 + aidgen-qnn236 2.4.0.139 均已装，qwen3-1.7b 就是 qnn236 cl4096 → 可能绕开 aidgense 的 ABI 问题，待实测）
2. **视觉：aidlite C++ API + aidlite-qnn236（现状即可用）**
3. **语音：aidvoice-sdk 1.4.2（官方 C++，唯一路径）**，依赖 aidlite-qnn240/qnn231 需 root 一起装
4. HTTP 服务（aidgense）仅作调试/旁路，不作为产品依赖

## 5. 实证结论（2026-09-08 aidgen C++ 直连实测）

官方示例 `test_aidgen_t2t` + `qwen3-1.7b-htp.json` + `qnn236` 后端：

```
license: the device is licensed, license ID: aidb9fd...        ← 授权 OK
[context.cpp:246] [AIDGEN] Version too low.
  Required minimum: 2.4.3, Actual: 2.4.0.                     ← 卡在这
FATAL: Context::initialize() failed: NOT_INITIALIZED (-2)
```

- aidgen 伞库 `libaidgen.so` 2.4.3.166 要求后端库 ≥ 2.4.3
- 已装 qnn236 后端 `libaidgen_qnn236.so` = **2.4.0（最后版本，仓库无新版）** → **qnn236 编译的 LLM 在 2.4.3 SDK 上无法运行**
- 与 aidlite 侧同构：aidlite-qnn236 插件止步 2.4.1.278、无 2.5 新版

**结论：qnn236 模型线（qwen3-1.7b cl4096 / qwen3-8b-cl16384 等）在当前官方运行时上已全线退役，
唯一出路是换 qnn240/248 编译的新模型，或改走 llamacpp/GGUF（CPU）路线。**

## 6. 待 root 实测清单（修订）

```bash
sudo aid-pkg update
# A. aidgense 恢复（Aidlite SDK 2.5）+ qnn240/248 模型才能跑；注意：
#    qnn236 插件无 2.5 版 → 升级前先确认 aidlite 2.5 SDK 能否加载 2.4.1 qnn236 插件
sudo aid-pkg -i aidlite-sdk -v 2.5.0.284
# B. 语音 SDK（MeloTTS qnn2.31 + SenseVoice qnn2.40 的运行时依赖随包安装）
sudo aid-pkg -i aidvoice-sdk
# C. aidgen C++ 直连改用 qnn240 后端 + qnn240 模型（当前 aplux 文本 LLM 多为 qnn236，需等官方补 qnn240 版）
```

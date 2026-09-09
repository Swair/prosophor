# 模型来源约束（决策记录，2026-09-08）

**原则：模型一律走官方渠道——Model Farm (`mms`) / aid-pkg / aidllm aplux 仓库。
禁止 HF(huggingface/hf-mirror)、GitHub 个人仓库等第三方下载。**

## 当前缺口：人脸识别（ArcFace 512-d 特征）

现状：
- `scripts/download_models.sh` 从 `hf-mirror.com/onnx-community/arcface-onnx` 拉取 —— **违反约束，已否决**
- Model Farm 全量检索结果：只有
  Lightweight-Face-Detection / MediaPipe-Face-Detection / HRNetFace /
  Facial-Attribute-Detection / Facial-Landmark-Detection —— **均无人脸特征(embedding)模型**
- aidllm aplux 仓库只有 bge 文本 embedding，无 face embedding

## 结论与过渡方案

1. **第一阶段（C++ 移植）以官方模型为准，重新定义能力边界**：
   - 人脸**在场检测**：Lightweight-Face-Detection（官方，QNN2.31/2.36）✅
   - **身份识别（谁在吃）**：官方渠道暂无可用模型 → 降级为"在场人数 + 注册照片存档"，身份个性化（禁忌/营养按人）在有人脸特征模型前不接入规则引擎
2. **保留接口，不保留实现**：`MemberRepository::find_match` 接口照常移植，实现留空并注释"待 Model Farm 上架 face-embedding 模型"
3. **若身份识别是硬需求**：等待 Model Farm 上架，或用官方 AI 模型转换工具链（AidLux AI 模型转换 Python SDK，官方）自研转换——不作为当前依赖

## 官方模型完整映射（C++ 版规划）

| 能力 | 模型 | 获取 |
|---|---|---|
| 人脸在场 | Lightweight-Face-Detection INT8 | `mms get -m Lightweight-Face-Detection ...` |
| 菜品识别 | YOLOv8s-worldv2 | `mms get`（open-vocab，提示词限定菜类） |
| ASR | SenseVoiceSmall / Whisper-small | `mms get`（AidVoice SDK 加载） |
| TTS | MeloTTS-Chinese | `mms get`（AidVoice SDK 加载） |
| LLM | Qwen3-1.7B（QNN-HTP cl4096，已在本机） | aidllm pull（aplux 仓库） |

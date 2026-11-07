// AidGen LLM Provider：本地 NPU 推理直连（官方 aidgen-sdk）
//
// 多态接入 prosophor LLM 生态 —— 与 llamacpp_provider 同构：
//   class AidGenProvider : public LLMProvider     （参照 llamacpp_provider.h）
// 实现的纯虚接口：ChatStream / GetProviderName / GetSupportedModels /
// Serialize / Deserialize / CreateHeaders / PrintRequestLog；
// 生命周期扩展（同 llamacpp）：Load(异步)/Unload/IsLoaded/IsLoading/GetLoadError。
//
// 文本与多模态同属一个 class：由请求内容分流（带 image 块走视觉链路，
// 否则走纯文本链路），装配差异只在配置里 —— 不为两种请求各立一套类型。
//
// 说明：
//   - 本地 NPU 推理，不走 HTTP：CreateHeaders/PrintRequestLog 为空实现，
//     Chat() 为本地直跑（不调用基类 HTTP 模板）。
//   - 流式语义：逐段 kContentDelta，首段前 kContentStart、结束 kContentEnd。
//   - 工具调用/思考块暂不支持（aidgen 模型）—— request 中相关字段忽略。
//
// aidgen 调用流（官方示例 test_aidgen_t2t.cpp / qwen25vl_test.cpp）：
//   Context::create_instance(config, props, backend) -> initialize()
//   -> Generator::create_instance(ctx) -> initialize() -> run(prompt | tensor, cb)
//
// SDK 隔离：aidgen.hpp / aidlite.hpp / opencv 只在本 .cc 出现（自包含，同
// llamacpp 之于 llama.h），头文件仅以不透明句柄引用 —— 非 nutribot 形态靠
// CMake 过滤本 .cc 即可在无 SDK 环境编译。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/config.h"        // AidGenModelConfig —— 与 llamacpp_cfg 同处一等配置
#include "providers/llm/llm_provider.h"

namespace prosophor {

// aidgen 运行时句柄（Context + Generator，定义于 .cc）
struct AidGenCore;
// 视觉编码器句柄（aidlite 解释器 + 预处理缓冲，定义于 .cc）
struct AidGenVisionEncoder;

class AidGenProvider : public LLMProvider {
public:
    explicit AidGenProvider(AidGenModelConfig cfg);
    ~AidGenProvider() override;

    std::string GetProviderName() const override { return "aidgen"; }
    std::vector<std::string> GetSupportedModels() const override;
    std::string Serialize(const ChatRequest& request) const override;
    ChatResponse Deserialize(const std::string& text) const override;
    ChatResponse ChatStream(const ChatRequest& request,
                            std::function<void(StreamEvent, std::string)> callback) override;

    /// 本地直跑对话（不走 HTTP 基类模板）
    ChatResponse Chat(const ChatRequest& request) override;

    // ── 生命周期（同 llamacpp：构造即异步装载） ──
    void Load() override;
    void Unload();
    bool IsLoaded() const override;
    bool IsLoading() const override;
    std::string GetLoadError() const override;
    void ResetConversation();   // 清空 KV Cache / 对话状态

    /// 当前装配的模型配置是否含 vlm_model 段（即是否为多模态模型）。
    /// 与编码器是否装载成功无关 —— 后者失败原因见 GetLoadError()。
    bool HasVisionConfig() const override { return has_vision_config_; }

protected:
    // 本地无 HTTP：空实现
    HeaderList CreateHeaders(const ChatRequest& request) const override;
    void PrintRequestLog(const ChatRequest& request) const override;

private:
    // 编码产物：纯文本走 text，含图请求走拼装好的 embedding 张量。
    // error 非空表示编码失败 —— 不可静默退化，否则图文请求看似成功却只用到了文本。
    struct EncodedPrompt {
        std::string text;              // 文本路径：完整 ChatML
        std::vector<uint8_t> tensor;   // 视觉路径：段/图/段/图… 全量张量
        std::vector<std::string> image_paths;   // 本轮参与张量的图（按序列顺序）
        bool use_tensor = false;
        std::string error;
    };

    // 视觉编码缓存：历史中的旧图每轮都要进全量张量，不缓存就得逐轮重编码。
    // 键为 路径|mtime|大小 —— 图片被替换后自动失效。条目数超限即整体清空，
    // 不做 LRU：单会话的图数远低于上限，省掉淘汰簿记。
    struct VisionCache {
        std::unordered_map<std::string, std::vector<uint8_t>> entries;
        static constexpr size_t kMaxEntries = 16;
    };

    // ── 装载链路 ──
    void DoLoad();                      // 后台线程入口：装配核心 + 视觉编码器
    bool LoadCore(std::string& error_msg);
    bool LoadVisionEncoder(std::string& error_msg);
    void Release();

    // ── ChatStream 大步骤 ──
    bool WaitForModelReady(ChatResponse& response) const;
    std::string BuildPrompt(const ChatRequest& request) const;
    EncodedPrompt EncodePrompt(const ChatRequest& request,
                               const std::string& prompt) const;
    /// 取单张图的视觉 embedding：命中缓存则直接复用，否则解码→编码→入缓存。
    /// 历史里的旧图每轮都要进全量张量，缓存是必需品而非优化。
    bool EncodeImage(const std::string& image_path,
                     const AidGenCore& core,
                     AidGenVisionEncoder& encoder,
                     std::vector<uint8_t>& out_embedding) const;
    bool RunInference(const EncodedPrompt& encoded,
                      ChatResponse& response,
                      std::function<void(StreamEvent, std::string)>& callback);
    void FinalizeResponse(ChatResponse& response, const ChatRequest& request) const;

    AidGenModelConfig cfg_;

    std::atomic<bool> loaded_{false};
    std::atomic<bool> loading_{false};
    std::atomic<bool> has_vision_config_{false};
    std::string load_error_msg_;
    mutable std::mutex model_mutex_;
    std::future<void> load_future_;

    std::shared_ptr<AidGenCore> core_;
    std::unique_ptr<AidGenVisionEncoder> vision_encoder_;
    // 上次 run 时的图片列表：图集合变化即须清 KV（见 RunInference 注释）
    std::vector<std::string> last_run_image_paths_;   // 受 model_mutex_ 保护
    // 缓存对 const 方法（EncodePrompt）可写 —— 逻辑 const，physical mutable
    mutable VisionCache vision_cache_;   // 受 model_mutex_ 保护
};

}  // namespace prosophor

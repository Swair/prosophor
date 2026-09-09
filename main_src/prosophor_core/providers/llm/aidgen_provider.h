// AidGen LLM Provider：本地 NPU 文本模型直连（官方 aidgen-sdk）
//
// 多态接入 prosophor LLM 生态 —— 与 llamacpp_provider 同构：
//   class AidGenProvider : public LLMProvider     （参照 llamacpp_provider.h:26）
// 实现的纯虚接口：ChatStream / GetProviderName / GetSupportedModels /
// Serialize / Deserialize / CreateHeaders / PrintRequestLog；
// 生命周期扩展（同 llamacpp）：Load(异步)/Unload/IsLoaded/IsLoading/GetLoadError。
//
// 说明：
//   - 本地 NPU 推理，不走 HTTP：CreateHeaders/PrintRequestLog 为空实现，
//     Chat() 为本地直跑（不调用基类 HTTP 模板）。
//   - 流式语义：逐段 kContentDelta，首段前 kContentStart、结束 kContentEnd。
//   - 工具调用/思考块暂不支持（aidgen 文本模型）—— request 中相关字段忽略。
//
// aidgen 调用流（官方示例 test_aidgen_t2t.cpp）：
//   Context::create_instance(config, props, backend) -> initialize()
//   -> Generator::create_instance(ctx) -> initialize() -> run(prompt, cb)
#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "providers/llm/llm_provider.h"

namespace prosophor {

// AidGen 后端选择（与 aidllm --qnn_ver 的 236/240/248 对应）
// auto = 系统自动择优（QNN248 > QNN240 > QNN236，见 aidgen.hpp 注释）
enum class AidGenBackend { Auto, Qnn236, Qnn240, Qnn248 };

// 解析后端字符串："auto"|"" -> Auto, "qnn236"|"236" -> Qnn236 ...
AidGenBackend parse_aidgen_backend(const std::string& s);

// AidGen 模型配置（对标 aidllm 使用的模型配置 JSON，如 qwen3-1.7b-htp.json）
struct AidGenModelConfig {
    std::string config_file;      // 模型配置 JSON 路径（ctx-bins/tokenizer 相对其所在目录）
    AidGenBackend backend = AidGenBackend::Auto;
};

// 内部实现状态（定义于 aidgen_provider.cc，避免头文件依赖 aidgen SDK）
struct AidGenState;

// 参照: class LlamacppProvider : public LLMProvider
class AidGenProvider : public LLMProvider {
public:
    explicit AidGenProvider(AidGenModelConfig cfg);
    ~AidGenProvider() override;

    // ---- LLMProvider 纯虚接口 ----
    std::string GetProviderName() const override { return "aidgen"; }
    std::vector<std::string> GetSupportedModels() const override;
    std::string Serialize(const ChatRequest& request) const override;
    ChatResponse Deserialize(const std::string& text) const override;
    ChatResponse ChatStream(const ChatRequest& request,
                            std::function<void(StreamEvent, std::string)> callback) override;

    // 本地直跑对话（不走 HTTP 基类模板；内部 = Serialize -> Generate -> Deserialize）
    ChatResponse Chat(const ChatRequest& request);

    // ---- 生命周期（同 llamacpp_provider 模式） ----
    void Load();                    // 异步加载（内部 std::async）
    void Unload();
    bool IsLoaded() const;
    bool IsLoading() const;
    std::string GetLoadError() const;
    void ResetConversation();       // 清空 KV Cache / 对话状态

protected:
    // 本地无 HTTP：空实现（头文件无 curl 依赖面；实现见 .cc）
    HeaderList CreateHeaders(const ChatRequest& request) const override;
    void PrintRequestLog(const ChatRequest& request) const override;

private:
    // 在后台线程执行真实加载；返回错误信息（空=成功）
    std::string DoLoad();
    void Release();

    AidGenModelConfig cfg_;
    mutable std::mutex mu_;
    std::atomic<bool> loaded_{false};
    std::atomic<bool> loading_{false};
    std::string load_error_;
    std::future<std::string> load_future_;

    // aidgen 句柄（仅加载线程与 Generate 线程访问；Generate 前须已加载完成）
    std::shared_ptr<AidGenState> state_;
};

}  // namespace prosophor

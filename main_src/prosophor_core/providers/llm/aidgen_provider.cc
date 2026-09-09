// AidGenProvider 实现 —— prosophor::LLMProvider 多态（对齐 llamacpp_provider）
// 本地 NPU 推理：不走 HTTP；工具/思考块暂不支持（文本模型）。
#include "providers/llm/aidgen_provider.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <thread>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aidlux/aidgen/aidgen.hpp"

#include "common/log_wrapper.h"

namespace prosophor {

using namespace aplux::aidgen;

// ---------------------------------------------------------------------------
// 内部状态：持有 aidgen Context / Generator 句柄
// ---------------------------------------------------------------------------
struct AidGenState {
    std::shared_ptr<Context> ctx;              // aidgen 上下文（单例资源）
    std::unique_ptr<Generator> generator;      // 生成器（KV cache 复用）
};

namespace {

BackendType to_backend_type(AidGenBackend b) {
    switch (b) {
        case AidGenBackend::Qnn236: return BackendType::QNN236;
        case AidGenBackend::Qnn240: return BackendType::QNN240;
        case AidGenBackend::Qnn248: return BackendType::QNN248;
        case AidGenBackend::Auto:
        default:                   return BackendType::DEFAULT;
    }
}

const char* error_code_name(ErrorCode ec) {
    switch (ec) {
        case ErrorCode::SUCCESS:             return "SUCCESS";
        case ErrorCode::INVALID_PARAM:       return "INVALID_PARAM";
        case ErrorCode::NOT_INITIALIZED:     return "NOT_INITIALIZED";
        case ErrorCode::ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
        case ErrorCode::OUT_OF_MEMORY:       return "OUT_OF_MEMORY";
        case ErrorCode::UNSUPPORTED:         return "UNSUPPORTED";
        case ErrorCode::ABORTED:             return "ABORTED";
        case ErrorCode::FILE_NOT_FOUND:      return "FILE_NOT_FOUND";
        default:                             return "UNKNOWN";
    }
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ---------------------------------------------------------------------------
// Qwen ChatML 提示词构建（原 providers/llm/prompt.h，已并入本 provider）
// ---------------------------------------------------------------------------
enum class QwenRole { System, User, Assistant };

struct QwenTurn {
    QwenRole role;
    std::string text;
};

std::string QwenRoleTag(QwenRole r) {
    switch (r) {
        case QwenRole::System:    return "system";
        case QwenRole::User:      return "user";
        case QwenRole::Assistant: return "assistant";
    }
    return "user";
}

// system + 多轮 turns -> 完整 ChatML 提示词；
// 最后一轮为 user 时自动补 assistant 起始头（供模型续写，不留 im_end）
std::string BuildQwenPrompt(const std::string& system,
                              const std::vector<QwenTurn>& turns) {
    constexpr const char* im_start = "<|im_start|>";
    constexpr const char* im_end = "<|im_end|>";
    std::string out;
    if (!system.empty()) {
        out += im_start;
        out += "system\n";
        out += system;
        out += im_end;
        out += "\n";
    }
    bool last_is_user = false;
    for (const auto& t : turns) {
        out += im_start;
        out += QwenRoleTag(t.role);
        out += '\n';
        out += t.text;
        out += im_end;
        out += '\n';
        last_is_user = (t.role == QwenRole::User);
    }
    if (last_is_user) {
        out += im_start;
        out += "assistant\n";
    }
    return out;
}

// ChatRequest(messages) -> Qwen ChatML 文本 prompt
// 本地文本语义：
//   - request.system 各条文本顺序拼接为 system 段
//   - messages 中仅 role=user/assistant 参与（MessageSchema::text() 已滤 tool 块）
std::string BuildPrompt(const ChatRequest& request) {
    std::string system;
    for (const auto& s : request.system) {
        if (!system.empty()) system += "\n";
        system += s.text;
    }
    std::vector<QwenTurn> turns;
    for (const auto& m : request.messages) {
        if (m.role == "user") {
            turns.push_back({QwenRole::User, m.text()});
        } else if (m.role == "assistant") {
            turns.push_back({QwenRole::Assistant, m.text()});
        }
        // 其余 role 本实现忽略：文本模型无工具调用
    }
    return BuildQwenPrompt(system, turns);
}

}  // namespace

// ---------------------------------------------------------------------------

AidGenBackend parse_aidgen_backend(const std::string& s) {
    const std::string v = to_lower(s);
    if (v == "qnn236" || v == "236") return AidGenBackend::Qnn236;
    if (v == "qnn240" || v == "240") return AidGenBackend::Qnn240;
    if (v == "qnn248" || v == "248") return AidGenBackend::Qnn248;
    return AidGenBackend::Auto;   // "" / "auto" / 未知一律自动择优
}

AidGenProvider::AidGenProvider(AidGenModelConfig cfg) : cfg_(std::move(cfg)) {
    Load();   // 单模型实例=使用意图：构造即异步加载（幂等，重复调用忽略）
}

AidGenProvider::~AidGenProvider() {
    Release();
}

// ---- LLMProvider 接口 ----

std::vector<std::string> AidGenProvider::GetSupportedModels() const {
    // 单模型实例：由配置 JSON 指定
    return {cfg_.config_file};
}

std::string AidGenProvider::Serialize(const ChatRequest& request) const {
    return BuildPrompt(request);
}

ChatResponse AidGenProvider::Deserialize(const std::string& text) const {
    ChatResponse resp;            // 本地文本语义：整段文本 -> content_text
    resp.AddText(text);
    resp.stop_reason = "end_turn";
    return resp;
}

bool AidGenProvider::IsLoaded() const { return loaded_.load(); }
bool AidGenProvider::IsLoading() const { return loading_.load(); }

std::string AidGenProvider::GetLoadError() const {
    std::lock_guard<std::mutex> lk(mu_);
    return load_error_;
}

void AidGenProvider::Load() {
    std::lock_guard<std::mutex> lk(mu_);
    if (loaded_.load() || loading_.load()) return;   // 已加载 / 加载中：忽略
    loading_.store(true);
    load_error_.clear();
    load_future_ = std::async(std::launch::async, [this]() {
        const std::string err = DoLoad();
        {
            std::lock_guard<std::mutex> l2(mu_);
            load_error_ = err;
        }
        loading_.store(false);
        return err;
    });
}

// 后台线程执行：创建 Context(单例) + Generator；返回错误串（空=成功）
std::string AidGenProvider::DoLoad() {
    set_log_level(LogLevel::ERROR);   // 减少无关打印；aidgen 错误自带日志

    // 模型配置中的 tokenizer/ctx-bins 为相对路径，以进程 cwd 为基准解析：
    // 配置先绝对化，加载期间临时 chdir 到其目录，结束（成功/失败）即恢复
    const auto orig_cwd = std::filesystem::current_path();
    const std::filesystem::path cfg_path =
        std::filesystem::absolute(cfg_.config_file);
    if (cfg_path.has_parent_path() && !cfg_path.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::current_path(cfg_path.parent_path(), ec);
    }
    struct CwdGuard {
        std::filesystem::path orig;
        ~CwdGuard() {
            std::error_code ec;
            std::filesystem::current_path(orig, ec);
        }
    } cwd_guard{orig_cwd};

    ContextProperties props;
    props.stream = true;
    props.enable_profiler = false;
    props.enable_prompt_cache = true;
    auto ctx = Context::create_instance(cfg_path.string(), props,
                                        to_backend_type(cfg_.backend));
    if (!ctx) {
        return "Context::create_instance 失败: 配置不存在或已有 Context 实例";
    }
    ErrorCode ec = ctx->initialize();
    if (ec != ErrorCode::SUCCESS) {
        return "Context::initialize() failed: " + std::string(error_code_name(ec));
    }

    auto gen = Generator::create_instance(ctx);
    if (!gen) {
        return "Generator::create_instance 失败";
    }
    ec = gen->initialize();
    if (ec != ErrorCode::SUCCESS) {
        return "Generator::initialize() failed: " + std::string(error_code_name(ec));
    }

    auto st = std::make_shared<AidGenState>();
    st->ctx = std::move(ctx);
    st->generator = std::move(gen);
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_ = std::move(st);
        loaded_.store(true);
    }
    return "";
}

void AidGenProvider::Release() {
    std::lock_guard<std::mutex> lk(mu_);
    state_.reset();
    loaded_.store(false);
}

void AidGenProvider::Unload() {
    if (loading_.load()) {
        if (load_future_.valid()) load_future_.wait();   // 等加载结束再释放
    }
    Release();
}

void AidGenProvider::ResetConversation() {
    std::shared_ptr<AidGenState> st;
    {
        std::lock_guard<std::mutex> lk(mu_);
        st = state_;
    }
    if (st && st->generator) {
        st->generator->reset();   // 清空 KV Cache / 对话状态
    }
}

ChatResponse AidGenProvider::Chat(const ChatRequest& request) {
    return ChatStream(request, nullptr);
}

ChatResponse AidGenProvider::ChatStream(
    const ChatRequest& request,
    std::function<void(StreamEvent, std::string)> callback) {
    ChatResponse result;
    // 异步加载通常于构造后数秒完成；首条消息可能更早，等待就绪（上限 180s）
    for (int i = 0; i < 900 && IsLoading(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!IsLoaded()) {
        result.error_msg = "模型未加载: " + GetLoadError();
        return result;
    }
    std::shared_ptr<AidGenState> st;
    {
        std::lock_guard<std::mutex> lk(mu_);
        st = state_;
    }
    if (!st || !st->generator) {
        result.error_msg = "aidgen 状态无效";
        return result;
    }

    const std::string prompt = BuildPrompt(request);

    struct Accumulator {
        ChatResponse* out;
        std::function<void(StreamEvent, std::string)> cb;
        bool sent_start = false;
    } acc{&result, std::move(callback)};

    GenCallback cb = [](const GenEvent& event, void* user_data) {
        auto* a = static_cast<Accumulator*>(user_data);
        switch (event.state) {
            case GenState::BEGIN:
                if (!a->sent_start) {
                    a->sent_start = true;
                    if (a->cb) a->cb(StreamEvent::kContentStart, "");
                }
                break;
            case GenState::CONTINUE:
                if (!a->sent_start) {       // 兜底：无 BEGIN 也先补 Start
                    a->sent_start = true;
                    if (a->cb) a->cb(StreamEvent::kContentStart, "");
                }
                if (a->cb) a->cb(StreamEvent::kContentDelta, event.text);
                a->out->content_text += event.text;
                break;
            case GenState::END:
                if (a->cb) a->cb(StreamEvent::kContentEnd, "");
                a->out->stop_reason = "end_turn";
                break;
            case GenState::ABORT:
                a->out->stop_reason = "abort";
                break;
            case GenState::ERROR:
                a->out->error_msg = "aidgen 生成错误";
                break;
            default:
                break;
        }
    };

    const ErrorCode ec = st->generator->run(prompt, cb, &acc);
    if (ec != ErrorCode::SUCCESS) {
        result.error_msg = "run() failed: " + std::string(error_code_name(ec));
    }
    if (result.stop_reason.empty() && result.error_msg.empty()) {
        result.stop_reason = "end_turn";   // run 返回后未收到 END 的兜底
    }
    return result;
}

// ---- 本地无 HTTP（照 llamacpp_provider 先例：显式空实现）----
HeaderList AidGenProvider::CreateHeaders(const ChatRequest&) const {
    LOG_DEBUG("[aidgen] CreateHeaders (local NPU, no HTTP)");
    return {};
}

void AidGenProvider::PrintRequestLog(const ChatRequest&) const {
    LOG_DEBUG("[aidgen] PrintRequestLog (local NPU, no HTTP)");
}

}  // namespace prosophor

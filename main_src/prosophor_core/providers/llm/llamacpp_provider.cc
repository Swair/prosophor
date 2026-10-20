// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/llm/llamacpp_provider.h"
#include "common/log_wrapper.h"
#include "common/thread_pool.h"
#include "common/time_wrapper.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "common/reasoning-budget.h"
#include "common/chat-auto-parser.h"
#pragma GCC diagnostic pop

// Redirect llama.cpp log output to spdlog so it doesn't flood the TUI terminal.
static void llama_log_to_spdlog(ggml_log_level level,
                                 const char*    text,
                                 void*          /*user_data*/) {
    if (!text || text[0] == '\0') return;
    std::string msg(text);
    if (!msg.empty() && msg.back() == '\n') msg.pop_back();
    if (msg.empty()) return;

    switch (level) {
        case GGML_LOG_LEVEL_ERROR: LOG_ERROR("[llama] {}", msg); break;
        case GGML_LOG_LEVEL_WARN:  LOG_WARN ("[llama] {}", msg); break;
        default:                   LOG_DEBUG("[llama] {}", msg); break;
    }
}

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

namespace prosophor {

static ggml_type kv_cache_type_from_string(const std::string& s) {
    if (s == "q8_0") return GGML_TYPE_Q8_0;
    if (s == "f16")  return GGML_TYPE_F16;
    return GGML_TYPE_Q4_0;
}

LlamacppProvider::LlamacppProvider(const LlamacppModelConfig& cfg)
    : cfg_(cfg) {}

LlamacppProvider::~LlamacppProvider() {
    if (load_future_.valid()) {
        load_future_.wait();
    }
    Release();
}

void LlamacppProvider::Load() {
    bool expected = false;
    if (!loading_.compare_exchange_strong(expected, true)) {
        LOG_WARN("[local] Load already in progress");
        return;
    }

    if (loaded_) {
        loading_ = false;
        return;
    }

    // 为了初始化不阻塞，异步加载模型。首次 Chat 时会等待加载完成。
    load_future_ = GetGlobalThreadPool().SubmitWithFuture([this]() {
        try {
            const std::string& path = cfg_.model_path;
            if (path.empty()) {
                LOG_ERROR("[local] model_path is empty");
                load_error_msg_ = "model_path is empty";
                loading_ = false;
                return;
            }

            llama_log_set(llama_log_to_spdlog, nullptr);

            LOG_INFO("[local] Loading model: {}", path);
            auto t0 = SteadyClock::Now();

            llama_model_params mparams = llama_model_default_params();
            mparams.n_gpu_layers = cfg_.n_gpu_layers;

            // Route MoE expert weights to CPU to save VRAM (needed for large MoE models
            // like Qwen 35B-A3B on 8GB GPUs; no effect on dense models).
            llama_model_tensor_buft_override moe_overrides[2] = {};
            if (cfg_.cpu_moe) {
                moe_overrides[0] = llm_ffn_exps_cpu_override();
                mparams.tensor_buft_overrides = moe_overrides;
                LOG_INFO("[local] MoE experts routed to CPU (cpu_moe=true)");
            }
            if (cfg_.no_mmap) {
                mparams.use_mmap = false;
                LOG_INFO("[local] mmap disabled (no_mmap=true)");
            }

            std::lock_guard<std::mutex> lock(model_mutex_);
            model_ = llama_model_load_from_file(path.c_str(), mparams);
            if (!model_) {
                LOG_ERROR("[local] Failed to load: {}", path);
                load_error_msg_ = "Failed to load model: " + path;
                loading_ = false;
                return;
            }
            vocab_ = llama_model_get_vocab(model_);

            llama_context_params cparams = llama_context_default_params();
            cparams.n_ctx           = static_cast<uint32_t>(cfg_.context_window);
            cparams.n_threads       = cfg_.threads > 0 ? cfg_.threads : 8;
            cparams.n_threads_batch = cfg_.n_threads_batch > 0
                                      ? cfg_.n_threads_batch
                                      : std::max(cparams.n_threads * 4, 32);
            cparams.n_batch         = static_cast<uint32_t>(cfg_.n_batch);
            cparams.n_ubatch        = static_cast<uint32_t>(cfg_.n_ubatch);
            cparams.type_k          = kv_cache_type_from_string(cfg_.type_k);
            cparams.type_v          = kv_cache_type_from_string(cfg_.type_v);
            cparams.offload_kqv     = cfg_.offload_kqv;
            cparams.flash_attn_type = cfg_.flash_attn
                                      ? LLAMA_FLASH_ATTN_TYPE_ENABLED
                                      : LLAMA_FLASH_ATTN_TYPE_DISABLED;
            ctx_ = llama_init_from_model(model_, cparams);
            if (!ctx_) {
                LOG_ERROR("[local] Failed to create context");
                load_error_msg_ = "Failed to create context";
                Release();
                loading_ = false;
                return;
            }

            llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
            sampler_ = llama_sampler_chain_init(sparams);
            if (cfg_.min_p > 0.0f) {
                llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(cfg_.min_p, 1));
            }
            llama_sampler_chain_add(sampler_, llama_sampler_init_temp(cfg_.temperature));
            llama_sampler_chain_add(sampler_,
                cfg_.seed >= 0
                    ? llama_sampler_init_dist(static_cast<uint32_t>(cfg_.seed))
                    : llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

            // Initialize chat template for proper prompt rendering
            chat_tmpls_ = common_chat_templates_init(model_, "");
            if (chat_tmpls_) {
                LOG_DEBUG("[local] Chat template initialized");
            }

            // Detect thinking/reasoning markers from the model's chat template.
            // Falls back to configured reason_start/reason_end if auto-detection fails.
            try {
                const char* tmpl_str = llama_model_chat_template(model_, nullptr);
                if (tmpl_str && tmpl_str[0]) {
                    char buf[64];
                    int n;
                    n = llama_token_to_piece(vocab_, llama_vocab_bos(vocab_),
                                             buf, (int32_t)sizeof(buf), 0, true);
                    std::string bos(n > 0 ? std::string(buf, (size_t)n) : "");
                    n = llama_token_to_piece(vocab_, llama_vocab_eos(vocab_),
                                             buf, (int32_t)sizeof(buf), 0, true);
                    std::string eos(n > 0 ? std::string(buf, (size_t)n) : "");

                    common_chat_template chat_tmpl(tmpl_str, bos, eos);
                    autoparser::analyze_reasoning reasoning(chat_tmpl, /*supports_tools=*/false);

                    if (reasoning.mode != autoparser::reasoning_mode::NONE) {
                        thinking_start_ = reasoning.start;
                        thinking_end_   = reasoning.end;
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN("[local] Reasoning marker detection failed: {}", e.what());
            }

            // Configured markers override auto-detection
            if (!cfg_.thinking_start.empty()) { thinking_start_ = cfg_.thinking_start; }
            if (!cfg_.thinking_end.empty())   { thinking_end_   = cfg_.thinking_end;   }

            thinking_enabled_ = !thinking_start_.empty();

            LOG_INFO("[local] Reasoning markers: start='{}' end='{}'",
                     thinking_start_, thinking_end_);

            loaded_ = true;
            loading_ = false;
            auto ms = SteadyClock::ElapsedMillis(t0);
            LOG_INFO("[local] Loaded in {} ms  n_gpu_layers={}", ms, cfg_.n_gpu_layers);
        } catch (const std::exception& e) {
            LOG_ERROR("[local] Load exception: {}", e.what());
            load_error_msg_ = e.what();
            loading_ = false;
        }
    });

    return true;
}

void LlamacppProvider::Unload() {
    if (load_future_.valid()) {
        load_future_.wait();
    }
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        Release();
    }
    loaded_ = false;
    loading_ = false;
    load_error_msg_.clear();
    LOG_INFO("[local] Model unloaded");
}

void LlamacppProvider::Release() {
    if (sampler_) { llama_sampler_free(sampler_); sampler_ = nullptr; }
    if (ctx_)     { llama_free(ctx_);              ctx_     = nullptr; }
    if (model_)   { llama_model_free(model_);      model_   = nullptr; }
    vocab_ = nullptr;
}

bool LlamacppProvider::IsLoaded() const { return loaded_; }
bool LlamacppProvider::IsLoading() const { return loading_; }
std::string LlamacppProvider::GetLoadError() const { return load_error_msg_; }

// ── Prompt 步骤 ───────────────────────────────────────────────────────
//
// 纯数据变换，不依赖类成员，故为文件内 static。只有 BuildChatParams 调用，
// 不进头文件。

// ── BuildSystemPrompt ────────────────────────────────────────────────

static void BuildSystemPrompt(std::vector<common_chat_msg>& out,
                              const std::vector<SystemSchema>& system) {
    for (const auto& s : system) {
        if (s.text.empty()) { continue; }
        common_chat_msg m;
        m.role    = "system";
        m.content = s.text;
        out.push_back(std::move(m));
    }
}

// ── BuildMessagePrompt ──────────────────────────────────────────────

static void BuildMessagePrompt(std::vector<common_chat_msg>& out,
                               const std::vector<MessageSchema>& messages) {
    for (const auto& m : messages) {
        common_chat_msg cm;
        cm.role = m.role;
        bool has_structured = false;
        for (const auto& b : m.content) {
            if (b.type == "text" || b.type == "thinking") {
                cm.content += b.text;
            } else if (b.type == "tool_use") {
                common_chat_tool_call tc;
                tc.name      = b.name;
                tc.arguments = b.input.dump();
                tc.id        = b.tool_use_id;
                cm.tool_calls.push_back(std::move(tc));
                has_structured = true;
            } else if (b.type == "tool_result") {
                cm.tool_call_id = b.tool_use_id;
                cm.content      = b.content;
                cm.role         = "tool";
                has_structured = true;
            }
        }
        if (!cm.empty() || has_structured) {
            out.push_back(std::move(cm));
        }
    }
}

// ── BuildToolsPrompt ─────────────────────────────────────────────────

static void BuildToolsPrompt(std::vector<common_chat_tool>& out,
                             const std::vector<ToolsSchema>& tools) {
    for (const auto& t : tools) {
        common_chat_tool ct;
        ct.name        = t.name;
        ct.description = t.description;
        ct.parameters  = t.input_schema.dump();
        out.push_back(std::move(ct));
    }
}

common_chat_params LlamacppProvider::BuildChatParams(const ChatRequest& request) const {
    common_chat_params result;
    if (!model_) return result;

    common_chat_templates_inputs inputs;
    inputs.add_generation_prompt = true;
    inputs.enable_thinking       = request.thinking;
    inputs.use_jinja             = true;
    inputs.add_bos               = false;

    BuildSystemPrompt(inputs.messages, request.system);
    BuildMessagePrompt(inputs.messages, request.messages);
    if (!request.tools.empty()) {
        BuildToolsPrompt(inputs.tools, request.tools);
        inputs.tool_choice = request.tool_choice_auto
            ? COMMON_CHAT_TOOL_CHOICE_AUTO
            : COMMON_CHAT_TOOL_CHOICE_REQUIRED;
    }

    if (chat_tmpls_) {
        try {
            result = common_chat_templates_apply(chat_tmpls_.get(), inputs);
        } catch (const std::exception& e) {
            LOG_WARN("[local] common_chat_templates_apply failed: {}", e.what());
        }
    }

    if (result.prompt.empty()) {
        result.prompt = BuildFallbackPrompt(inputs.messages);
    }


    return result;
}

// ── TokenizePrompt ────────────────────────────────────────────────────

bool LlamacppProvider::TokenizePrompt(const std::string& prompt, int max_tokens,
                                       std::vector<llama_token>& out_tokens,
                                       int& out_n_tokens, std::string& error_msg) const {
    const int n_vocab_tokens = llama_vocab_n_tokens(vocab_);
    out_tokens.resize(std::max(static_cast<int>(prompt.size()), n_vocab_tokens));
    int n_prompt = llama_tokenize(
        vocab_,
        prompt.c_str(), static_cast<int32_t>(prompt.size()),
        out_tokens.data(), static_cast<int32_t>(out_tokens.size()),
        /*add_special=*/true,
        /*parse_special=*/true);
    if (n_prompt < 0) {
        error_msg = "Tokenize failed";
        return false;
    }
    out_tokens.resize(static_cast<size_t>(n_prompt));
    out_n_tokens = n_prompt;

    uint32_t n_ctx_max = llama_n_ctx(ctx_);
    uint32_t n_reserve = static_cast<uint32_t>(max_tokens > 0 ? max_tokens : cfg_.max_tokens);
    if (static_cast<uint32_t>(n_prompt) + n_reserve > n_ctx_max) {
        int allowed = static_cast<int>(n_ctx_max - n_reserve);
        if (allowed > 0) {
            int trim = n_prompt - allowed;
            LOG_WARN("[local] Prompt {} tokens exceeds available context ({}), trimming {} tokens from head",
                     n_prompt, n_ctx_max - n_reserve, trim);
            out_tokens.erase(out_tokens.begin(),
                             out_tokens.begin() + std::min(trim, n_prompt - 1));
            out_n_tokens = static_cast<int>(out_tokens.size());
        } else {
            error_msg = "Prompt exceeds context window even with zero reserve";
            return false;
        }
    }
    return true;
}

void LlamacppProvider::PrintRequestLog(const ChatRequest& request) const {
    LOG_DEBUG("=== [local] Request ===");
    LOG_DEBUG("Model: {}", cfg_.model_path);
    LOG_DEBUG("Max tokens: {}", request.max_tokens);
    LOG_DEBUG("Temperature: {}", request.temperature);
    LOG_DEBUG("Thinking: {}, budget_tokens: {}", request.thinking, request.thinking_budget_tokens);
    LOG_DEBUG("Reasoning effort: {}", request.reasoning_effort);
    LOG_DEBUG("Messages count: {}", request.messages.size());
    LOG_DEBUG("System blocks: {}", request.system.size());
    LOG_DEBUG("Tools count: {}", request.tools.size());
    LOG_DEBUG("Streaming: {}", request.stream);
    LOG_DEBUG("Context window: {}  n_gpu_layers: {}", cfg_.context_window, cfg_.n_gpu_layers);
    LOG_DEBUG("n_batch: {}  threads: {}", cfg_.n_batch, cfg_.threads);
}

HeaderList LlamacppProvider::CreateHeaders(const ChatRequest&) const { return {}; }

std::string LlamacppProvider::Serialize(const ChatRequest&) const {
    LOG_DEBUG("[local] Serialize called (stub)");
    return {};
}

ChatResponse LlamacppProvider::Deserialize(const std::string&) const {
    LOG_DEBUG("[local] Deserialize called (stub)");
    return {};
}

bool LlamacppProvider::PrefillPrompt(
    std::vector<llama_token>& prompt_tokens,
    int n_prompt, ChatResponse& response)
{
    uint32_t batch_max = llama_n_batch(ctx_);
    LOG_DEBUG("[local] Prefill: {} tokens (n_batch={})", n_prompt, batch_max);

    int32_t batch_sz = static_cast<int32_t>(batch_max);
    for (int32_t i = 0; i < n_prompt; i += batch_sz) {
        int32_t n_tokens = std::min(batch_sz, n_prompt - i);
        llama_batch batch = llama_batch_get_one(prompt_tokens.data() + i, n_tokens);
        if (llama_decode(ctx_, batch) != 0) {
            response.error_msg = "llama_decode (prefill) failed";
            return false;
        }
    }
    return true;
}

// ── GenerateReply ─────────────────────────────────────────────────────
//
// Token-by-token generation loop with:
//  - Pending buffer to handle thinking markers that span BPE subword boundaries
//  - <end_of_turn>/<start_of_turn> detection for Gemma-style models
//  - Thinking/content separation via markers from chat template

ChatResponse LlamacppProvider::GenerateReply(
    const ChatRequest& request,
    std::function<void(StreamEvent, std::string)>& callback,
    int n_prompt)
{
    const int max_new = request.max_tokens > 0 ? request.max_tokens : cfg_.max_tokens;
    ChatResponse response;

    bool  in_thinking   = false;
    bool  in_tool_call  = false;
    bool  response_done = false;
    int   n_generated   = 0;
    std::string tool_call_buffer;

    callback(StreamEvent::kContentStart, {});

    char  token_frame[256];

    auto NextToken = [&](llama_token& out_token, std::string& out_text) -> bool {
        out_token = llama_sampler_sample(sampler_, ctx_, -1);
        if (llama_vocab_is_eog(vocab_, out_token)) {
            LOG_DEBUG("[local] EOG token {} hit, stopping (n_generated={})", out_token, n_generated);
            return false;
        }
        int n = llama_token_to_piece(vocab_, out_token, token_frame,
                                     static_cast<int32_t>(sizeof(token_frame)),
                                     /*lstrip=*/0, /*special=*/true);
        out_text.assign(token_frame, static_cast<size_t>(n > 0 ? n : 0));
        return true;
    };

    auto UpdateKvCache = [&](llama_token t) -> bool {
        llama_batch batch = llama_batch_get_one(&t, 1);
        if (llama_decode(ctx_, batch) != 0) { return false; }
        ++n_generated;
        return true;
    };

    LOG_DEBUG("[local] Starting generation loop with max_new={} tokens", max_new);
    while (n_generated < max_new) {
        llama_token token;
        std::string token_frame_str;
        if (!NextToken(token, token_frame_str)) break;

        if (!token_frame_str.empty()) {
            // LOG_INFO("n_generated={}, token_frame_str={}", n_generated, token_frame_str);
            if (ProcessTokenFrame(token_frame_str,
                                  in_thinking, in_tool_call, response_done,
                                  tool_call_buffer, response, callback) && response_done) {
                    break;
            }
        }

        if (!UpdateKvCache(token)) { break; }
    }

    // Finalize
    if (!response_done) {
        if (in_thinking) { callback(StreamEvent::kThinkingEnd, {}); }
        else if (in_tool_call) { callback(StreamEvent::kToolEnd, {}); }
        else          { callback(StreamEvent::kContentEnd, {}); }
    }

    // Parse tool calls from buffered text
    if (!tool_call_buffer.empty()) {
        LOG_INFO("[local] Finalizing tool call from buffer: '{}'", tool_call_buffer);
        ParseToolCalls(response, tool_call_buffer);
    }

    response.usage.prompt_tokens     = n_prompt;
    response.usage.completion_tokens = n_generated;
    response.usage.total_tokens      = n_prompt + n_generated;

    response.stop_reason = (n_generated >= max_new) ? "max_tokens" : "stop";
    if (!response.tool_calls.empty()) {
        response.stop_reason = "tool_calls";
    }

    LOG_DEBUG("[local] Done: {} tokens generated", n_generated);
    return response;
}

// ── Stream ──────────────────────────────────────────────────

bool LlamacppProvider::ProcessTokenFrame(
    const std::string& raw,
    bool& in_thinking, bool& in_tool_call, bool& response_done,
    std::string& tool_call_buffer,
    ChatResponse& response,
    std::function<void(StreamEvent, std::string)>& callback)
{
    // --- end_of_turn / start_of_turn ---
    if (!cfg_.end_of_turn.empty() && raw.find(cfg_.end_of_turn) != std::string::npos) {
        if (!in_tool_call) { callback(StreamEvent::kContentEnd, {}); }
        response_done = true;
        return true;
    }
    if (!cfg_.start_of_turn.empty() && raw.find(cfg_.start_of_turn) != std::string::npos) {
        if (!in_tool_call) { callback(StreamEvent::kContentStart, {}); }
        response_done = true;
        return true;
    }

    // --- tool call end marker ---
    if (in_tool_call && !cfg_.tool_call_end.empty() &&
        raw.find(cfg_.tool_call_end) != std::string::npos) {
        in_tool_call = false;
        callback(StreamEvent::kToolEnd, {});
        LOG_INFO("[local] Exiting tool use mode");
        return true;
    }

    // --- tool call start marker ---
    if (!in_tool_call && !cfg_.tool_call_start.empty() &&
        raw.find(cfg_.tool_call_start) != std::string::npos) {
        in_tool_call = true;
        tool_call_buffer.clear();
        // Emit ContentEnd if there was content before tool call
        if (!response.content_text.empty() || !response.content_thinking.empty()) {
            callback(StreamEvent::kContentEnd, {});
        }
        callback(StreamEvent::kToolStart, {});
        LOG_INFO("[local] Entering tool use mode");
        return true;
    }

    // --- buffering tool call content ---
    if (in_tool_call) {
        tool_call_buffer += raw;
        callback(StreamEvent::kToolDelta, {});
        // LOG_INFO("[local] Buffering tool_call_buffer: '{}'", raw);
        return true;
    }

    // --- thinking start marker ---
    if (thinking_enabled_ && !in_thinking && !thinking_start_.empty() &&
        raw.find(thinking_start_) != std::string::npos) {
        if (!response.content_text.empty() || !response.content_thinking.empty()) {
            callback(StreamEvent::kContentEnd, {});
        }
        callback(StreamEvent::kThinkingStart, {});
        in_thinking = true;
        LOG_DEBUG("[local] Entering thinking mode");
        return true;
    }

    // --- thinking end marker ---
    if (thinking_enabled_ && in_thinking && !thinking_end_.empty() &&
        raw.find(thinking_end_) != std::string::npos) {
        callback(StreamEvent::kThinkingEnd, {});
        callback(StreamEvent::kContentStart, {});
        in_thinking = false;
        LOG_DEBUG("[local] Exiting thinking mode");
        return true;
    }

    // Normal text: emit directly based on current mode
    LOG_DEBUG("[local] Generated token frame: '{}'", raw);
    if (!raw.empty()) {
        if (in_thinking) {
            response.AppendThinking(raw);
            callback(StreamEvent::kThinkingDelta, raw);
        } else {
            response.AppendText(raw);
            callback(StreamEvent::kContentDelta, raw);
        }
    }
    return false;
}

// ── ChatStream ─────────────────────────────────────────────────────────
//
// Build prompt, tokenize, prefill, then generate tokens.
// Prompt is built via common_chat_templates_apply with thinking support.
// Thinking markers are extracted from the template result.

ChatResponse LlamacppProvider::ChatStream(
    const ChatRequest& request,
    std::function<void(StreamEvent, std::string)> callback) {

    ChatResponse response;

    if (!loaded_) {
        response.error_msg = "Model not loaded";
        return response;
    }

    // Step 1: Build prompt via compiled chat template
    auto chat_params = BuildChatParams(request);
    std::string prompt = chat_params.prompt;

    if (prompt.empty()) {
        response.error_msg = "Empty prompt";
        return response;
    }
    PrintRequestLog(request);

    LOG_DEBUG("[local] Prompt[0:400]={}", prompt.substr(0, 400));

    // Step 2: Tokenize
    std::vector<llama_token> prompt_tokens;
    int n_prompt = 0;
    if (!TokenizePrompt(prompt, request.max_tokens, prompt_tokens, n_prompt, response.error_msg)) {
        return response;
    }

    // Step 3: Prefill KV cache
    llama_memory_clear(llama_get_memory(ctx_), /*data=*/true);
    auto t_prefill = SteadyClock::Now();
    if (!PrefillPrompt(prompt_tokens, n_prompt, response))
        return response;
    LOG_DEBUG("[local] Prefill done in {} ms", SteadyClock::ElapsedMillis(t_prefill));

    // Step 4: Generate
    return GenerateReply(request, callback, n_prompt);
}

ChatResponse LlamacppProvider::Chat(const ChatRequest& request) {
    return ChatStream(request, [](StreamEvent, std::string) {});
}

// ── BuildFallbackPrompt ─────────────────────────────────────────────

std::string LlamacppProvider::BuildFallbackPrompt(
    const std::vector<common_chat_msg>& messages) const {
    struct RenderedMsg { std::string role; std::string content; };
    auto render = [](const common_chat_msg& cm) -> RenderedMsg {
        if (!cm.tool_calls.empty()) {
            std::string content;
            for (const auto& tc : cm.tool_calls) {
                if (!content.empty()) content += "\n";
                content += "<|tool_call>call:" + tc.name + "{";
                auto args = nlohmann::json::parse(tc.arguments);
                bool first = true;
                for (auto& [key, val] : args.items()) {
                    if (!first) content += ",";
                    first = false;
                    // Numbers and booleans don't need <|"|> quotes
                    if (val.is_number() || val.is_boolean()) {
                        content += key + ":" + val.dump();
                    } else {
                        content += key + ":<|\"|>" + val.dump() + "<|\"|>";
                    }
                }
                content += "}<tool_call|>";
            }
            return {"assistant", std::move(content)};
        }
        if (!cm.tool_call_id.empty()) {
            return {"tool", "<|tool_result|>" + cm.content + "<tool_result|>"};
        }
        return {cm.role, cm.content};
    };

    std::vector<RenderedMsg> rendered;
    std::vector<llama_chat_message> msgs;
    rendered.reserve(messages.size());
    msgs.reserve(messages.size());
    for (const auto& cm : messages) {
        auto r = render(cm);
        if (r.content.empty()) { continue; }
        rendered.push_back(std::move(r));
        msgs.push_back({rendered.back().role.c_str(),
                        rendered.back().content.c_str()});
    }

    const char* tmpl = llama_model_chat_template(model_, /*name=*/nullptr);
    if (!tmpl) {
        std::string path_lower = cfg_.model_path;
        std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);
        if (path_lower.find("gemma") != std::string::npos) {
            tmpl = llama_model_chat_template(model_, "gemma");
        }
    }

    if (tmpl) LOG_DEBUG("[local] Chat template: {}", tmpl);

    std::vector<char> buf(8192);
    int n = -1;
    if (tmpl) {
        n = llama_chat_apply_template(
            tmpl,
            msgs.data(), static_cast<size_t>(msgs.size()),
            /*add_ass=*/true,
            buf.data(), static_cast<int32_t>(buf.size()));
        if (n > static_cast<int>(buf.size())) {
            buf.resize(static_cast<size_t>(n) + 1);
            n = llama_chat_apply_template(
                tmpl, msgs.data(), static_cast<size_t>(msgs.size()),
                true, buf.data(), static_cast<int32_t>(buf.size()));
        }
    }

    std::string result;
    if (n < 0) {
        std::string path_lower = cfg_.model_path;
        std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);

        if (path_lower.find("gemma") != std::string::npos) {
            for (const auto& r : rendered) {
                auto role = (r.role == "assistant") ? "model" : r.role;
                result += "<start_of_turn>" + role + "\n" + r.content + "<end_of_turn>\n";
            }
            result += "<start_of_turn>model\n";
        }

        if (result.empty()) {
            LOG_WARN("[local] llama_chat_apply_template failed, using raw concat");
            for (const auto& r : rendered)
                result += "<|" + r.role + "|>\n" + r.content + "\n";
            result += "<|assistant|>\n";
        }
    } else {
        result = std::string(buf.data(), static_cast<size_t>(n));
    }
    return result;
}

// ── ParseToolCalls ────────────────────────────────────────────────────

void LlamacppProvider::ParseToolCalls(ChatResponse& response, const std::string& tool_text) {
    // tool_text format: "call:name{key1:val1,key2:val2}" or "name{...}"
    if (tool_text.empty()) return;

    const std::string kQuote = "<|\"|>";

    std::string::size_type pos = 0;
    while (pos < tool_text.size()) {
        auto args_begin = tool_text.find('{', pos);
        if (args_begin == std::string::npos) break;

        std::string header = tool_text.substr(pos, args_begin - pos);
        auto colon = header.rfind(':');
        std::string name = (colon != std::string::npos)
                           ? header.substr(colon + 1) : header;
        name.erase(0, name.find_first_not_of(" \t\r\n"));
        name.erase(name.find_last_not_of(" \t\r\n") + 1);
        if (name.empty()) { pos = args_begin + 1; continue; }

        // Find matching close brace
        int brace_depth = 1;
        auto args_end = args_begin + 1;
        while (args_end < tool_text.size() && brace_depth > 0) {
            if (tool_text[args_end] == '{') ++brace_depth;
            else if (tool_text[args_end] == '}') --brace_depth;
            ++args_end;
        }
        if (brace_depth != 0) break;
        --args_end;

        std::string args_block = tool_text.substr(args_begin + 1, args_end - args_begin - 1);
        if (args_block.empty()) { pos = args_end + 1; continue; }

        // Parse key:value pairs (supports <|"|> quoted values)
        nlohmann::json args = nlohmann::json::object();
        std::string::size_type ap = 0;
        while (ap < args_block.size()) {
            while (ap < args_block.size() &&
                   (args_block[ap] == ' ' || args_block[ap] == ','))
                ++ap;
            if (ap >= args_block.size()) break;

            auto key_end = args_block.find(':', ap);
            auto qp = args_block.find(kQuote, ap);
            if (key_end == std::string::npos) break;
            if (qp != std::string::npos && qp < key_end)
                key_end = qp;

            std::string key = args_block.substr(ap, key_end - ap);
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            ap = key_end + 1;

            auto vopen = args_block.find(kQuote, ap);
            if (vopen == std::string::npos || vopen != ap) {
                // Unquoted value — parse as raw JSON literal
                auto vend = args_block.find_first_of(",}", ap);
                if (vend == std::string::npos) vend = args_block.size();
                std::string raw_val = args_block.substr(ap, vend - ap);
                raw_val.erase(0, raw_val.find_first_not_of(" \t\r\n"));
                raw_val.erase(raw_val.find_last_not_of(" \t\r\n") + 1);
                try { args[key] = nlohmann::json::parse(raw_val); }
                catch (...) { args[key] = std::move(raw_val); }
                ap = vend;
                continue;
            }
            // Quoted value: <|"|>...<|"|>
            ap = vopen + kQuote.size();
            auto vclose = args_block.find(kQuote, ap);
            if (vclose == std::string::npos) break;
            args[key] = args_block.substr(ap, vclose - ap);
            ap = vclose + kQuote.size();
        }

        response.AddToolCall(name, name, std::move(args));
        pos = args_end + 1;
    }
}

std::vector<std::string> LlamacppProvider::GetSupportedModels() const {
    if (model_) {
        char buf[256] = {};
        llama_model_desc(model_, buf, sizeof(buf));
        return {buf[0] ? std::string(buf) : cfg_.model_path};
    }
    return {cfg_.model_path};
}

}  // namespace prosophor

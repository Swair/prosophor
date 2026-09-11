// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
//
// AidGenProvider 实现 —— prosophor::LLMProvider 多态（对齐 llamacpp_provider）。
//
// 本文件是仓库内唯一直接使用 aidgen / aidlite / opencv 的翻译单元：
// 非 nutribot 形态由 CMake 过滤本 .cc，其余代码经头文件的不透明句柄引用本模块。
#include "providers/llm/aidgen_provider.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include "aidlux/aidgen/aidgen.hpp"
#include "aidlux/aidlite/aidlite.hpp"

#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "common/string_utils.h"
#include "common/thread_pool.h"

namespace prosophor {

using namespace aplux::aidgen;

// ── 小工具 ────────────────────────────────────────────────────────────

static const char* ErrorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS:             return "SUCCESS";
        case ErrorCode::INVALID_PARAM:       return "INVALID_PARAM";
        case ErrorCode::NOT_INITIALIZED:     return "NOT_INITIALIZED";
        case ErrorCode::ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
        case ErrorCode::OUT_OF_MEMORY:       return "OUT_OF_MEMORY";
        case ErrorCode::UNSUPPORTED:         return "UNSUPPORTED";
        case ErrorCode::ABORTED:             return "ABORTED";
        case ErrorCode::FILE_NOT_FOUND:      return "FILE_NOT_FOUND";
        case ErrorCode::FILE_OPEN_FAILED:    return "FILE_OPEN_FAILED";
        case ErrorCode::FILE_EMPTY:          return "FILE_EMPTY";
        case ErrorCode::FILE_READ_ERROR:     return "FILE_READ_ERROR";
        case ErrorCode::FILE_PARSE_ERROR:    return "FILE_PARSE_ERROR";
        case ErrorCode::BACKEND_ERROR:       return "BACKEND_ERROR";
        case ErrorCode::BACKEND_NOT_FOUND:   return "BACKEND_NOT_FOUND";
        case ErrorCode::INTERNAL_ERROR:      return "INTERNAL_ERROR";
        default:                             return "UNKNOWN_ERROR";
    }
}

static BackendType ToBackendType(const std::string& backend) {
    const std::string name = ToLower(backend);
    if (name == "qnn236" || name == "236") return BackendType::QNN236;
    if (name == "qnn240" || name == "240") return BackendType::QNN240;
    if (name == "qnn248" || name == "248") return BackendType::QNN248;
    return BackendType::DEFAULT;
}

// aidgen 与 aidlite 各有一套同序号的框架枚举，视觉后端须一并映射
static Aidlux::Aidlite::FrameworkType ToAidliteFramework(const std::string& backend) {
    const std::string name = ToLower(backend);
    if (name == "qnn236" || name == "236") return Aidlux::Aidlite::FrameworkType::TYPE_QNN236;
    if (name == "qnn240" || name == "240") return Aidlux::Aidlite::FrameworkType::TYPE_QNN240;
    if (name == "qnn248" || name == "248") return Aidlux::Aidlite::FrameworkType::TYPE_QNN248;
    return Aidlux::Aidlite::FrameworkType::TYPE_DEFAULT;
}

// 按内容块顺序渲染消息体：文本/思考块出文本，图像块在原位出视觉槽位。
// 用户随时给图 —— 每张图挂在它跟的那条消息上，槽位就落在该消息内。
// 纯文本消息的输出与 MessageSchema::text() 完全一致（非 VL 路径零变化）。
static std::string RenderMessageBody(const MessageSchema& message,
                                     const std::string& vision_start,
                                     const std::string& vision_end) {
    std::string body;
    for (const auto& block : message.content) {
        if (block.type == "text" || block.type == "thinking") {
            if (!body.empty()) body += "\n\n";
            body += block.text;
        } else if (block.type == "image") {
            body += vision_start + vision_end;
        }
    }
    return body;
}

// 剥掉会话控制标记（如 <|im_end|>）：模型生成 EOS token 时，SDK 会把该 token 的
// 文本一并交到事件里 —— 那是控制标记而非回答内容，不该出现在正文与 UI 中。
static void StripControlMarkers(std::string& text, const std::string& marker) {
    if (marker.empty()) return;
    for (size_t pos = text.find(marker); pos != std::string::npos;
         pos = text.find(marker, pos)) {
        text.erase(pos, marker.size());
    }
}

// 请求里的全部图片路径，按消息顺序、块顺序展开 —— 与提示词中视觉槽位一一对应
static std::vector<std::string> CollectImagePaths(const ChatRequest& request) {
    std::vector<std::string> paths;
    for (const auto& message : request.messages) {
        for (const auto& block : message.content) {
            if (block.type != "image") continue;
            if (block.input.contains("path")) {
                paths.push_back(block.input["path"].get<std::string>());
            }
        }
    }
    return paths;
}

// 缓存键：路径 + mtime + 大小 —— 图片被替换后自动失效
static std::string VisionCacheKey(const std::string& path) {
    std::error_code stat_error;
    const auto write_time = std::filesystem::last_write_time(path, stat_error);
    if (stat_error) return path;
    const auto size = std::filesystem::file_size(path, stat_error);
    if (stat_error) return path;
    return path + "|" + std::to_string(write_time.time_since_epoch().count()) +
           "|" + std::to_string(size);
}

// 张量缓冲的释放器。TensorDeleter 是 void(*)(uint8_t*)，与 std::free 的
// void(*)(void*) 类型不同，须经此包装才能传入。
static void FreeTensorBuffer(uint8_t* data) { std::free(data); }

// 把 embedding 字节流转成 SDK 张量。
// 必须显式给 deleter：Tensor 的 data 是裸 unique_ptr，传 nullptr 会在析构时
// 调用空函数指针而崩溃（头文件"默认删除器自动释放"说的是 SDK 自造张量的内部约定，
// 不适用于调用方直接构造）。
static Tensor ToTensor(const std::vector<uint8_t>& bytes) {
    Tensor tensor;
    tensor.size = bytes.size();
    tensor.data = std::unique_ptr<uint8_t[], TensorDeleter>(
        static_cast<uint8_t*>(std::malloc(bytes.size())), FreeTensorBuffer);
    if (tensor.data) {
        std::memcpy(tensor.data.get(), bytes.data(), bytes.size());
    }
    return tensor;
}

// ── aidgen 运行时句柄 ─────────────────────────────────────────────────

struct AidGenCore {
    std::shared_ptr<Context>   context;
    std::unique_ptr<Generator> generator;
    std::unique_ptr<Tokenizer> tokenizer;   // 仅视觉链路需要（文本段分词）

    const char* embedding_weights      = nullptr;  // 词嵌入查找表（Generator 持有）
    size_t      embedding_weights_size = 0;        // 查找表字节数
    size_t      embedding_size         = 0;        // 单个 token 的 float 数

    // 按 token 查表拼出连续 embedding 字节流
    bool LookupTokenEmbeddings(const std::vector<int32_t>& tokens,
                               std::vector<uint8_t>& out) const {
        if (embedding_weights == nullptr || embedding_size == 0) return false;
        const size_t token_bytes = embedding_size * sizeof(float);
        out.resize(tokens.size() * token_bytes);
        for (size_t i = 0; i < tokens.size(); ++i) {
            std::memcpy(out.data() + i * token_bytes,
                        embedding_weights + static_cast<size_t>(tokens[i]) * token_bytes,
                        token_bytes);
        }
        return true;
    }
};

// 按张量名设置输入。aide 按索引的接口需要预知模型输入顺序，且名称转索引仍需
// 2.5.0+ 的 input_tensor_name_to_index（板上 aidlite 2.4.1 无）—— 故沿用官方
// 例程的按名调用，在此把其弃用告警收口到一处。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static int32_t SetAidliteInput(Aidlux::Aidlite::Interpreter* interpreter,
                               const char* name,
                               void* data) {
    return interpreter->set_input_tensor(name, data);
}
#pragma GCC diagnostic pop

// ── 视觉编码器 ────────────────────────────────────────────────────────
//
// 官方 aidmlm 例程配方：aidlite 载入 Qwen-VL 视觉塔，opencv 解码缩放，
// CHW 归一化 → 双帧堆叠 → 时空 patch，输出视觉 embedding 字节流。

struct AidGenVisionEncoder {
    // 模型参数（来自 aidgen 配置 JSON 的 vlm_model 段）
    size_t img_h = 392;
    size_t img_w = 392;
    size_t img_c = 3;
    size_t frame_count = 2;
    size_t patch_size = 14;
    size_t temporal_patch_size = 2;
    size_t merge_size = 2;

    // 衍生参数
    size_t img_area = 0;
    size_t img_elem_size = 0;
    size_t grid_step = 0;

    size_t embedding_size = 0;

    bool available = false;
    std::unique_ptr<Aidlux::Aidlite::Interpreter> interpreter;

    // 预分配缓冲，避免每帧重新分配
    std::vector<float> chw_data;
    std::vector<float> cthw_data;
    std::vector<float> input_data;

    bool Initialize(const std::string& model_path,
                    const std::string& pos_cos_path,
                    const std::string& pos_sin_path,
                    const std::string& window_mask_path,
                    const std::string& full_mask_path,
                    Aidlux::Aidlite::FrameworkType framework);
    bool Encode(const cv::Mat& image, std::vector<uint8_t>& out_embedding);

private:
    bool CheckModel(const Aidlux::Aidlite::TensorInfo& tensor) const;
    bool Preprocess(const cv::Mat& image);
    bool Postprocess(float* data, uint32_t total_bytes, int vec_len) const;
    void NormalizeToChw(const cv::Mat& image);
    void StackFrames();
    void SpatioTemporalPatch();

    // Qwen-VL 图像归一化常数
    static constexpr double normal_scale[3] = {255.0, 255.0, 255.0};
    static constexpr double mean[3] = {0.48145466, 0.4578275, 0.40821073};
    static constexpr double std_dev[3] = {0.26862954, 0.26130258, 0.27577711};
};

bool AidGenVisionEncoder::Initialize(const std::string& model_path,
                                     const std::string& pos_cos_path,
                                     const std::string& pos_sin_path,
                                     const std::string& window_mask_path,
                                     const std::string& full_mask_path,
                                     Aidlux::Aidlite::FrameworkType framework) {
    img_area      = img_h * img_w;
    img_elem_size = img_area * img_c;
    grid_step     = patch_size * merge_size;

    chw_data.resize(img_elem_size);
    cthw_data.resize(img_elem_size * frame_count);
    input_data.resize(img_elem_size * frame_count);

    auto* model = Aidlux::Aidlite::Model::create_instance(model_path);
    auto* config = Aidlux::Aidlite::Config::create_instance();
    config->implement_type = Aidlux::Aidlite::ImplementType::TYPE_LOCAL;
    config->framework_type = framework;
    config->accelerate_type = Aidlux::Aidlite::AccelerateType::TYPE_DSP;
    interpreter = Aidlux::Aidlite::InterpreterBuilder::build_interpreter_from_model_and_config(
        model, config);
    if (!interpreter) {
        LOG_ERROR("[aidgen] 视觉编码器解释器构建失败: {}", model_path);
        return false;
    }
    if (interpreter->init() < 0 || interpreter->load_model() < 0) {
        LOG_ERROR("[aidgen] 视觉编码器 init/load_model 失败（aidlite 后端版本不匹配?）");
        interpreter.reset();
        return false;
    }

    // 张量信息查询会随 init() 一起失败（如 aidlite 后端版本门拦截），
    // 此时返回的是空 vector —— 必须先验空再取 [0]，否则越界崩溃。
    std::vector<std::vector<Aidlux::Aidlite::TensorInfo>> in_tensors;
    if (interpreter->get_input_tensor_info(in_tensors) < 0 ||
        in_tensors.empty() || in_tensors[0].empty()) {
        LOG_ERROR("[aidgen] 视觉编码器未返回输入张量（aidlite 后端版本不匹配?）");
        interpreter.reset();
        return false;
    }
    bool found_pixel_values = false;
    for (const auto& tensor : in_tensors[0]) {
        if (tensor.name != "pixel_values") continue;
        found_pixel_values = true;
        if (!CheckModel(tensor)) {
            interpreter.reset();
            return false;
        }
    }
    if (!found_pixel_values) {
        LOG_ERROR("[aidgen] 视觉编码器缺少 pixel_values 输入张量");
        interpreter.reset();
        return false;
    }

    std::vector<std::vector<Aidlux::Aidlite::TensorInfo>> out_tensors;
    if (interpreter->get_output_tensor_info(out_tensors) < 0 ||
        out_tensors.empty() || out_tensors[0].empty() ||
        out_tensors[0][0].shape.size() < 2) {
        LOG_ERROR("[aidgen] 视觉编码器未返回有效输出张量（aidlite 后端版本不匹配?）");
        interpreter.reset();
        return false;
    }
    embedding_size = out_tensors[0][0].shape[1];

    std::vector<float> pos_cos, pos_sin, window_mask, full_mask;
    if (!LoadRawFloats(pos_cos_path, pos_cos) ||
        !LoadRawFloats(pos_sin_path, pos_sin) ||
        !LoadRawFloats(window_mask_path, window_mask) ||
        !LoadRawFloats(full_mask_path, full_mask)) {
        interpreter.reset();
        return false;
    }
    SetAidliteInput(interpreter.get(), "position_ids_cos", pos_cos.data());
    SetAidliteInput(interpreter.get(), "position_ids_sin", pos_sin.data());
    SetAidliteInput(interpreter.get(), "window_attention_mask", window_mask.data());
    SetAidliteInput(interpreter.get(), "full_attention_mask", full_mask.data());

    available = true;
    return true;
}

// 张量形状与配置的 patch 参数必须自洽 —— 不符说明配置取错了模型
bool AidGenVisionEncoder::CheckModel(const Aidlux::Aidlite::TensorInfo& tensor) const {
    int num_patches = 0;
    int patch_area = 0;
    int valid_dims = 0;
    for (auto dim : tensor.shape) {
        if (dim <= 1) continue;
        if (valid_dims == 0) num_patches = static_cast<int>(dim);
        else if (valid_dims == 1) patch_area = static_cast<int>(dim);
        ++valid_dims;
    }
    const int expected_num_patches = static_cast<int>(img_area / (patch_size * patch_size));
    const int expected_patch_area =
        static_cast<int>(img_c * frame_count * patch_size * patch_size);
    if (num_patches != expected_num_patches || patch_area != expected_patch_area) {
        LOG_ERROR("[aidgen] pixel_values 形状不符: patches={} (期望 {}), patch_area={} (期望 {})",
                  num_patches, expected_num_patches, patch_area, expected_patch_area);
        return false;
    }
    return true;
}

bool AidGenVisionEncoder::Encode(const cv::Mat& image, std::vector<uint8_t>& out_embedding) {
    if (!available || image.empty()) return false;
    if (image.cols != static_cast<int>(img_w) ||
        image.rows != static_cast<int>(img_h) ||
        image.channels() != static_cast<int>(img_c)) {
        LOG_ERROR("[aidgen] 图像尺寸不符: 实际 {}x{}x{}, 期望 {}x{}x{}",
                  image.cols, image.rows, image.channels(), img_w, img_h, img_c);
        return false;
    }
    if (!Preprocess(image)) return false;

    SetAidliteInput(interpreter.get(), "pixel_values", input_data.data());
    if (interpreter->invoke() < 0) {
        LOG_ERROR("[aidgen] 视觉编码 invoke 失败");
        return false;
    }

    float* buffer = nullptr;
    uint32_t length = 0;
    interpreter->get_output_tensor(0u, reinterpret_cast<void**>(&buffer),
                                   /*is_native=*/false, &length);
    if (buffer == nullptr || length == 0) {
        LOG_ERROR("[aidgen] 视觉编码输出为空（aidlite 后端版本不匹配?）");
        return false;
    }
    if (!Postprocess(buffer, length, static_cast<int>(embedding_size))) return false;

    out_embedding.resize(length);
    std::memcpy(out_embedding.data(), buffer, length);
    return true;
}

bool AidGenVisionEncoder::Preprocess(const cv::Mat& image) {
    NormalizeToChw(image);
    StackFrames();
    SpatioTemporalPatch();
    return true;
}

bool AidGenVisionEncoder::Postprocess(float* data, uint32_t total_bytes, int vec_len) const {
    if (data == nullptr || total_bytes == 0 || vec_len <= 0) return false;
    const int num_elements = static_cast<int>(total_bytes / sizeof(float));
    const int num_tokens = num_elements / vec_len;
    for (int token = 0; token < num_tokens; ++token) {
        float* vector = data + token * vec_len;
        double sum = 0.0;
        for (int i = 0; i < vec_len; ++i) sum += static_cast<double>(vector[i]);
        const double average = sum / vec_len;
        for (int i = 0; i < vec_len; ++i) vector[i] = static_cast<float>(vector[i] - average);
    }
    return true;
}

void AidGenVisionEncoder::NormalizeToChw(const cv::Mat& image) {
    for (size_t c = 0; c < img_c; ++c) {
        float* dst = chw_data.data() + c * img_area;
        for (size_t h = 0; h < img_h; ++h) {
            const uchar* row = image.ptr<uchar>(static_cast<int>(h));
            for (size_t w = 0; w < img_w; ++w) {
                const float source = static_cast<float>(row[w * img_c + c]);
                *dst++ = static_cast<float>(
                    ((source / normal_scale[c]) - mean[c]) / std_dev[c]);
            }
        }
    }
}

// 单帧复制成 T 帧，构成 c,T,h,w 布局
void AidGenVisionEncoder::StackFrames() {
    for (size_t c = 0; c < img_c; ++c) {
        float* dst = cthw_data.data() + c * temporal_patch_size * img_area;
        for (size_t t = 0; t < temporal_patch_size; ++t) {
            std::memcpy(dst + t * img_area, chw_data.data() + c * img_area,
                        img_area * sizeof(float));
        }
    }
}

// 按 (merge_size × merge_size) 网格切 patch，逐 patch 内按 c,T 展开
void AidGenVisionEncoder::SpatioTemporalPatch() {
    float* dst = input_data.data();
    const float* src = cthw_data.data();
    const size_t patch_bytes = patch_size * sizeof(float);
    const size_t channel_frame_total = img_c * temporal_patch_size;

    for (size_t row = 0; row < img_h; row += grid_step) {
        for (size_t col = 0; col < img_w; col += grid_step) {
            for (size_t merge_y = 0; merge_y < merge_size; ++merge_y) {
                const size_t patch_y = row + merge_y * patch_size;
                for (size_t merge_x = 0; merge_x < merge_size; ++merge_x) {
                    const size_t patch_x = col + merge_x * patch_size;
                    const size_t spatial_offset = patch_y * img_w + patch_x;
                    for (size_t ct = 0; ct < channel_frame_total; ++ct) {
                        const float* src_ptr = src + ct * img_area + spatial_offset;
                        for (size_t r = 0; r < patch_size; ++r) {
                            std::memcpy(dst, src_ptr + r * img_w, patch_bytes);
                            dst += patch_size;
                        }
                    }
                }
            }
        }
    }
}

// ── 构造 / 析构 ───────────────────────────────────────────────────────

AidGenProvider::AidGenProvider(AidGenModelConfig cfg) : cfg_(std::move(cfg)) {
    Load();   // 构造即异步装载（同 llamacpp 的 auto-start 语义）
}

AidGenProvider::~AidGenProvider() {
    // 不主动 Unload：genie/qnn240 的 aidgen Context 析构会段错误 ——
    // 交由 OS 进程退出回收。代价是进程内反复创建 provider 会占住进程唯一的 Context。
}

// ── 装载链路 ──────────────────────────────────────────────────────────

void AidGenProvider::Load() {
    bool expected = false;
    if (!loading_.compare_exchange_strong(expected, true)) {
        LOG_WARN("[aidgen] 装载已在进行中");
        return;
    }
    if (loaded_) {
        loading_ = false;
        return;
    }

    // 为了初始化不阻塞，异步加载模型。首次 Chat 时会等待加载完成。
    load_future_ = GetGlobalThreadPool().SubmitWithFuture([this]() { DoLoad(); });
}

void AidGenProvider::DoLoad() {
    // 装载在后台线程跑：任何异常逃逸都会让 loading_ 永远为真，
    // 调用方只能等到超时且看不到原因 —— 故在此兜住并转化为错误信息。
    std::string failure;
    try {
        std::string core_error;
        if (!LoadCore(core_error)) {
            failure = core_error;
        } else {
            // 视觉链路失败不阻断文本：错误仅记录，图文请求时再单独报出
            std::string vision_error;
            if (!LoadVisionEncoder(vision_error)) {
                LOG_WARN("[aidgen] {}", vision_error);
                std::lock_guard<std::mutex> lock(model_mutex_);
                load_error_msg_ = vision_error;
            }
        }
    } catch (const std::exception& load_error) {
        failure = std::string("装载异常: ") + load_error.what();
    } catch (...) {
        failure = "装载异常: 未知错误";
    }

    if (!failure.empty()) {
        LOG_ERROR("[aidgen] 核心装载失败: {}", failure);
        std::lock_guard<std::mutex> lock(model_mutex_);
        load_error_msg_ = failure;
    }
    loading_ = false;
}

bool AidGenProvider::LoadCore(std::string& error_msg) {
    set_log_level(LogLevel::ERROR);   // 抑制 SDK 冗余输出；错误自带上报

    // 模型配置里的 tokenizer / ctx-bins 是相对路径，以进程 cwd 为基准解析：
    // 配置先绝对化，装载期间临时 chdir 到其目录，结束即恢复。
    const auto original_cwd = std::filesystem::current_path();
    const std::filesystem::path config_path =
        std::filesystem::absolute(cfg_.config_file);
    if (config_path.has_parent_path()) {
        std::error_code change_error;
        std::filesystem::current_path(config_path.parent_path(), change_error);
    }
    struct CwdGuard {
        std::filesystem::path original;
        ~CwdGuard() {
            std::error_code restore_error;
            std::filesystem::current_path(original, restore_error);
        }
    } cwd_guard{original_cwd};

    ContextProperties properties;
    properties.stream = true;
    properties.enable_profiler = false;
    properties.enable_prompt_cache = true;

    auto context = Context::create_instance(config_path.string(), properties,
                                            ToBackendType(cfg_.backend));
    if (!context) {
        error_msg = "Context::create_instance 失败：配置不存在或已有 Context 实例";
        return false;
    }
    const ErrorCode init_code = context->initialize();
    if (init_code != ErrorCode::SUCCESS) {
        error_msg = std::string("Context::initialize() 失败: ") + ErrorCodeName(init_code);
        return false;
    }

    auto generator = Generator::create_instance(context);
    if (!generator) {
        error_msg = "Generator::create_instance 失败";
        return false;
    }
    const ErrorCode generator_code = generator->initialize();
    if (generator_code != ErrorCode::SUCCESS) {
        error_msg = std::string("Generator::initialize() 失败: ") + ErrorCodeName(generator_code);
        return false;
    }
    generator->set_property("stream", "1");

    auto core = std::make_shared<AidGenCore>();
    core->context = std::move(context);
    core->generator = std::move(generator);
    // 词嵌入表 / embedding-size / 分词器三件仅视觉三段拼装需要，由 LoadVisionEncoder
    // 按需装配 —— 纯文本模型上没有 VLM 词嵌入表，此处调用只会换来一条
    // "Failed to get embedding buff" 的 SDK 报错噪声。

    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        core_ = std::move(core);
        loaded_ = true;
    }
    LOG_INFO("[aidgen] 核心已装载: {}", cfg_.config_file);
    return true;
}

// 视觉编码器按需装载：无图模型的配置里没有 vlm_model 段，属正常跳过。
bool AidGenProvider::LoadVisionEncoder(std::string& error_msg) {
    // 视觉塔按 aidgen-vl 专用配置装配；未单独给则回落文本配置
    const std::string vision_config_file =
        cfg_.vl_config_file.empty() ? cfg_.config_file : cfg_.vl_config_file;

    const auto config_json = ReadJson(vision_config_file);
    if (!config_json.has_value()) {
        error_msg = "aidgen 配置无法读取或解析失败: " + vision_config_file;
        return false;
    }
    if (!config_json->contains("vlm_model")) {
        LOG_DEBUG("[aidgen] 配置无 vlm_model 段，按纯文本模型处理");
        return true;
    }
    has_vision_config_ = true;   // 配置层面是多模态：装配失败与否由下面决定

    // 视觉三段拼装所需的三件核心侧资源：词嵌入查找表、embedding-size、分词器。
    // 先于视觉编码器装配 —— 即便编码器失败，误差也应指向编码器而非"分词器不可用"。
    std::shared_ptr<AidGenCore> core;
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        core = core_;
    }
    if (!core) {
        error_msg = "视觉装配时 aidgen 核心尚未就绪";
        return false;
    }
    core->generator->get_embedding_buff(core->embedding_weights, core->embedding_weights_size);

    // embedding-size 只出现在 Context 配置文本里，正则取出供拼装算偏移
    const std::string context_config = core->context->get_config();
    std::smatch match;
    if (std::regex_search(context_config, match,
                          std::regex(R"("embedding-size"\s*:\s*(\d+))"))) {
        core->embedding_size = static_cast<size_t>(std::stoull(match[1].str()));
    }

    auto tokenizer = Tokenizer::create_instance(core->context);
    if (!tokenizer || tokenizer->initialize() != ErrorCode::SUCCESS) {
        error_msg = "视觉链路所需的分词器装配失败";
        return false;
    }
    core->tokenizer = std::move(tokenizer);

    const auto& vlm = (*config_json)["vlm_model"];
    // 视觉塔与权重路径均相对配置 JSON 所在目录
    const std::string base_dir =
        std::filesystem::absolute(vision_config_file).parent_path().string() + "/";
    auto resolve = [&base_dir, &vlm](const std::string& name) {
        return base_dir + vlm.value(name, std::string());
    };

    auto encoder = std::make_unique<AidGenVisionEncoder>();
    // Qwen-VL 视觉参数在 vlm_model.<type> 子段（如 "qwen2.5"）
    if (vlm.contains("qwen2.5")) {
        const auto& shape = vlm["qwen2.5"];
        encoder->img_h = shape.value("img_h", encoder->img_h);
        encoder->img_w = shape.value("img_w", encoder->img_w);
        encoder->img_c = shape.value("img_c", encoder->img_c);
        encoder->frame_count = shape.value("T", encoder->frame_count);
        encoder->patch_size = shape.value("patch_size", encoder->patch_size);
        encoder->temporal_patch_size =
            shape.value("temporal_patch_size", encoder->temporal_patch_size);
        encoder->merge_size = shape.value("merge_size", encoder->merge_size);
    }

    if (!encoder->Initialize(resolve("vision_model_path"),
                             resolve("pos_embed_cos_path"),
                             resolve("pos_embed_sin_path"),
                             resolve("window_attention_mask_path"),
                             resolve("full_attention_mask_path"),
                             ToAidliteFramework(cfg_.vit_backend))) {
        error_msg = "视觉编码器不可用（aidlite 后端版本不匹配?）";
        return false;
    }

    std::lock_guard<std::mutex> lock(model_mutex_);
    vision_encoder_ = std::move(encoder);
    LOG_INFO("[aidgen] 视觉编码器已装载 ({}x{}x{}, patch={}, merge={})",
             vision_encoder_->img_w, vision_encoder_->img_h, vision_encoder_->img_c,
             vision_encoder_->patch_size, vision_encoder_->merge_size);
    return true;
}

void AidGenProvider::Unload() {
    if (load_future_.valid()) {
        load_future_.wait();
    }
    Release();
}

void AidGenProvider::Release() {
    std::lock_guard<std::mutex> lock(model_mutex_);
    vision_encoder_.reset();
    core_.reset();
    loaded_ = false;
    loading_ = false;
    load_error_msg_.clear();
    LOG_INFO("[aidgen] 已卸载");
}

bool AidGenProvider::IsLoaded() const { return loaded_; }
bool AidGenProvider::IsLoading() const { return loading_; }

std::string AidGenProvider::GetLoadError() const {
    std::lock_guard<std::mutex> lock(model_mutex_);
    return load_error_msg_;
}

void AidGenProvider::ResetConversation() {
    std::shared_ptr<AidGenCore> core;
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        core = core_;
    }
    if (core && core->generator) {
        core->generator->reset();   // 清空 KV Cache：与角色会话隔离
    }
}

// ── LLMProvider 接口 ──────────────────────────────────────────────────

std::vector<std::string> AidGenProvider::GetSupportedModels() const {
    return {cfg_.config_file};   // 单模型实例：由配置 JSON 指定
}

std::string AidGenProvider::Serialize(const ChatRequest& request) const {
    return BuildPrompt(request);
}

ChatResponse AidGenProvider::Deserialize(const std::string& text) const {
    ChatResponse response;
    response.AddText(text);
    response.stop_reason = "end_turn";
    return response;
}

HeaderList AidGenProvider::CreateHeaders(const ChatRequest&) const {
    LOG_DEBUG("[aidgen] CreateHeaders (本地 NPU，无 HTTP)");
    return {};
}

void AidGenProvider::PrintRequestLog(const ChatRequest& request) const {
    LOG_DEBUG("=== [aidgen] Request ===");
    LOG_DEBUG("Config: {}", cfg_.config_file);
    LOG_DEBUG("Backend: {}", cfg_.backend);
    LOG_DEBUG("Messages: {}  System blocks: {}", request.messages.size(), request.system.size());
    LOG_DEBUG("Max tokens: {}  Temperature: {}", request.max_tokens, request.temperature);
    LOG_DEBUG("Streaming: {}", request.stream);
}

ChatResponse AidGenProvider::Chat(const ChatRequest& request) {
    return ChatStream(request, [](StreamEvent, std::string) {});
}

// ── ChatStream ────────────────────────────────────────────────────────
//
// 步骤清单：等核心就绪 → 渲染提示词 → 编码（文本 / 视觉张量）→ 推理 → 收尾。

ChatResponse AidGenProvider::ChatStream(
    const ChatRequest& request,
    std::function<void(StreamEvent, std::string)> callback) {
    ChatResponse response;

    if (!WaitForModelReady(response)) return response;
    PrintRequestLog(request);

    const std::string prompt = BuildPrompt(request);
    if (prompt.empty()) {
        response.error_msg = "提示词为空";
        return response;
    }

    // 图集合发生变化时先清 KV。SDK 的前缀复用只认序列本身，无法感知张量里
    // 视觉段被替换 —— 会沿用上一张图的 KV，表现为模型"看见"的仍是旧图
    // （实测：换成人物图后仍回答"图里没有人"，而那恰是前一张狗图的内容）。
    // 官方 VL 例程每次 run 后都 reset()，正是绕开这一点。
    // 须在编码之前重置：实测放在编码之后仍会看到旧图。
    // 纯文本会话图集合恒为空 → 永不 reset，KV 复用与非 VL 路径原样保留。
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        const std::vector<std::string> image_paths = CollectImagePaths(request);
        if (image_paths != last_run_image_paths_) {
            if (core_ && core_->generator) core_->generator->reset();
            last_run_image_paths_ = image_paths;
        }
    }

    const EncodedPrompt encoded = EncodePrompt(request, prompt);
    if (!encoded.error.empty()) {
        response.error_msg = encoded.error;
        return response;
    }

    RunInference(encoded, response, callback);
    FinalizeResponse(response);
    return response;
}

// 步骤 1：核心异步装载完成前先等（首条消息通常早于装载完成）
bool AidGenProvider::WaitForModelReady(ChatResponse& response) const {
    constexpr int max_wait_ms = 180000;
    constexpr int poll_interval_ms = 200;
    for (int waited = 0; waited < max_wait_ms && loading_; waited += poll_interval_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }
    if (!loaded_) {
        response.error_msg = "模型未加载: " + GetLoadError();
        return false;
    }
    return true;
}

// 步骤 2：请求 → ChatML 提示词。
// 含图请求在末轮用户文本后插入视觉界标，编码阶段据此切开插视觉 embedding。
std::string AidGenProvider::BuildPrompt(const ChatRequest& request) const {
    std::string system_text;
    for (const auto& block : request.system) {
        if (block.text.empty()) continue;
        if (!system_text.empty()) system_text += "\n";
        system_text += block.text;
    }

    std::string prompt;
    if (!system_text.empty()) {
        prompt += cfg_.im_start + "system\n" + system_text + cfg_.im_end + "\n";
    }

    bool last_turn_is_user = false;
    for (const auto& message : request.messages) {
        if (message.role != "user" && message.role != "assistant") continue;

        // 每张图在它自己那一轮的槽位上 —— 用户中途换图时模型看到的才是当轮图
        prompt += cfg_.im_start + message.role + "\n" +
                  RenderMessageBody(message, cfg_.vision_start, cfg_.vision_end);
        prompt += cfg_.im_end + "\n";
        last_turn_is_user = (message.role == "user");
    }

    if (last_turn_is_user) {
        prompt += cfg_.im_start + "assistant\n";   // 供模型续写
    }
    return prompt;
}

// 步骤 3：提示词 → 可推理输入。
// 无视觉界标 = 纯文本，直接交给 SDK；有界标则切三段，中段换成视觉 embedding。
AidGenProvider::EncodedPrompt AidGenProvider::EncodePrompt(
    const ChatRequest& request,
    const std::string& prompt) const {
    EncodedPrompt encoded;

    // 收集全部视觉槽位的位置（按出现顺序）
    std::vector<size_t> slot_positions;
    for (size_t pos = prompt.find(cfg_.vision_start);
         pos != std::string::npos;
         pos = prompt.find(cfg_.vision_start, pos + cfg_.vision_start.size())) {
        slot_positions.push_back(pos);
    }
    if (slot_positions.empty()) {
        encoded.text = prompt;
        return encoded;   // 纯文本链路：与非 VL 完全一致
    }

    // 走到这里说明请求带图。此时任何一步失败都**必须报错**：
    // 退化成纯文本会让没有视觉输入的模型凭空编造图片内容，比报错危害大得多。
    std::shared_ptr<AidGenCore> core;
    AidGenVisionEncoder* encoder = nullptr;
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        core = core_;
        encoder = vision_encoder_.get();
    }
    if (!core || !core->tokenizer) {
        encoded.error = "视觉链路未就绪：aidgen 分词器不可用";
        return encoded;
    }
    if (encoder == nullptr) {
        encoded.error = GetLoadError();   // 装载期已写明原因（如 aidlite 版本门）
        return encoded;
    }

    const std::vector<std::string> image_paths = CollectImagePaths(request);
    if (image_paths.size() != slot_positions.size()) {
        encoded.error = "图片数量与提示词视觉槽位不匹配（" +
                        std::to_string(image_paths.size()) + " vs " +
                        std::to_string(slot_positions.size()) + "）";
        return encoded;
    }

    // 按槽位切段：段0 槽0 段1 槽1 … 段N，逐段取文本 embedding、逐个取视觉 embedding。
    // 关键：vision_start / vision_end 界标本身要留在待分词的文本里（官方例程同此）——
    // 视觉向量插在两个界标 token 之间，模型靠界标识别"这段是图像"。
    std::vector<std::string> text_segments;
    text_segments.reserve(slot_positions.size() + 1);
    size_t cursor = 0;
    for (size_t slot : slot_positions) {
        // 前段含 vision_start
        const size_t head_end = slot + cfg_.vision_start.size();
        text_segments.push_back(prompt.substr(cursor, head_end - cursor));
        cursor = head_end;   // 后段自 vision_end 起（下轮循环或尾部自然带上）
    }
    text_segments.push_back(prompt.substr(cursor));

    for (size_t i = 0; i < text_segments.size(); ++i) {
        std::vector<int32_t> tokens;
        if (core->tokenizer->encode(text_segments[i], tokens) != ErrorCode::SUCCESS) {
            encoded.error = "视觉提示词分词失败";
            return encoded;
        }
        std::vector<uint8_t> segment_bytes;
        if (!core->LookupTokenEmbeddings(tokens, segment_bytes)) {
            encoded.error = "文本段词嵌入查表失败";
            return encoded;
        }
        encoded.tensor.insert(encoded.tensor.end(),
                              segment_bytes.begin(), segment_bytes.end());

        if (i >= image_paths.size()) continue;
        std::vector<uint8_t> vision_embedding;
        if (!EncodeImage(image_paths[i], *core, *encoder, vision_embedding)) {
            encoded.error = "视觉编码失败: " + image_paths[i];
            return encoded;
        }
        encoded.tensor.insert(encoded.tensor.end(),
                              vision_embedding.begin(), vision_embedding.end());
    }

    encoded.image_paths = image_paths;
    encoded.use_tensor = true;
    return encoded;
}

// 单张图的视觉 embedding：优先命中缓存。历史旧图每轮都在全量张量里，
// 不缓存就要逐轮重跑一遍编码（每张几百毫秒，多轮线性累积）。
bool AidGenProvider::EncodeImage(const std::string& image_path,
                                 const AidGenCore& core,
                                 AidGenVisionEncoder& encoder,
                                 std::vector<uint8_t>& out_embedding) const {
    (void)core;   // 视觉编码走 aidlite，不经 aidgen 核心；参数保留以便日后扩展
    const std::string cache_key = VisionCacheKey(image_path);
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        const auto hit = vision_cache_.entries.find(cache_key);
        if (hit != vision_cache_.entries.end()) {
            out_embedding = hit->second;
            return true;
        }
    }

    cv::Mat image = cv::imread(image_path);
    if (image.empty()) {
        LOG_ERROR("[aidgen] 图像无法读取: {}", image_path);
        return false;
    }
    cv::cvtColor(image, image, cv::COLOR_BGR2RGB);
    cv::resize(image, image, cv::Size(static_cast<int>(encoder.img_w),
                                      static_cast<int>(encoder.img_h)),
               0.f, 0.f, cv::INTER_CUBIC);

    std::vector<uint8_t> encoded_image;
    if (!encoder.Encode(image, encoded_image)) return false;

    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        // 超限即整体清空：单会话图数远低于上限，省掉 LRU 簿记
        if (vision_cache_.entries.size() >= VisionCache::kMaxEntries) {
            vision_cache_.entries.clear();
        }
        vision_cache_.entries.emplace(cache_key, encoded_image);
    }
    out_embedding = std::move(encoded_image);
    return true;
}

// 步骤 4：把编码结果交给 SDK，边收事件边转发流式回调
bool AidGenProvider::RunInference(
    const EncodedPrompt& encoded,
    ChatResponse& response,
    std::function<void(StreamEvent, std::string)>& callback) {
    std::shared_ptr<AidGenCore> core;
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        core = core_;
    }
    if (!core || !core->generator) {
        response.error_msg = "aidgen 状态无效";
        return false;
    }

    // 图集合发生变化时必须清 KV。SDK 的前缀复用只认序列本身，无法感知张量里
    // 视觉段被替换 —— 会沿用上一张图的 KV，表现为模型"看见"的仍是旧图
    // （实测：换成人物图后仍回答"图里没有人"，而那恰是前一张狗图的内容）。
    // 官方 VL 例程每次 run 后都 reset()，正是绕开这一点。
    // 纯文本会话图集合恒为空 → 永不 reset，KV 复用与非 VL 路径原样保留。

    struct Accumulator {
        ChatResponse* response;
        std::function<void(StreamEvent, std::string)>* callback;
        bool content_started = false;
        std::string im_end_marker;   // EOS 文本随 END 事件一起到达，须剥掉
    } accumulator{&response, &callback, false, cfg_.im_end};

    auto on_event = [](const GenEvent& event, void* user_data) {
        auto* state = static_cast<Accumulator*>(user_data);
        auto& out = *state->response;
        auto& emit = *state->callback;
        auto ensure_started = [&]() {
            if (state->content_started) return;
            state->content_started = true;
            if (emit) emit(StreamEvent::kContentStart, "");
        };

        switch (event.state) {
            case GenState::BEGIN:
                ensure_started();
                if (!event.text.empty()) {
                    out.AppendText(event.text);
                    if (emit) emit(StreamEvent::kContentDelta, event.text);
                }
                break;
            case GenState::CONTINUE:
                ensure_started();
                out.AppendText(event.text);
                if (emit) emit(StreamEvent::kContentDelta, event.text);
                break;
            case GenState::END: {
                ensure_started();
                std::string tail = event.text;
                StripControlMarkers(tail, state->im_end_marker);
                if (!tail.empty()) {
                    out.AppendText(tail);
                    if (emit) emit(StreamEvent::kContentDelta, tail);
                }
                if (emit) emit(StreamEvent::kContentEnd, "");
                out.stop_reason = "end_turn";
                break;
            }
            case GenState::ABORT:
                out.stop_reason = "abort";
                break;
            case GenState::ERROR:
                out.error_msg = "aidgen 生成错误";
                break;
            case GenState::IDLE:
            default:
                break;
        }
    };

    ErrorCode code = ErrorCode::SUCCESS;
    if (encoded.use_tensor) {
        Tensor combined = ToTensor(encoded.tensor);   // 须活到 run() 返回
        code = core->generator->run(combined, on_event, &accumulator);
    } else {
        code = core->generator->run(encoded.text, on_event, &accumulator);
    }
    if (code != ErrorCode::SUCCESS) {
        response.error_msg = std::string("run() 失败: ") + ErrorCodeName(code);
        return false;
    }
    return true;
}

// 步骤 5：补齐停止原因 —— SDK 未发 END 时兜底
void AidGenProvider::FinalizeResponse(ChatResponse& response) const {
    // 兜底：SDK 未走 END 事件时标记仍可能留在正文里
    StripControlMarkers(response.content_text, cfg_.im_end);
    if (response.stop_reason.empty() && response.error_msg.empty()) {
        response.stop_reason = "end_turn";
    }
}

}  // namespace prosophor

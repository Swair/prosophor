// voice_session 实现 —— 设计与线程约定见 voice_session.h。
#include "voice_session.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <string_view>
#include <utility>

#include "common/log_wrapper.h"

namespace prosophor::nr::app {

namespace {

// 帧能量 → dBFS（0 = 满幅；全零返回 -1000，必然低于任何门限）
double FrameRmsDbfs(const int16_t* pcm, size_t samples) {
    if (samples == 0) {
        return -1000.0;
    }
    double sum = 0.0;
    for (size_t i = 0; i < samples; ++i) {
        const double v = static_cast<double>(pcm[i]);
        sum += v * v;
    }
    const double rms = std::sqrt(sum / static_cast<double>(samples));
    if (rms < 1e-9) {
        return -1000.0;
    }
    return 20.0 * std::log10(rms / 32768.0);
}

int FramePeak(const int16_t* pcm, size_t samples) {
    int peak = 0;
    for (size_t i = 0; i < samples; ++i) {
        const int v = pcm[i] < 0 ? -static_cast<int>(pcm[i]) : pcm[i];
        if (v > peak) {
            peak = v;
        }
    }
    return peak;
}

bool IsUtf8Continuation(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

// 迭代 UTF-8 码点（不校验合法性，按首字节推长度；ASCII 与合法 UTF-8 均可用）
struct Codepoint {
    uint32_t value = 0;
    int bytes = 1;
};

Codepoint NextCodepoint(const std::string& text, size_t index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    Codepoint cp;
    cp.value = lead;
    if (lead >= 0xF0) {
        cp.bytes = 4;
        cp.value = lead & 0x07u;
    } else if (lead >= 0xE0) {
        cp.bytes = 3;
        cp.value = lead & 0x0Fu;
    } else if (lead >= 0xC0) {
        cp.bytes = 2;
        cp.value = lead & 0x1Fu;
    }
    for (int k = 1; k < cp.bytes && index + static_cast<size_t>(k) < text.size(); ++k) {
        const unsigned char byte = static_cast<unsigned char>(text[index + static_cast<size_t>(k)]);
        if (!IsUtf8Continuation(byte)) {
            cp.bytes = k;   // 截断的多字节序列：按已读部分收尾
            break;
        }
        cp.value = (cp.value << 6) | (byte & 0x3Fu);
    }
    return cp;
}

// 需要丢弃的码点：markdown 记号、emoji 与装饰符号、控制符
bool IsDroppable(uint32_t cp) {
    if (cp < 0x20) return true;                                   // 控制符
    switch (cp) {
        case '*': case '`': case '#': case '>': case '_': case '~':
            return true;                                          // markdown 记号
        case 0x200Du: case 0xFE0Fu:                               // ZWJ / 变体选择符
            return true;
        default:
            break;
    }
    if (cp >= 0x1F000) return true;                               // emoji 与扩展平面
    if (cp >= 0x2600 && cp <= 0x27BF) return true;                // 杂项符号/装饰
    if (cp >= 0x2B00 && cp <= 0x2BFF) return true;
    return false;
}

bool IsSentenceEnd(uint32_t cp) {
    switch (cp) {
        case 0x3002:  // 。
        case 0xFF01:  // ！
        case 0xFF1F:  // ？
        case 0xFF1B:  // ；
        case '!': case '?': case ';': case '.':
            return true;
        default:
            return false;
    }
}

void TrimTrailingSpaces(std::string& text) {
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
}

}  // namespace

namespace {

// 全角/半角标点：必须逐"序列"比较 —— 多字节标点放进字符集合会按字节展开，
// 误吃掉别处多字节字符的首字节（"（沉默）"的 "（" 首字节 0xEF 与 "，" 相同）
const char* const kPuncts[] = {"。", "，", "！", "？", "；", "、", "：", "…", "—",
                               ".", ",", ";", "!", "?", ":", "-"};

// 剥掉首尾的空白与**一轮**标点（"（沉默）。" -> "（沉默）"）。
// 只剥一轮是刻意为之：IsSilentReply 的等值判断需要标记本体完整;
// 反复剥会把 "（沉默）.-" 这类变体剥成残句，多轮无收益只增误伤面。
std::string TrimEdges(const std::string& text) {
    std::string out = text;
    const size_t begin = out.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    out.erase(0, begin);
    out.erase(out.find_last_not_of(" \t\r\n") + 1);

    for (const char* punct : kPuncts) {
        const size_t len = std::string_view(punct).size();
        if (out.size() >= len && out.compare(0, len, punct) == 0) {
            out.erase(0, len);
            break;
        }
    }
    for (const char* punct : kPuncts) {
        const size_t len = std::string_view(punct).size();
        if (out.size() >= len && out.compare(out.size() - len, len, punct) == 0) {
            out.erase(out.size() - len);
            break;
        }
    }
    return out;
}

}  // namespace

bool IsSilentReply(const std::string& reply) {
    const std::string trimmed = TrimEdges(reply);
    return trimmed == kSilenceMarker || trimmed == "(沉默)" || trimmed == "[沉默]";
}

std::string StripSilenceMarker(const std::string& text) {
    std::string out = text;
    for (const char* marker : {kSilenceMarker, "(沉默)", "[沉默]"}) {
        size_t pos = 0;
        while ((pos = out.find(marker, pos)) != std::string::npos) {
            out.erase(pos, std::string(marker).size());
        }
    }
    return out;
}

bool ShouldAnswerUtterance(const std::string& text) {
    // 问询词：出现即"在问话"
    for (const char* token : {"？", "?", "吗", "呢", "怎么", "多少", "什么", "推荐"}) {
        if (text.find(token) != std::string::npos) return true;
    }
    // 营养把关词：即使陈述句，"这汤太咸了"也该接（餐桌管家职责内）
    for (const char* token : {"热量", "千卡", "卡路里", "糖", "盐", "咸", "油",
                              "减肥", "禁忌", "过敏", "能吃", "不能吃", "可以吃"}) {
        if (text.find(token) != std::string::npos) return true;
    }
    return false;
}

std::string SanitizeForSpeech(const std::string& reply, size_t max_chars) {
    std::string out;
    out.reserve(reply.size());
    size_t codepoints = 0;
    size_t last_sentence_end = std::string::npos;   // out 中的字节位置（含标点）
    size_t last_sentence_end_cp = 0;                // 对应的码点数
    bool pending_space = false;

    size_t i = 0;
    while (i < reply.size()) {
        // 模型特殊 token（<|endoftext|>/<|im_end|> 等）：小模型偶有原样吐出
        // （实测日志 "[助手] <|endoftext|"），念出来是噪音，整段丢弃
        if (reply.compare(i, 2, "<|") == 0) {
            const size_t close = reply.find("|>", i + 2);
            if (close != std::string::npos) {
                i = close + 2;
                continue;
            }
        }

        // URL 整段丢弃（念网址对播报无意义）
        if (reply.compare(i, 7, "http://") == 0 || reply.compare(i, 8, "https://") == 0) {
            while (i < reply.size() &&
                   !std::isspace(static_cast<unsigned char>(reply[i]))) {
                ++i;
            }
            pending_space = true;
            continue;
        }

        const Codepoint cp = NextCodepoint(reply, i);
        const size_t char_bytes = static_cast<size_t>(cp.bytes);
        i += char_bytes;

        // 空白（含换行）折叠为单个空格，稍后按需输出
        if (cp.value == ' ' || cp.value == '\n' || cp.value == '\r' || cp.value == '\t') {
            pending_space = true;
            continue;
        }
        if (IsDroppable(cp.value)) {
            continue;
        }

        // 已达上限而文本未结束：退到最近句末，否则硬截补句号
        if (codepoints >= max_chars) {
            if (last_sentence_end != std::string::npos &&
                last_sentence_end_cp * 2 >= max_chars) {
                out.resize(last_sentence_end);
            } else {
                out += "。";
            }
            TrimTrailingSpaces(out);
            return out;
        }

        if (pending_space && !out.empty()) {
            out += ' ';
            ++codepoints;
        }
        pending_space = false;

        out.append(reply, i - char_bytes, char_bytes);
        ++codepoints;
        if (IsSentenceEnd(cp.value)) {
            last_sentence_end = out.size();
            last_sentence_end_cp = codepoints;
        }
    }
    TrimTrailingSpaces(out);
    return out;
}

// ============================ VoiceSession ============================

VoiceSession::VoiceSession(TranscribeFn transcribe, SpeakFn speak, VoiceTuning tuning,
                           bool start_workers)
    : transcribe_(std::move(transcribe)),
      speak_(std::move(speak)),
      tuning_(tuning) {
    if (start_workers) {
        Start();
    }
}

VoiceSession::~VoiceSession() {
    Stop();
}

void VoiceSession::Start() {
    if (running_.exchange(true)) {
        return;
    }
    asr_thread_ = std::thread([this] { AsrWorker(); });
    tts_thread_ = std::thread([this] { TtsWorker(); });
    LOG_INFO("VoiceSession: workers 已启动");
}

void VoiceSession::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    asr_cv_.notify_all();
    tts_cv_.notify_all();
    if (asr_thread_.joinable()) {
        asr_thread_.join();
    }
    if (tts_thread_.joinable()) {
        tts_thread_.join();
    }
    LOG_INFO("VoiceSession: workers 已停止");
}

void VoiceSession::ResetVadState() {
    in_speech_ = false;
    start_confirm_ = 0;
    silence_frames_ = 0;
    utterance_.clear();
    pre_roll_.clear();
}

void VoiceSession::FeedFrame(const int16_t* pcm, size_t samples) {
    if (pcm == nullptr || samples < static_cast<size_t>(tuning_.frame_samples)) {
        return;
    }
    if (reset_requested_.exchange(false)) {
        ResetVadState();
    }
    BumpCounter(&Stats::feed_frames);

    const size_t frame = static_cast<size_t>(tuning_.frame_samples);
    const bool voiced = FrameRmsDbfs(pcm, frame) > tuning_.vad_rms_dbfs;

    if (!in_speech_) {
        // 空闲：维护回捞环，连续确认后进入说话态（回捞帧一并入句，防首字被切）
        pre_roll_.emplace_back(pcm, pcm + frame);
        const size_t keep = static_cast<size_t>(std::max(0, tuning_.pre_roll_frames));
        while (pre_roll_.size() > keep) {
            pre_roll_.pop_front();
        }
        start_confirm_ = voiced ? start_confirm_ + 1 : 0;
        if (start_confirm_ >= tuning_.start_confirm_frames) {
            in_speech_ = true;
            silence_frames_ = 0;
            utterance_.clear();
            for (const auto& buffered : pre_roll_) {
                utterance_.insert(utterance_.end(), buffered.begin(), buffered.end());
            }
            pre_roll_.clear();
            start_confirm_ = 0;
        }
        return;
    }

    // 说话中：累积并判定收句（静音够长或到达单句上限）
    utterance_.insert(utterance_.end(), pcm, pcm + frame);
    silence_frames_ = voiced ? 0 : silence_frames_ + 1;
    const bool too_long =
        static_cast<int>(utterance_.size() / frame) >= tuning_.max_utterance_frames;
    if (!too_long && silence_frames_ < tuning_.end_silence_frames) {
        return;
    }

    std::vector<int16_t> formed = std::move(utterance_);
    ResetVadState();

    // 三道闸：最短句长 / 峰值自检（数字静音坑）/ 单飞（上一句在飞则丢弃）
    const int frames = static_cast<int>(formed.size() / frame);
    if (frames < tuning_.min_utterance_frames) {
        BumpCounter(&Stats::dropped_short);
        return;
    }
    if (FramePeak(formed.data(), formed.size()) < tuning_.min_peak) {
        BumpCounter(&Stats::dropped_lowpeak);
        return;
    }
    if (asr_in_flight_.load() > 0) {
        BumpCounter(&Stats::dropped_busy);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(asr_mutex_);
        asr_queue_.push_back(std::move(formed));
    }
    asr_in_flight_.fetch_add(1);
    BumpCounter(&Stats::utterances);
    asr_cv_.notify_one();
}

std::optional<std::string> VoiceSession::PollUtterance(int timeout_ms) {
    std::unique_lock<std::mutex> lock(text_mutex_);
    if (text_queue_.empty()) {
        text_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [&] { return !text_queue_.empty(); });
    }
    if (text_queue_.empty()) {
        return std::nullopt;
    }
    std::string text = std::move(text_queue_.front());
    text_queue_.pop_front();
    return text;
}

void VoiceSession::Speak(std::string text) {
    if (text.empty()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(tts_mutex_);
        if (has_pending_speech_) {
            BumpCounter(&Stats::speak_dropped);   // 深度 1：丢旧保新
        }
        pending_speech_ = std::move(text);
        has_pending_speech_ = true;
    }
    tts_cv_.notify_one();
}

void VoiceSession::AsrWorker() {
    while (running_.load()) {
        std::vector<int16_t> pcm;
        {
            std::unique_lock<std::mutex> lock(asr_mutex_);
            asr_cv_.wait_for(lock, std::chrono::milliseconds(200), [&] {
                return !asr_queue_.empty() || !running_.load();
            });
            if (!running_.load()) {
                break;
            }
            if (asr_queue_.empty()) {
                continue;
            }
            pcm = std::move(asr_queue_.front());
            asr_queue_.pop_front();
        }

        std::string text;
        try {
            text = transcribe_ ? transcribe_(pcm) : std::string{};
        } catch (...) {
            text.clear();   // 异常不出 worker（转文本后按空结果处理）
        }
        asr_in_flight_.fetch_sub(1);

        if (text.empty()) {
            BumpCounter(&Stats::asr_empty);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(text_mutex_);
            text_queue_.push_back(std::move(text));
            while (text_queue_.size() > 4) {
                text_queue_.pop_front();   // 上限防堆积（主循环卡住时丢最旧）
            }
        }
        text_cv_.notify_all();
    }
}

void VoiceSession::TtsWorker() {
    while (running_.load()) {
        std::string text;
        {
            std::unique_lock<std::mutex> lock(tts_mutex_);
            tts_cv_.wait_for(lock, std::chrono::milliseconds(200), [&] {
                return has_pending_speech_ || !running_.load();
            });
            if (!running_.load()) {
                break;
            }
            if (!has_pending_speech_) {
                continue;
            }
            text = std::move(pending_speech_);
            pending_speech_.clear();
            has_pending_speech_ = false;
        }

        speaking_.store(true);
        bool ok = false;
        try {
            ok = speak_ ? speak_(text) : false;
        } catch (...) {
            ok = false;   // 异常不出 worker
        }
        speaking_.store(false);
        BumpCounter(ok ? &Stats::spoken : &Stats::speak_failed);
    }
}

void VoiceSession::BumpCounter(int Stats::*field) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ++(stats_.*field);
}

VoiceSession::Stats VoiceSession::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

}  // namespace prosophor::nr::app

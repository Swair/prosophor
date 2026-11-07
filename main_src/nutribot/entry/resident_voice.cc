// resident 语音状态机：唤醒 / 聆听 / 播报
#include "entry/resident_session.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "common/log_wrapper.h"
#include "prosophor_core/public/core_types.h"
#include "entry/nutribot_support.h"
#include "tools/robot_control_tools.h"
#include "voice_interaction/speech_text.h"

namespace prosophor::nr::nutribot {

namespace {

// 底盘 talking 表情保持 5 秒后自行回落；播报超过这个周期就补发一次
constexpr int talking_face_refresh_ms = 3000;
// 说话时小爪开合幅度（爪行程 25~70）
constexpr int speaking_claw_open_angle = 70;
constexpr int speaking_claw_closed_angle = 45;

// aidgen 文本协议的工具调用标签（与 providers/llm/aidgen_tool_call_protocol.h
// 一致）。流式阶段 provider 还没解析工具调用，标签内 JSON 不能念出来。
constexpr std::string_view tool_call_open = "<tool_call>";
constexpr std::string_view tool_call_close = "</tool_call>";

size_t CountCodepoints(const std::string& text) {
    size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0) != 0x80) ++count;
    }
    return count;
}

std::string NormalizeUtterance(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char byte : text) {
        if (byte >= 0x80) {
            out.push_back(static_cast<char>(byte));   // 汉字等多字节字符原样保留
        } else if (std::isalnum(byte)) {
            out.push_back(static_cast<char>(std::tolower(byte)));
        }
    }
    static constexpr std::string_view kPunctuation[] = {
        "，", "。", "！", "？", "、", "；", "：", "…", "—", "·", "　",
        "“", "”", "‘", "’", "（", "）", "《", "》", "【", "】"};
    for (const std::string_view marker : kPunctuation) {
        std::string::size_type pos = 0;
        while ((pos = out.find(marker, pos)) != std::string::npos) {
            out.erase(pos, marker.size());
        }
    }
    return out;
}

int AwakeWindowMs(const prosophor::NutribotConfig& cfg) {
    return cfg.awake_window_ms > 0 ? cfg.awake_window_ms : 8000;
}

// 纯标点/空白/噪声的 ASR 结果不进对话（归一化后为空）
bool HasSpeakableContent(const std::string& text) {
    return !NormalizeUtterance(text).empty();
}

// 依赖当前画面的问题：事实过期时先补一拍感知再交给对话模型
bool IsVisionQuestion(const std::string& text) {
    static constexpr std::string_view markers[] = {
        "看到", "看见", "画面", "桌上", "餐盘", "什么菜",
        "这是什么", "那是什么", "有没有菜", "认得出",
        // 营养/热量类问题同样需要当前餐桌事实（含营养库数值）
        "热量", "营养", "卡路里", "多少卡", "脂肪", "蛋白质", "钠", "糖分",
        "能不能吃", "能吃"};
    for (const std::string_view marker : markers) {
        if (text.find(marker.data(), 0, marker.size()) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 整段回复看起来像裸工具调用（name(...)）时不能念出来：真正的调用应已被
// provider 解析并执行；走到这里说明解析失败，TTS 念出参数只会是噪音。
bool LooksLikeToolCallText(const std::string& text) {
    const size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return false;
    const size_t end = text.find_last_not_of(" \t\r\n");
    const std::string trimmed = text.substr(begin, end - begin + 1);

    // JSON 形态的工具调用：{"name": "...", "arguments"/"parameters": {...}}
    if (trimmed.front() == '{' && trimmed.find("\"name\"") != std::string::npos &&
        (trimmed.find("\"arguments\"") != std::string::npos ||
         trimmed.find("\"parameters\"") != std::string::npos)) {
        return true;
    }

    const bool ends_ascii = trimmed.back() == ')';
    const bool ends_full =
        trimmed.size() >= 3 && trimmed.compare(trimmed.size() - 3, 3, "）") == 0;
    if (!ends_ascii && !ends_full) return false;
    size_t paren = trimmed.find('(');
    const size_t full_paren = trimmed.find("（");
    if (full_paren != std::string::npos &&
        (paren == std::string::npos || full_paren < paren)) {
        paren = full_paren;
    }
    if (paren == std::string::npos || paren == 0) return false;

    // 只看括号前的名字部分（允许名字和括号之间有空格）；参数里出现中文标点
    // （robotface(你看到什么的？)）不影响"这是一条工具调用"的判断。
    const size_t name_begin = trimmed.find_first_not_of(" \t\r\n");
    const size_t name_end = trimmed.find_last_not_of(" \t\r\n", paren - 1);
    if (name_end == std::string::npos || name_end < name_begin) return false;
    for (size_t i = name_begin; i <= name_end; ++i) {
        const unsigned char c = static_cast<unsigned char>(trimmed[i]);
        if (std::isalnum(c) == 0 && c != '_') return false;
    }
    return true;
}

}  // namespace

void ResidentSession::TickConversation(prosophor::SteadyClock::TimePoint now) {
    // ① 对话相位：待机听唤醒词 / 窗口内听需求 / 播报结束回窗口。
    // LLM 推理在 TickListening 内同步跑完，不需要单独的"思考"状态。
    if (voice_interaction_.IsSpeaking()) {
        phase_ = Phase::kSpeaking;
        RefreshSpeakingFace(now);
        return;
    }
    if (phase_ == Phase::kSpeaking) {
        // LLM 通过 robot_face 工具请求的表情：播报期间被 talking 覆盖，
        // 说完后恢复，保持 5 秒由固件自动回落。
        const std::string requested_face = TakeLastRequestedFace();
        if (!requested_face.empty()) {
            SetRobotFace(requested_face);
            LOG_INFO("[表情] 播报结束 → {}（5 秒后自动回落）",
                     requested_face.c_str());
        } else {
            SetRobotFace("auto");   // 播报结束：口型交还底盘默认表情
        }
        // LLM 通过 robot_claw 工具请求的收尾动作（无则全张）
        const int requested_claw = TakeLastRequestedClawAngle();
        SetRobotServo(6, requested_claw > 0 ? requested_claw
                                            : speaking_claw_open_angle);
        LOG_INFO("[流程] TTS 出 → 开聆听窗口");
        EnterListening(now);   // 播报结束：开 8s 聆听窗口
        return;
    }
    if (phase_ == Phase::kStandby) {
        TickStandby(now);
        return;
    }
    TickListening(now);
}

bool ResidentSession::IsWake(const std::string& text) const {
    const std::string normalized = NormalizeUtterance(text);
    if (normalized.empty()) return false;
    for (const std::string& word : config_.nutribot.wake_words) {
        const std::string target = NormalizeUtterance(word);
        // 只要归一化后包含连续唤醒词就唤醒（ASR 常带前后缀/标点）
        if (!target.empty() && normalized.find(target) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void ResidentSession::TickStandby(prosophor::SteadyClock::TimePoint now) {
    // 待机只认整句唤醒词；电视/桌边闲聊在这里被挡掉，不进 LLM
    std::vector<std::string> pieces;
    voice_interaction_.PollAllAsrResults(pieces);
    size_t wake_index = pieces.size();
    for (size_t i = 0; i < pieces.size(); ++i) {
        if (IsWake(pieces[i])) {
            wake_index = i;
            break;
        }
    }
    if (wake_index == pieces.size()) {
        if (!pieces.empty()) {
            LOG_DEBUG("nutribot: 待机忽略 {} 条语音（无唤醒词）", pieces.size());
        }
        return;
    }
    LOG_INFO("[流程] ASR 出: {}（命中唤醒词）", pieces[wake_index].c_str());
    LOG_INFO("[唤醒] 命中唤醒词");
    // 同批里唤醒词之后的独立 final 直接进并句缓冲，等应答播完统一派发
    for (size_t i = wake_index + 1; i < pieces.size(); ++i) {
        if (!IsWake(pieces[i]) && HasSpeakableContent(pieces[i])) {
            utterance_coalescer_.Add(std::move(pieces[i]), now);
        }
    }
    if (voice_interaction_.IsSpeechActive()) {
        // 用户已经在说需求：按半双工跳过应答，直接开窗；不能重置 VAD
        EnterListening(now, /*reset_input=*/false);
        return;
    }
    const std::string& ack = config_.nutribot.wake_ack_text;
    if (StartRobotSpeech(ack)) {
        LOG_INFO("[唤醒] 应答「{}」", ack.c_str());
        return;
    }
    // TTS 不可用时不能失聪：仍然进入聆听，让用户把需求说完
    LOG_WARN("nutribot: 唤醒应答未入队（TTS 不可用？），直接进入聆听");
    EnterListening(now);
}

void ResidentSession::TickListening(prosophor::SteadyClock::TimePoint now) {
    // 连说并句：一次取尽队列里的 final，等用户停口后统一派发
    std::vector<std::string> pieces;
    voice_interaction_.PollAllAsrResults(pieces);
    for (std::string& piece : pieces) {
        if (IsWake(piece)) {
            // 听到唤醒词就应答并重新计时，不区分“首次/再次”
            awake_until_ = now + prosophor::Milliseconds(AwakeWindowMs(config_.nutribot));
            const std::string& ack = config_.nutribot.wake_ack_text;
            if (StartRobotSpeech(ack)) {
                LOG_INFO("[唤醒] 应答「{}」", ack.c_str());
                return;
            }
            continue;
        }
        if (!HasSpeakableContent(piece)) {
            LOG_DEBUG("nutribot: 忽略无内容语音: {}", piece.c_str());
            continue;
        }
        LOG_INFO("[流程] ASR 出 → 并句: {}", piece.c_str());
        utterance_coalescer_.Add(std::move(piece), now);
    }
    if (utterance_coalescer_.ReadyToDispatch(now,
                                             voice_interaction_.IsSpeechActive())) {
        DispatchConversationTurn(utterance_coalescer_.TakeMerged());
        return;
    }
    // 先派发再判超时：窗口到期与用户停口同 tick 时不能把话丢掉
    if (utterance_coalescer_.Empty() &&
        !voice_interaction_.IsSpeechActive() && now >= awake_until_) {
        LOG_INFO("[唤醒] 聆听窗口结束，回待机");
        EnterStandby();
    }
}

void ResidentSession::DispatchConversationTurn(const std::string& text) {
    voice_interaction_.SetMicMuted(true);   // 思考期闭麦
    LOG_INFO("[流程] 并句 → LLM: {}", text.c_str());
    LOG_INFO("[用户] {}", text.c_str());

    const std::string frame_path = camera_.LatestFrame();
    const bool vision_question = IsVisionQuestion(text);
    filler_queued_this_turn_ = false;
    QueueThinkingFiller(vision_question);
    // 视觉问题且事实过期：先补一拍感知，避免对话模型凭空编菜名
    if (vision_question && !HasFreshTableFact() && !frame_path.empty()) {
        LOG_INFO("[感知] 视觉问题且事实过期，先补一拍");
        RunPerceptionTurn(frame_path);
        ApplyPendingTableReport();
    }

    // 用户先开口：取消待播报的主动提醒（本轮已带最新事实）
    if (pending_conflict_at_.has_value()) {
        LOG_INFO("[冲突] 用户先开口，主动提醒并入本轮对话");
        pending_conflict_at_.reset();
    }

    // 用户语音也是一个任务事件：同一个 agent 会话决定回答/调工具/沉默
    const AgentTurnResult turn =
        RunAgentTurn(text, frame_path, {}, vision_question);
    ApplyPendingTableReport();
    const bool meal_window = schedule_.Lookup(LocalMinutesOfDay()).has_value();
    const Milliseconds period =
        meal_window ? perception_period_ : idle_perception_period_;
    next_perception_at_ = prosophor::SteadyClock::Now() + period;

    const auto finished_at = prosophor::SteadyClock::Now();
    if (!turn.ok || !SpeakAgentReply(turn.reply, /*proactive=*/false)) {
        LOG_INFO("[沉默/失败] agent 未产出可播报回复");
        EnterListening(finished_at);
    }
}

void ResidentSession::QueueThinkingFiller(bool vision_question) {
    // 云端模型（DeepSeek）首句够快，不垫套路话；只有本地模型才需要填充
    if (!thinking_fillers_enabled_) return;

    const auto& fillers = vision_question ? config_.nutribot.vision_fillers
                                          : config_.nutribot.thinking_fillers;
    if (fillers.empty()) return;

    const std::string& filler = fillers[filler_index_ % fillers.size()];
    ++filler_index_;
    if (!voice_interaction_.Speak(filler)) {
        LOG_DEBUG("nutribot: 思考填充语入队失败: {}", filler.c_str());
        return;
    }
    // 填充语播完到回复首句之间不断流（避免扬声器开关声），首句入队时再解除
    voice_interaction_.SetPlaybackHold(true);
    filler_queued_this_turn_ = true;
    phase_ = Phase::kSpeaking;
    BeginSpeakingFace();
    LOG_INFO("[流程] 思考填充: {}", filler.c_str());
}

void ResidentSession::SpeakStreamReady(bool done) {
    if (!stream_turn_active_) return;
    const std::string ready = tts_chunker_.TakeReady(done);
    if (ready.empty()) return;

    // 工具协议段（aidgen 文本协议）整段扣住：标签里的 JSON 参数不能念
    std::string filtered;
    size_t cursor = 0;
    while (cursor < ready.size()) {
        if (stream_in_tool_block_) {
            const size_t close = ready.find(tool_call_close, cursor);
            if (close == std::string::npos) break;
            cursor = close + tool_call_close.size();
            stream_in_tool_block_ = false;
            continue;
        }
        const size_t open = ready.find(tool_call_open, cursor);
        if (open == std::string::npos) {
            filtered.append(ready, cursor, std::string::npos);
            break;
        }
        filtered.append(ready, cursor, open - cursor);
        stream_in_tool_block_ = true;
        cursor = open + tool_call_open.size();
    }
    if (filtered.empty()) return;

    const std::string text =
        prosophor::voice::StripSilenceMarker(filtered);
    if (text.empty() || prosophor::voice::IsSilentReply(text)) return;
    if (LooksLikeToolCallText(text)) {
        LOG_DEBUG("nutribot: 流式工具调用文本不播报：{}", text.c_str());
        return;
    }

    size_t budget = 0;   // 0 = 不截断
    if (voice_cfg_.speak_max_chars > 0) {
        const size_t cap = static_cast<size_t>(voice_cfg_.speak_max_chars);
        if (stream_spoken_chars_ >= cap) return;
        budget = cap - stream_spoken_chars_;
    }
    const std::string speech = prosophor::voice::SanitizeForSpeech(text, budget);
    if (speech.empty()) return;

    // 回复开始入队：只丢掉还没开播的填充语，当前正在播的那条让它自然收尾
    if (!stream_reply_queued_) {
        stream_reply_queued_ = true;
        if (filler_queued_this_turn_) {
            const size_t dropped = voice_interaction_.DropPendingSpeech();
            if (dropped > 0) {
                LOG_DEBUG("nutribot: 丢弃 {} 条未开播填充语", dropped);
            }
            filler_queued_this_turn_ = false;
        }
    }
    // TTS worker 是"合成 + 播放"串行：不预合成的话，每句播放完才开始合成下一句，
    // 本地 TTS 每句要 0.6~1s，句间会有明显空档。这里在主线程先合成（前一句还在
    // 播），worker 取到就是缓存命中，句间无缝；合成与播放互不持同一把锁。
    voice_interaction_.WarmSpeech(speech);
    if (!voice_interaction_.Speak(speech)) return;
    voice_interaction_.SetPlaybackHold(false);   // 回复已接上，静音垫可以停了

    stream_spoke_ = true;
    stream_spoken_chars_ += CountCodepoints(speech);
    phase_ = Phase::kSpeaking;
    RefreshSpeakingFace(prosophor::SteadyClock::Now());
    LOG_INFO("[助手] {}", speech.c_str());
}

bool ResidentSession::StartRobotSpeech(const std::string& speech,
                                       bool queue_behind) {
    if (speech.empty() || (!queue_behind && voice_interaction_.IsSpeaking())) {
        return false;
    }
    voice_interaction_.SetMicMuted(true);   // 先闭麦，再入队
    if (!voice_interaction_.Speak(speech)) {
        LOG_WARN("[流程] TTS 入队失败");
        voice_interaction_.SetMicMuted(false);
        return false;
    }
    LOG_INFO("[流程] TTS 入: {} 字", speech.size());
    phase_ = Phase::kSpeaking;
    BeginSpeakingFace();
    return true;
}

void ResidentSession::BeginSpeakingFace() {
    // 每次播报都尝试 talking（固件支持动态嘴型）；失败只影响本拍，回退 happy，
    // 不在首次失败后永久禁用——否则一次网络抖动会一直用静态表情。
    if (!SetRobotFace("talking")) {
        SetRobotFace("happy");
    }
    // 说话时小爪开合（爪行程 25~70），25° 幅度
    speaking_claw_open_ = !speaking_claw_open_;
    const int angle = speaking_claw_open_ ? speaking_claw_open_angle
                                          : speaking_claw_closed_angle;
    if (!SetRobotServo(6, angle)) {
        LOG_DEBUG("robot: 说话爪子微动失败");
    }
    next_speaking_face_at_ =
        prosophor::SteadyClock::Now() + prosophor::Milliseconds(talking_face_refresh_ms);
}

void ResidentSession::RefreshSpeakingFace(prosophor::SteadyClock::TimePoint now) {
    if (now < next_speaking_face_at_) return;
    BeginSpeakingFace();
}

bool ResidentSession::SpeakAgentReply(const std::string& reply, bool proactive,
                                      const char* source) {
    const bool queue_behind =
        filler_queued_this_turn_ && voice_interaction_.IsSpeaking();
    filler_queued_this_turn_ = false;
    // 回复已在本函数里入队（或确认无话可说）：思考静音垫到此为止。
    // 成功路径在 StartRobotSpeech 之后再清，失败/沉默路径立即清。
    const auto end_playback_hold = [this] {
        voice_interaction_.SetPlaybackHold(false);
    };

    if (stream_spoke_) {
        // 流式已逐句播报：不整段重播，只补主动播报的记账与日志
        if (proactive) {
            const std::string speech = prosophor::voice::SanitizeForSpeech(
                prosophor::voice::StripSilenceMarker(reply),
                voice_cfg_.speak_max_chars);
            if (!speech.empty()) {
                last_proactive_reply_ = speech;
                if (source != nullptr && *source != '\0') {
                    LOG_INFO("[主动] {}: {}", source, speech.c_str());
                } else {
                    LOG_INFO("[主动] {}", speech.c_str());
                }
            }
        }
        end_playback_hold();
        return true;
    }

    if (reply.empty() || prosophor::voice::IsSilentReply(reply)) {
        end_playback_hold();
        return false;
    }
    if (LooksLikeToolCallText(reply)) {
        end_playback_hold();
        LOG_WARN("nutribot: 未解析的工具调用，丢弃不播报：{}", reply.c_str());
        return false;
    }
    const std::string speech = prosophor::voice::SanitizeForSpeech(
        prosophor::voice::StripSilenceMarker(reply), voice_cfg_.speak_max_chars);
    if (speech.empty()) {
        end_playback_hold();
        return false;
    }

    if (proactive) {
        if (source != nullptr && *source != '\0') {
            LOG_INFO("[主动] {}: {}", source, speech.c_str());
        } else {
            LOG_INFO("[主动] {}", speech.c_str());
        }
    } else {
        LOG_INFO("[助手] {}", speech.c_str());
    }

    if (queue_behind) {
        // 填充语还在播：先把回复合成好（播放时不持 tts 锁），再排到队列末尾；
        // 只丢掉还没开播的填充语，当前这条自然收尾后无缝接上回复。
        voice_interaction_.WarmSpeech(speech);
        const size_t dropped = voice_interaction_.DropPendingSpeech();
        if (dropped > 0) {
            LOG_DEBUG("nutribot: 丢弃 {} 条未开播填充语", dropped);
        }
    }
    if (!StartRobotSpeech(speech, queue_behind)) {
        end_playback_hold();
        return false;
    }
    end_playback_hold();
    if (proactive) last_proactive_reply_ = speech;
    return true;
}

bool ResidentSession::CanStartProactiveSpeech() const {
    return phase_ == Phase::kStandby && !no_member_profiles_ &&
           !voice_interaction_.IsSpeechActive() && !voice_interaction_.IsSpeaking();
}

void ResidentSession::EnterListening(prosophor::SteadyClock::TimePoint now,
                                     bool reset_input) {
    voice_interaction_.SetPlaybackHold(false);   // 安全兜底：轮次已结束
    if (reset_input) {
        voice_interaction_.SetMicMuted(false);   // 开麦 + 重置 VAD
    }
    phase_ = Phase::kListening;
    awake_until_ = now + prosophor::Milliseconds(AwakeWindowMs(config_.nutribot));
}

void ResidentSession::EnterStandby() {
    voice_interaction_.SetPlaybackHold(false);
    utterance_coalescer_.Clear();
    voice_interaction_.SetMicMuted(false);
    phase_ = Phase::kStandby;
}

}  // namespace prosophor::nr::nutribot

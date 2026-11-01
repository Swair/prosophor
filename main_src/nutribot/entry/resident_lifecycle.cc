// resident 生命周期：Init / Shutdown / 主循环骨架 / 统计
#include "entry/resident_session.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "prosophor_core/public/agent_engine.h"
#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "prosophor_core/public/core_types.h"
#include "entry/nutribot_support.h"
#include "platform/platform.h"
#include "tools/robot_control_tools.h"

namespace prosophor::nr::nutribot {

namespace {

std::atomic<bool> g_stop{false};

std::string ResolveMemberStoreDir(const prosophor::NutribotConfig& cfg) {
    if (!cfg.member_store_dir.empty()) {
        return prosophor::ExpandHome(cfg.member_store_dir);
    }
    return prosophor::JoinPath(prosophor::ProsophorConfig::BaseDir().string(),
                               "nutribot_members");
}


// 演示成员种子：优先部署目录，回退仓库配置
std::string ResolveDemoMembersFile() {
    const std::string deployed = prosophor::JoinPath(
        prosophor::ProsophorConfig::BaseDir().string(),
        "nutribot_db", "demo_members.json");
    if (prosophor::FileExists(deployed)) return deployed;
    return "config/.prosophor/nutribot_db/demo_members.json";
}

// 空档案库从配置种子预置演示成员
void SeedDemoMembersIfEmpty(prosophor::nr::infra::JsonMemberRepository& repo) {
    if (!repo.list_members().empty()) return;

    const std::string seed_file = ResolveDemoMembersFile();
    const auto seed = prosophor::ReadJson(seed_file);
    if (!seed.has_value() || !seed->contains("members") ||
        !(*seed)["members"].is_array()) {
        LOG_WARN("nutribot: 演示成员配置不可用（{}）—— 档案库保持为空",
                 seed_file.c_str());
        return;
    }

    size_t seeded = 0;
    for (const auto& item : (*seed)["members"]) {
        if (!item.is_object()) continue;
        try {
            auto member =
                prosophor::nr::infra::JsonMemberRepository::member_from_json(
                    item.dump());
            if (member.name.empty()) continue;   // 无文件名主键，落盘只会得到 .json
            repo.add_member(member);
            ++seeded;
        } catch (const std::exception& e) {
            LOG_WARN("nutribot: 演示成员条目非法已跳过：{}", e.what());
        }
    }
    LOG_INFO("nutribot: 档案库为空 —— 已从 {} 预置 {} 位演示成员",
             seed_file.c_str(), seeded);
}

// 电机是锁存语义：一次 move 指令后固件保持目标速度，只有显式 stop 才停。
// 启动与退出时各清一次，防止带着锁存目标无人看管地跑。

void ClearLatchedMotion(const char* occasion) {
    if (!prosophor::ProsophorConfig::GetInstance().robot.enabled) return;
    const bool stopped = StopRobotMotion();
    LOG_INFO("robot: {}清运动目标 -> {}", occasion, stopped ? "ok" : "skipped/failed");
}

}  // namespace

ResidentSession::ResidentSession()
    : config_(prosophor::ProsophorConfig::GetInstance()),
      voice_cfg_(config_.voice) {}

ResidentSession::~ResidentSession() {
    Shutdown();
}

bool ResidentSession::Init(std::string& error) {
    g_stop.store(false);
    // 第一次 Ctrl-C：置停止标志，等当前推理返回后优雅退出。
    // 第二次 Ctrl-C：立即强退 —— 8B 的一次 VL 推理可能卡分钟级，等不起。
    prosophor::Platform::SetConsoleSignalHandler([] {
        static std::atomic<int> signal_count{0};
        if (signal_count.fetch_add(1) + 1 >= 2) std::_Exit(130);
        g_stop.store(true);
    });

    // AgentEngine 构造会初始化核心（LLM 路由等），放在信号处理器之后，
    // 保证长初始化期间 Ctrl-C 仍走优雅退出而不是默认终止。
    // NutriBot 是专用设备形态：不加载 bash/file/web 等通用内建工具，
    // 只保留随后注册的感知/档案/机器人工具。
    engine_ = &prosophor::AgentEngine::GetInstance();

    prosophor::voice::VoiceInteractionConfig voice_config;
    voice_config.asr = config_.asr;
    voice_config.tts = config_.tts;
    voice_config.voice = config_.voice;
    voice_config.tuning = prosophor::voice::BuildVoiceTuning(config_.voice);
    voice_config.warmup_providers = true;
    voice_config.require_providers = true;
    voice_config.sanitize_tts_text = false;   // 话轮侧已用 SanitizeForSpeech
    voice_config.use_alsa_audio = true;       // 内部按 voice.capture/playback_device 创建
    voice_config.tts_cache_bytes = config_.voice.tts_cache_bytes;

    if (!voice_interaction_.Init(voice_config, error)) {
        return false;
    }

    InitPerception();
    InitEngineSession();
    ClearLatchedMotion("启动");

    voice_interaction_.Start();

    // 预热唤醒应答与思考填充语：第一次用到时直接命中 TTS 缓存，不再现合成
    if (!voice_interaction_.WarmSpeech(config_.nutribot.wake_ack_text)) {
        LOG_WARN("nutribot: 唤醒应答预热失败");
    }
    for (const std::string& filler : config_.nutribot.thinking_fillers) {
        voice_interaction_.WarmSpeech(filler);
    }
    for (const std::string& filler : config_.nutribot.vision_fillers) {
        voice_interaction_.WarmSpeech(filler);
    }

    // 视觉在启动期一次性装配：阻塞到模型装载结束，运行期不再轮询"半就绪"。
    // 语音先启动，装载等待期间仍可采集（只是没人派发话轮）。
    InitVisionPerception();

    // 视觉装配期间语音仍在采集：等待结束时队列里都是过期话轮，进主循环前
    // 清掉，避免开机第一句回答几十秒前的话；VAD 残留也一并复位。
    std::vector<std::string> stale_pieces;
    const size_t stale_count = voice_interaction_.PollAllAsrResults(stale_pieces);
    if (stale_count > 0) {
        LOG_INFO("nutribot: 启动等待期间丢弃 {} 条语音", stale_count);
    }
    voice_interaction_.ResetUtterance();

    std::string wake_words;
    for (const std::string& word : config_.nutribot.wake_words) {
        if (!wake_words.empty()) wake_words += "/";
        wake_words += word;
    }
    if (wake_words.empty() || config_.nutribot.wake_ack_text.empty()) {
        LOG_WARN("nutribot: 唤醒词/应答为空 —— 使用默认值（你好瓦力 / 我在）");
    }
    LOG_INFO("nutribot: 唤醒词 {}，应答「{}」，聆听窗口 {}ms",
             wake_words.c_str(), config_.nutribot.wake_ack_text.c_str(),
             config_.nutribot.awake_window_ms);

    next_tick_ = prosophor::SteadyClock::Now();
    perception_period_ = prosophor::Milliseconds(
        config_.camera.perception_interval_ms > 0 ? config_.camera.perception_interval_ms
                                                 : 3000);
    idle_perception_period_ = prosophor::Milliseconds(
        config_.camera.idle_interval_ms > 0 ? config_.camera.idle_interval_ms : 15000);
    next_perception_at_ = prosophor::SteadyClock::Now();
    next_meal_retry_at_ = prosophor::SteadyClock::Now();

    initialized_ = true;
    return true;
}

void ResidentSession::InitPerception() {
    // 摄像头/VL 任缺一件只降级感知（语音问答照常），不阻断常驻回路
    prosophor::nr::infra::CameraConfig camera_cfg;
    camera_cfg.device      = config_.camera.device;
    camera_cfg.output_dir  = config_.camera.output_dir;
    camera_cfg.interval_ms = config_.camera.interval_ms;
    camera_cfg.timeout_ms  = config_.camera.timeout_ms;
    camera_cfg.keep_frames = config_.camera.keep_frames;
    std::string camera_error;
    camera_ok_ = camera_.Start(camera_cfg, camera_error);
    if (!camera_ok_) {
        LOG_ERROR("nutribot: 摄像头不可用，主动提醒停用 —— {}", camera_error.c_str());
    }

    // 成员档案库（常驻持久目录；空库预置演示成员）
    const std::string members_dir = ResolveMemberStoreDir(config_.nutribot);
    prosophor::EnsureDirectory(members_dir);
    members_ = std::make_unique<prosophor::nr::infra::JsonMemberRepository>(members_dir);
    SeedDemoMembersIfEmpty(*members_);

    // 无人脸识别：所有已建档成员都参与禁忌/慢病提醒；档案库为空时只做看菜
    no_member_profiles_ = members_->list_members().empty();
    if (no_member_profiles_) {
        LOG_WARN("nutribot: 成员档案库为空（{}）—— 只做看菜，不做禁忌提醒",
                 members_dir.c_str());
    }

    // 营养知识库（菜品 -> 每份营养）：加载失败只降级（播报不含热量），不阻断
    {
        const std::string nutrition_path = ResolveNutritionFile(config_.nutribot);
        std::string nutrition_error;
        auto loaded = prosophor::nr::app::DishNutritionTable::LoadFromFile(
            nutrition_path, nutrition_error);
        if (loaded.has_value()) {
            if (loaded->skipped_entries() > 0) {
                LOG_WARN("nutribot: 营养库跳过坏条目 {} 条（{}）",
                         loaded->skipped_entries(), nutrition_path.c_str());
            }
            LOG_INFO("nutribot: 营养库已加载 {} 道菜（{}）", loaded->size(),
                     nutrition_path.c_str());
            nutrition_table_ = std::move(loaded);
        } else {
            LOG_WARN("nutribot: 营养库不可用（{}）—— 播报不含热量", nutrition_error.c_str());
        }
    }

    // 饭点表（非法窗口跳过并告警 —— 配置错不该拦住常驻回路）
    for (const auto& window : config_.meal.windows) {
        if (!schedule_.AddWindow(window.name, window.start, window.end)) {
            LOG_WARN("nutribot: 饭点窗口非法已跳过：{} {}-{}", window.name.c_str(),
                     window.start.c_str(), window.end.c_str());
        }
    }
    LOG_INFO("nutribot: 感知路 {}（摄像头 {} / 成员档案 {} 位 / 饭点 {} 段 / 营养库 {} 道）",
             camera_ok_ ? "待命，启动时等模型" : "停用",
             camera_ok_ ? "ok" : "失败",
             members_->list_members().size(), schedule_.windows().size(),
             nutrition_table_.has_value() ? nutrition_table_->size() : 0);
}

void ResidentSession::InitEngineSession() {
    // 专用形态：清掉 bash/file/web 等通用内建工具，只注册自己的领域工具。
    // 必须在注册产品工具、创建角色会话之前调用。
    engine_->SetUseDefaultTools(false);

    // 工具必须在角色会话创建前注册（role loader 创建时读取工具表并做白名单过滤）
    RegisterMemberProfileTool();          // 会话域：成员档案
    RegisterRobotControlTools(*engine_);  // 设备域：运动 / 夹爪 / 表情
    // 角色在 AgentEngine 构造时就加载过一次，那时外部工具还没注册；
    // 重算 role.tools，否则 request.tools 为空、模型看不到工具 schema。
    engine_->ReloadRoles();
    // 图片只服务当轮；后续轮次引用餐桌靠 <当前餐桌> 文本快照
    engine_->SetClearImagesAfterTurn(true);

    session_id_ = engine_->CreateSession("nutribot", "常驻语音会话");

    // 周期感知用独立 session：角色本身无工具，保留自己的文字上下文；
    // 图片由 SetClearImagesAfterTurn 每轮清理。状态都在 session 里，
    // 与对话会话互不污染。角色缺失只降级看菜，不阻断语音回路。
    try {
        perception_session_id_ =
            engine_->CreateSession("nutribot_perception", "周期看菜");
        // 硬保证：感知会话只做 JSON 提取，角色配置被改动也不许带工具
        engine_->SetSessionUseTools(perception_session_id_, false);
        // 无状态：每轮结束清空历史/图片，不落盘、不做记忆整理
        engine_->SetSessionStateless(perception_session_id_, true);
    } catch (const std::exception& e) {
        LOG_ERROR("nutribot: 感知会话创建失败，周期看菜停用：{}", e.what());
    }

    LOG_INFO("nutribot 常驻回路已启动：采集={} 播放={} 会话={} 感知会话={}",
             voice_cfg_.capture_device.c_str(), voice_cfg_.playback_device.c_str(),
             session_id_.c_str(), perception_session_id_.c_str());
    LOG_INFO("  >>> 对着麦克风说一句话，或把一道菜放到摄像头前试试（Ctrl-C 退出）");

    // 注意：AgentEngine 不提供移除回调的 API，因此本对象必须活到进程退出
    // （RunResident 的栈对象满足；main 随后 _Exit）。
    engine_->SetOutputCallback([this](
                                 const std::string& sid, const std::string&,
                                 prosophor::AgentRuntimeState state,
                                 const std::string& state_msg,
                                 const std::optional<prosophor::MessageSchema>& reply,
                                 const std::string& delta) {
        if (sid == perception_session_id_) {   // 感知结果单独收，不进对话回复
            if ((state == prosophor::AgentRuntimeState::COMPLETE ||
                 state == prosophor::AgentRuntimeState::STREAM_MODE_COMPLETE) &&
                reply) {
                last_perception_reply_ = reply->reply_text();
            }
            return;
        }
        if (sid != session_id_) return;   // 主动会话不影响用户话轮
        if (state == prosophor::AgentRuntimeState::EXECUTING_TOOL ||
            state == prosophor::AgentRuntimeState::TOOL_USE) {
            last_turn_tool_used_ = true;
            if (!state_msg.empty()) LOG_INFO("[工具] {}", state_msg.c_str());
        }
        // 流式正文：分句入 TTS，首句不必等整段生成完
        if (stream_turn_active_ &&
            state == prosophor::AgentRuntimeState::STREAM_CONTENT_TYPING &&
            !delta.empty()) {
            tts_chunker_.Push(delta);
            SpeakStreamReady(false);
        }
        if (state == prosophor::AgentRuntimeState::COMPLETE ||
            state == prosophor::AgentRuntimeState::STREAM_MODE_COMPLETE) {
            if (stream_turn_active_) SpeakStreamReady(true);
            if (reply) last_turn_reply_ = reply->reply_text();
        }
    });
}

bool ResidentSession::InitVisionPerception() {
    if (!camera_ok_) {
        LOG_WARN("nutribot: 摄像头不可用，跳过视觉装配 —— 看菜停用");
        return false;
    }
    // 本地 provider 在 AgentEngine 构造时异步装载，需阻塞到就绪；
    // 云端 provider（anthropic/openai）无本地装载，直接放行。
    const std::string vision_provider =
        engine_->GetRoleProviderName("nutribot_perception");
    int waited_ms = 0;
    if (vision_provider == "aidgen") {
        const auto vision = WaitForAidgenVision(
            *engine_, [] { return g_stop.load(); }, "nutribot");
        if (vision.aborted) {
            LOG_INFO("nutribot: 启动期收到退出信号，跳过视觉装配");
            return false;
        }
        if (!vision.ready) {
            LOG_ERROR("nutribot: VL 未就绪（{}），看菜停用", vision.error.c_str());
            return false;
        }
        waited_ms = vision.waited_ms;
    } else {
        LOG_INFO("nutribot: 感知走云端 provider '{}'，跳过本地模型等待",
                 vision_provider.c_str());
    }

    LOG_INFO("nutribot: agent 感知就绪（provider={}，营养库 {} 道，启动等待 {:.1f}s）",
             vision_provider.c_str(),
             nutrition_table_.has_value() ? nutrition_table_->size() : 0,
             waited_ms / 1000.0);
    return true;
}

int ResidentSession::Run() {
    // 每个 100ms tick 打印 5 个流程的入/出会非常吵，默认关闭；
    // 设置 PROSOPHOR_TRACE_TICK=1 后启用，用于排查主循环卡在哪一步。
    static const bool trace_tick = std::getenv("PROSOPHOR_TRACE_TICK") != nullptr;
    while (!g_stop.load()) {
        const auto now = prosophor::SteadyClock::Now();
        const auto loop_start = prosophor::SteadyClock::Now();
        int64_t conv_ms = 0;
        int64_t meal_ms = 0;
        int64_t perception_ms = 0;
        int64_t conflict_ms = 0;
        int64_t stats_ms = 0;
        const auto run_step = [&](const char* name, auto&& fn, int64_t& out_ms) {
            if (trace_tick) LOG_INFO("[Tick] {} 入", name);
            const auto step_start = prosophor::SteadyClock::Now();
            fn();
            out_ms = prosophor::SteadyClock::ElapsedMillis(step_start);
            if (trace_tick) LOG_INFO("[Tick] {} 出 {}ms", name, out_ms);
        };

        run_step("TickConversation", [&] { TickConversation(now); }, conv_ms);
        run_step("TickMealWindow", [&] { TickMealWindow(now); }, meal_ms);
        run_step("TickPerception", [&] { TickPerception(now); }, perception_ms);
        run_step("TickPendingConflict", [&] { TickPendingConflict(now); },
                 conflict_ms);
        run_step("LogPeriodicStats", [&] { LogPeriodicStats(); }, stats_ms);

        // 只有被同步 LLM/VL 占住时才会超阈值：直接暴露主循环被堵在哪一段
        const int64_t loop_ms = prosophor::SteadyClock::ElapsedMillis(loop_start);
        if (loop_ms >= 200) {
            LOG_INFO("[时延] 本轮 {}ms（对话={} 饭点={} 感知={} 冲突={} 统计={}）",
                     loop_ms, conv_ms, meal_ms, perception_ms, conflict_ms,
                     stats_ms);
        }

        next_tick_ += Milliseconds(100);
        // 长任务（VL/LLM）结束后不要补跑，直接对齐到当前时刻
        if (next_tick_ < prosophor::SteadyClock::Now()) {
            next_tick_ = prosophor::SteadyClock::Now();
        }
        std::this_thread::sleep_until(next_tick_);
    }

    Shutdown();
    return 0;
}

void ResidentSession::Shutdown() {
    if (shutdown_) return;
    shutdown_ = true;

    LOG_INFO("nutribot: 退出中……");
    // 先停主动调度线程：它的回调捕获 this，不能在本对象析构后继续跑
    if (engine_) engine_->StopActiveTriggers();
    voice_interaction_.StopCapture();
    camera_.Stop();      // 抓帧线程先停：此后不再起新的 ffmpeg 抓拍
    voice_interaction_.StopAudio();
    if (initialized_) {
        ClearLatchedMotion("退出");
        LogFinalStats();
    }
}

void ResidentSession::LogPeriodicStats() {
    if (++tick_count_ % 300 == 0) {   // 每 30s 打点一次统计
        const auto stats = voice_interaction_.stats();
        std::string member_label = no_member_profiles_
            ? "无成员档案"
            : std::to_string(members_ != nullptr ? members_->list_members().size() : 0) +
                  " 位成员";
        LOG_INFO("[统计] 帧={} 句={} 播报={} 丢弃(短/弱/忙)={}/{}/{} ASR空={} | "
                 "抓帧={} agent={} | 场景={} 账本={} | 成员={} 静默跳过={}",
                 stats.feed_frames, stats.utterances, stats.spoken,
                 stats.dropped_short, stats.dropped_lowpeak, stats.dropped_busy,
                 stats.asr_empty, camera_.frame_count(), agent_turns_,
                 current_scene_label_.empty() ? "未知" : current_scene_label_.c_str(),
                 journal_.active() ? "记账中" : "空闲",
                 member_label.c_str(), silence_skipped_);
    }
}

void ResidentSession::LogFinalStats() const {
    const auto stats = voice_interaction_.stats();
    LOG_INFO("nutribot: 统计 帧={} 句={} 播报={} 丢弃(短/弱/忙)={}/{}/{} ASR空={} | "
             "抓帧={} agent={} | 场景={}",
             stats.feed_frames, stats.utterances, stats.spoken, stats.dropped_short,
             stats.dropped_lowpeak, stats.dropped_busy, stats.asr_empty,
             camera_.frame_count(), agent_turns_,
             current_scene_label_.empty() ? "未知" : current_scene_label_.c_str());
    LOG_INFO("nutribot 常驻循环结束");
}


}  // namespace prosophor::nr::nutribot

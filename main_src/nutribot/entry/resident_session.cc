// resident_session 实现 —— 生命周期与线程/退出顺序见 resident_session.h。
#include "entry/resident_session.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <utility>

#include "agent_engine.h"
#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "core/agent_types.h"
#include "core/messages_schema.h"
#include "conversation/diner_context.h"
#include "conversation/speech_rules.h"
#include "voice_interaction/speech_text.h"
#include "entry/nutribot_support.h"
#include "platform/platform.h"
#include "providers/llm/llm_provider.h"
#include "providers/llm_provider_router.h"
#include "conversation/quiz_cli.h"
#include "tools/tool_registry.h"

namespace prosophor::nr::nutribot {

namespace {

// 退出标志：信号处理器只置原子（信号上下文安全的唯一动作）
std::atomic<bool> g_stop{false};

// 本地时间当日分钟数（饭点判定；localtime_r 线程安全）
int LocalMinutesOfDay() {
    const std::time_t wallclock = std::time(nullptr);
    std::tm local{};
    localtime_r(&wallclock, &local);
    return local.tm_hour * 60 + local.tm_min;
}

// 成员档案目录：配置为空时用 <BaseDir>/nutribot_members。常驻形态持久化 ——
// 不像 selftest 每跑一次清空重建，删目录即恢复出厂（重新预置演示成员）
std::string ResolveMemberStoreDir(const prosophor::NutribotConfig& cfg) {
    if (!cfg.member_store_dir.empty()) {
        return prosophor::ExpandHome(cfg.member_store_dir);
    }
    return prosophor::JoinPath(prosophor::ProsophorConfig::BaseDir().string(),
                               "nutribot_members");
}

// 直呼名候选：本餐在场的已识别成员里挑一个该直呼的名字（同上）。
// 名单为空时退回档案库全体，保证饭点问候/小结仍有称呼。
std::string PickSalutation(const std::set<std::string>& active_diner_names,
                           const std::vector<prosophor::nr::domain::Member>& all_members) {
    std::vector<prosophor::nr::domain::Member> active;
    for (const auto& member : all_members) {
        if (active_diner_names.count(member.name) > 0) active.push_back(member);
    }
    if (active.empty()) active = all_members;
    return prosophor::nr::app::DirectAddress(active);
}

// 空档案库预置演示成员：人脸未接，禁忌链路需要"档案库里真有这个人"
// （nutribot.stub_member_name 指向其中之一）。建档走与 selftest 相同的
// 脚本化问答路径 —— 预置档案与交互建档同构，不是另一套旁路。
void SeedDemoMembersIfEmpty(prosophor::nr::infra::JsonMemberRepository& repo) {
    if (!repo.list_members().empty()) return;
    LOG_INFO("nutribot: 档案库为空 —— 预置演示成员（人脸识别接入后可删库重建）");
    const std::vector<RegistrationCase> cases = {
        {"小明", {"小明", "6", "男", "牛奶、花生", "虾", "少盐", "清淡、甜食",
                  "儿童、乳糖不耐受"}},
        {"王奶奶", {"王奶奶", "68", "女", "蛋糕、肥肉", "没有", "低糖、少盐",
                    "软烂、清淡", "2型糖尿病、老年人"}},
    };
    for (const auto& c : cases) {
        ScriptedDialoguePort dialogue(c.answers);
        prosophor::nr::app::RunQuizCli(repo, std::nullopt, dialogue);
    }
}

// 动作命令判定：小模型在历史里看到"同一指令 + 已完成的文字回复"会照抄那条
// 回复而不是重新执行。消息前贴短标记把注意力拉回工具调用。
bool IsActionCommand(const std::string& text) {
    static const std::vector<std::string> action_words = {
        "爪子", "机械爪", "手臂", "表情", "移动", "前进", "后退",
        "走一步", "转身", "状态", "张开", "闭上", "合上", "握"};
    for (const auto& word : action_words) {
        if (text.find(word) != std::string::npos) return true;
    }
    return false;
}

// 环境噪音过滤：ASR 会把电视/人声碎片转成 "."、"The." 这类句子，
// 送进 LLM 会污染历史。口语指令以中文为主：至少两个汉字才算有效话轮。
bool IsMeaningfulUtterance(const std::string& text) {
    int cjk = 0;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte & 0xC0) == 0x80) continue;   // 续接字节
        if (byte >= 0xE4 && byte <= 0xE9) ++cjk;   // UTF-8 常用汉字首字节
    }
    return cjk >= 2;
}

// 定向话轮判定：只有提问、命令或明确直呼才值得送进 LLM。
// 环境里的电视/有声书多是陈述句；但"你好/喂/在吗"这类是明确对设备说话，
// 不能一起挡掉（实测只答疑问句会把问候全部忽略）。
bool IsDirectedUtterance(const std::string& text) {
    if (IsActionCommand(text)) return true;
    static const std::vector<std::string> directed_markers = {
        // 疑问
        "吗", "？", "?", "什么", "怎么", "哪", "推荐", "可以",
        "多少", "是否", "能不能", "要不要", "怎么样",
        // 直呼/问候
        "你好", "您好", "喂", "嗨", "在吗",
        "早上好", "中午好", "晚上好"};
    for (const auto& marker : directed_markers) {
        if (text.find(marker) != std::string::npos) return true;
    }
    return false;
}

// 电机是锁存语义：一次 move 指令后固件保持目标速度，只有显式 stop 才停。
// 启动与退出时各清一次，防止带着锁存目标无人看管地跑。
void ClearLatchedMotion(const char* occasion) {
    if (!prosophor::ProsophorConfig::GetInstance().robot.enabled) return;
    auto& registry = prosophor::ToolRegistry::GetInstance();
    if (!registry.HasTool("robot_move")) return;
    const std::string result =
        registry.ExecuteTool("robot_move", {{"direction", "stop"}});
    LOG_INFO("robot: {}清运动目标 -> {}", occasion, result.c_str());
}

}  // namespace

ResidentSession::ResidentSession()
    : config_(prosophor::ProsophorConfig::GetInstance()),
      voice_cfg_(config_.voice),
      stub_vision_(config_.nutribot.stub_member_name),
      announcer_({voice_cfg_.speak_min_gap_ms}) {}

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

    next_tick_ = prosophor::SteadyClock::Now();
    perception_period_ = prosophor::Milliseconds(
        config_.camera.perception_interval_ms > 0 ? config_.camera.perception_interval_ms
                                                 : 3000);
    idle_perception_period_ = prosophor::Milliseconds(
        config_.camera.idle_interval_ms > 0 ? config_.camera.idle_interval_ms : 15000);
    next_perception_at_ = prosophor::SteadyClock::Now();
    next_scene_at_ = prosophor::SteadyClock::Now();
    next_meal_retry_at_ = prosophor::SteadyClock::Now();
    next_vl_probe_at_ = prosophor::SteadyClock::Now();
    run_started_at_ = prosophor::SteadyClock::Now();

    StartWatchdog();
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

    // 桩人脸：人脸识别未接，固定一位档案成员"在场"（vision_stub.h）。
    if (!members_->get_member(stub_vision_.member_name()).has_value()) {
        LOG_ERROR("nutribot: 桩成员「{}」不在档案库 {}，禁忌提醒不会触发"
                  "（改 settings.json 的 nutribot.stub_member_name 为库里已有成员）",
                  stub_vision_.member_name().c_str(), members_dir.c_str());
    }

    // 看菜检测器与用餐监控器：懒装配（aidgen 就绪是异步三段式，约 4s）。
    // 场景 FSM 按场景节拍驱动，与 VL 懒装配无关。
    scene_period_ = prosophor::Milliseconds(
        config_.camera.scene_interval_ms > 0 ? config_.camera.scene_interval_ms : 3000);
    if (camera_ok_) {
        prosophor::nr::infra::MotionConfig motion_cfg;
        motion_cfg.pixel_threshold = config_.camera.motion_pixel_threshold;
        scene_monitor_ = std::make_unique<prosophor::nr::infra::SceneMonitor>(
            prosophor::nr::domain::MakeHoldThresholds(
                static_cast<int>(scene_period_.count())),
            motion_cfg);
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
    LOG_INFO("nutribot: 感知路 {}（摄像头 {} / 场景 FSM {} / 桩成员「{}」/ 饭点 {} 段 / 营养库 {} 道）",
             camera_ok_ ? "待命，VL 就绪后自动开看" : "停用",
             camera_ok_ ? "ok" : "失败",
             scene_monitor_ != nullptr ? "已接" : "未接",
             stub_vision_.member_name().c_str(), schedule_.windows().size(),
             nutrition_table_.has_value() ? nutrition_table_->size() : 0);
}

void ResidentSession::InitEngineSession() {
    session_id_ = engine_->CreateSession("nutribot", "常驻语音会话");
    LOG_INFO("nutribot 常驻回路已启动：采集={} 播放={} 会话={}",
             voice_cfg_.capture_device.c_str(), voice_cfg_.playback_device.c_str(),
             session_id_.c_str());
    LOG_INFO("  >>> 对着麦克风说一句话，或把一道菜放到摄像头前试试（Ctrl-C 退出）");

    // 工具执行跟踪：动作命令的"没执行就重问"兜底用。回调与主循环同线程，
    // 单个 bool 足够（不加原子）。注意：AgentEngine 不提供移除回调的 API，
    // 因此本对象必须活到进程退出（RunResident 的栈对象满足；main 随后 _Exit）。
    turn_had_tool_ = false;
    engine_->AddOutputCallback([this](
                                 const std::string&, const std::string&,
                                 prosophor::AgentRuntimeState state,
                                 const std::string&,
                                 const std::optional<prosophor::MessageSchema>&,
                                 const std::string&) {
        if (state == prosophor::AgentRuntimeState::EXECUTING_TOOL ||
            state == prosophor::AgentRuntimeState::TOOL_USE) {
            turn_had_tool_ = true;
        }
    });
}

void ResidentSession::StartWatchdog() {
    // SDK 没有取消 API，卡死只能有界强退。心跳由 VL/LLM 调用两侧打点；
    // 只有 in_flight>0 才判超时。放在所有可失败初始化之后启动。
    const int inference_timeout_ms =
        std::max(30000, config_.nutribot.inference_timeout_ms);
    watchdog_stop_.store(false);
    inference_watchdog_ = std::thread([this, inference_timeout_ms] {
        while (!watchdog_stop_.load()) {
            std::this_thread::sleep_for(prosophor::Seconds(2));
            if (g_inference_in_flight.load() <= 0) continue;
            const int64_t stalled_ms = SteadyNowMs() - g_inference_heartbeat_ms.load();
            if (stalled_ms > inference_timeout_ms) {
                LOG_ERROR("nutribot: 推理卡死 {}s（阈值 {}s）—— watchdog 强退（124），"
                          "等待外部重启",
                          stalled_ms / 1000, inference_timeout_ms / 1000);
                std::_Exit(124);
            }
        }
    });
    LOG_INFO("nutribot: 推理看门狗已启动（阈值 {}s）", inference_timeout_ms / 1000);
}

void ResidentSession::InitVisionPerception(
    const std::shared_ptr<prosophor::LLMProvider>& provider,
    prosophor::SteadyClock::TimePoint now) {
    // 闭集模式（知识库口径）：候选来自营养库菜名 —— assets/labels.tsv
    // 的 59 个"知识库种子"菜名被营养库 66 道全覆盖。8B 基准：闭集
    // 严格 Top-1 22/72，开放命名只有 2/72（3B 时代"闭集更差"的结论
    // 不再适用）。
    // 连续两帧稳定性门控只挡抖动型幻觉：非餐桌画面（洞洞板/机械臂）
    // 上 8B 会稳定回同一串候选，实测连过两轮并被当真。故识别之外
    // 另加一道独立的存在性二分类校验（吃同一帧；prompt 不带候选表，
    // 实测菜图"有"/非餐桌"没有"）；主动播报再叠加场景静默门控。
    std::vector<std::string> candidates;
    if (nutrition_table_.has_value()) {
        for (const auto& dish : nutrition_table_->dishes()) {
            candidates.push_back(dish.name);
        }
    }
    detector_ = std::make_unique<prosophor::nr::app::VlFoodDetector>(
        MakeAidgenVisionBackend(provider, nullptr, &vl_last_reply_),
        /*min_interval_seconds=*/3.0, std::move(candidates));
    presence_backend_ = MakeAidgenVisionBackend(provider, nullptr, nullptr);

    // 营养富化装饰器：知识库可用才包（包住 VL 检测器后，规则引擎与
    // 账本拿到的菜就带每份营养；知识库缺失则行为与接库前完全一致）
    prosophor::nr::app::FoodDetectorPort* detector_port = detector_.get();
    if (nutrition_table_.has_value()) {
        enriched_detector_ = std::make_unique<
            prosophor::nr::app::NutritionEnrichingDetector>(
            detector_.get(), &*nutrition_table_);
        detector_port = enriched_detector_.get();
    }
    monitor_ = std::make_unique<prosophor::nr::app::MealMonitor>(
        &stub_vision_, members_.get(), nullptr, scene_monitor_.get(), detector_port);
    LOG_INFO("nutribot: VL 就绪，感知路启用（后台装载耗时 {:.1f}s）",
             prosophor::SteadyClock::ToMillis(now - run_started_at_) / 1000.0);
}

int ResidentSession::Run() {
    while (!g_stop.load()) {
        const auto now = prosophor::SteadyClock::Now();

        TickVisionLoader(now);
        TickScene(now);
        if (!TickConversation(now)) {
            next_tick_ += Milliseconds(100);
            std::this_thread::sleep_until(next_tick_);
            continue;
        }
        TickMealWindow(now);
        TickPerception(now);
        LogPeriodicStats();

        next_tick_ += Milliseconds(100);
        std::this_thread::sleep_until(next_tick_);
    }

    Shutdown();
    return 0;
}

void ResidentSession::TickVisionLoader(prosophor::SteadyClock::TimePoint now) {
    // 感知件懒装配：每 10s 探一次 VL 就绪（三段式异步），就绪才构造检测器/
    // 监控器；30s 仍不就来一句告警（装载失败/未装配），之后不再刷屏
    if (camera_ok_ && monitor_ == nullptr && now >= next_vl_probe_at_) {
        next_vl_probe_at_ = now + Seconds(10);
        auto provider =
            prosophor::LlmProviderRouter::GetInstance().GetProviderByName("aidgen");
        if (provider != nullptr && provider->IsLoaded() &&
            provider->HasVisionConfig()) {
            InitVisionPerception(provider, now);
        } else if (!vl_probe_warned_ && now - run_started_at_ > Seconds(30)) {
            vl_probe_warned_ = true;
            LOG_WARN("nutribot: VL 30s 未就绪（aidgen {}），看菜保持停用：{}",
                     provider == nullptr
                         ? "未装配"
                         : (provider->IsLoading() ? "装载中" : "装载失败"),
                     provider == nullptr ? "" : provider->GetLoadError().c_str());
        }
    }

}

void ResidentSession::TickScene(prosophor::SteadyClock::TimePoint now) {
    // ④ 场景节拍：按 scene_period_ 取最新帧判定一次（纯 opencv ~30ms，不耗
    // NPU）—— 刻意不受 !speaking 门控：播报期场景照常推进，否则"安静 90s"
    // 这类时间常数会被语音活动悄悄拉长；也要比 VL 快，起餐探测才不被慢节拍吃掉
    if (scene_monitor_ != nullptr && now >= next_scene_at_) {
        const std::string frame_path = camera_.LatestFrame();
        if (!frame_path.empty() && frame_path != last_scene_frame_path_) {
            last_scene_frame_path_ = frame_path;
            next_scene_at_ = now + scene_period_;   // 判定节拍固定（抓帧更勤只赚新鲜度）
            const prosophor::nr::app::ImageFrame frame{frame_path};
            const std::string previous_label = current_scene_label_;
            current_scene_label_ =
                scene_monitor_->update(&frame, faces_latest_, foods_latest_);

            if (current_scene_label_ != previous_label) {
                LOG_INFO("[场景] {} → {}（运动 {:.3f} / 人脸 {} / 菜 {}，累计转移 {}）",
                         previous_label.empty() ? "（启动）" : previous_label.c_str(),
                         current_scene_label_.c_str(), scene_monitor_->last_motion_ratio(),
                         faces_latest_ ? "有" : "无", foods_latest_ ? "有" : "无",
                         scene_monitor_->transitions());
                if (current_scene_label_ == "用餐中") {
                    journal_.BeginMeal(now);   // 开饭：开始记这顿的账
                    active_diner_names_.clear();   // 新的一餐：在场成员重新认定
                } else if (current_scene_label_ == "餐后") {
                    if (auto summary = journal_.EndMeal(now);
                        summary.has_value() && config_.meal.announce) {
                        pending_meal_summary_ = std::move(summary);   // 小节话术稍后播
                    }
                }
                // 状态变了立刻看一眼桌面：把"起餐时菜还没被 VL 认出来"的
                // 虚惊回落窗口压到一轮 VL 之内（见 scene.cc 的窗口注记）
                next_perception_at_ = now;
            }
            // 有动静也把下一轮看菜拉到眼前：空闲态干等 15s 的节拍是"放菜到
            // 播报"时延的大头（实测 22s 里占 15s）—— 动静过去后自然退回慢节拍
            if (scene_monitor_->last_motion_ratio() >
                prosophor::nr::domain::SceneStateMachine::MOTION_LOW) {
                if (next_perception_at_ > now) next_perception_at_ = now;
            }
        }
    }

}

bool ResidentSession::TickConversation(prosophor::SteadyClock::TimePoint now) {
    // ① 话轮：用户说话 → 会话回复 → 播报（最高优先）。响应路不受访客
    // 静默约束 —— 被点名要能回应，静默只针对"主动开口"
    if (!voice_interaction_.IsSpeaking()) {
        if (auto text = voice_interaction_.PollAsrResult(0)) {
            if (!IsMeaningfulUtterance(*text)) {
                LOG_INFO("[忽略] 无有效中文（{}）", text->c_str());
                return false;
            }
            if (!IsDirectedUtterance(*text)) {
                LOG_INFO("[忽略] 非定向话轮（{}）", text->c_str());
                return false;
            }
            LOG_INFO("[用户] {}", text->c_str());
            // 语气自适应：在场成员档案压成提示块随消息注入（system 保持
            // 稳定，KV 前缀不受影响；块内容见角色 prompt 的成员约定）
            std::vector<prosophor::nr::domain::Member> diners;
            if (!active_diner_names_.empty()) {
                for (const auto& member : members_->list_members()) {
                    if (active_diner_names_.count(member.name) > 0) {
                        diners.push_back(member);
                    }
                }
            }
            const std::string prompt_block =
                prosophor::nr::app::ComposeDinerPromptBlock(diners);
            // 动作命令贴短标记：紧邻生成点，抵消"照抄上一轮已完成回复"的惯性
            const std::string action_prefix =
                IsActionCommand(*text) ? "【动作命令】" : "";
            const std::string user_message = prompt_block + action_prefix + *text;
            // 话轮里的每次 LLM 调用都纳入推理心跳（重问也算）
            const auto send_turn = [&](const std::string& message) {
                InferenceHeartbeatGuard heartbeat;
                return engine_->SendUserMessage(session_id_, message);
            };
            turn_had_tool_ = false;
            std::string reply = send_turn(user_message);
            // 动作未执行兜底：小模型会照抄历史里"已完成"的文字回复而不调用工具
            //（实测连说两遍"做个开心的表情"，第二遍只回"做得开心"）。动作命令
            // 但本轮无工具执行 → 重问一次并强调必须调用。
            if (IsActionCommand(*text) && !turn_had_tool_) {
                LOG_INFO("[动作未执行] 重问一次（原话: {}）", reply.c_str());
                turn_had_tool_ = false;
                reply = send_turn(
                    prompt_block +
                    "（旁白：这是动作命令，必须调用工具执行，不要用一句话代替）\n" +
                    action_prefix + *text);
            }
            // 沉默兜底：0.5B 对"可以不接话"的出口过拟合（连"有什么推荐"都
            // 沉默），规则认为该答就重问一次 —— 话术质量次要，把话接住优先
            if (!reply.empty() && prosophor::voice::IsSilentReply(reply) &&
                prosophor::nr::app::ShouldAnswerUtterance(*text)) {
                LOG_INFO("[沉默] 但句子需要回应，重问一次（原话: {}）", reply.c_str());
                reply = send_turn(
                    prompt_block +
                    "（旁白：刚才你判断保持沉默，但这句话是直接问你的，请作出简短回答）\n" +
                    action_prefix + *text);
            }
            // 空回复兜底：0.5B 偶发一上来就吐 EOS（零内容），与沉默过拟合同源，
            // 重问一次；问句/动作命令被吞同样是"把话接住"优先
            if (reply.empty()) {
                LOG_INFO("[空回复] 重问一次");
                reply = send_turn(
                    prompt_block +
                    "（旁白：刚才你没有输出任何内容，请直接回应这句话）\n" +
                    action_prefix + *text);
            }
            // 小模型会把重问旁白当正文复述（实测："刚才你没有输出任何内容，
            // 请直接回应这句话。"被原样念出来）—— 复述不是回复，按失败处理
            const bool retry_narration_echo =
                reply.find("刚才你没有输出任何内容") != std::string::npos ||
                reply.find("刚才你判断保持沉默") != std::string::npos ||
                reply.find("这是动作命令") != std::string::npos;
            if (reply.empty() || retry_narration_echo) {
                LOG_WARN("nutribot: 会话无有效回复（{}）",
                         retry_narration_echo ? "复述了重问旁白" : "空");
            } else if (prosophor::voice::IsSilentReply(reply)) {
                // LLM 自主判断不接话（角色 prompt 的沉默约定）：话进了对话
                // 上下文，但不开口 —— 桌边闲聊不该句句插嘴
                LOG_INFO("[沉默] 判断无需回应，仅记录上下文（原话: {}）", reply.c_str());
            } else {
                const std::string speech = prosophor::voice::SanitizeForSpeech(
                    prosophor::voice::StripSilenceMarker(reply),
                    voice_cfg_.speak_max_chars);
                if (!speech.empty()) {
                    LOG_INFO("[助手] {}", speech.c_str());
                    voice_interaction_.Speak(speech);
                    announcer_.NoteConversationSpeech(now);   // 主动播报不追着对话抢麦
                }
            }
        }
    }

    return true;
}

void ResidentSession::TickMealWindow(prosophor::SteadyClock::TimePoint now) {
    // ③ 饭点：进入窗口问候一次（节流中挂起，按感知节拍重试）；排在感知前 ——
    // 问候是瞬时的，不该排在一次秒级 VL 推理后面。问候承诺"我帮你看看"，
    // 故与感知同步启用（VL 未就绪时不问候）
    if (monitor_ != nullptr && !voice_interaction_.IsSpeaking()) {
        const std::vector<prosophor::nr::domain::Member> all_members =
            members_->list_members();   // 本 tick 快照：打招呼查询用
        const auto window = schedule_.Lookup(LocalMinutesOfDay());
        const std::string meal_now = window.has_value() ? window->name : std::string();
        if (meal_now != current_meal_name_) {
            current_meal_name_ = meal_now;
            if (!meal_now.empty()) {
                LOG_INFO("[饭点] 进入「{}」窗口", meal_now.c_str());
                pending_meal_greeting_ = meal_now;
            }
        }
        // 访客静默（书 1.2/3.3）：本帧未识别到注册成员（客人或无人）时，
        // 主动播报整体转静默 —— 问候挂在 pending 上等下轮（只是推迟，不丢）；
        // 小结没有重播时机，放弃并计数（下次"餐后"会有新账）
        // 饭点问候（受 meal.announce 管：报菜名/禁忌提醒不受它管，那是安全/信息类）
        if (pending_meal_greeting_.has_value() && config_.meal.announce &&
            now >= next_meal_retry_at_) {
            next_meal_retry_at_ = now + perception_period_;
            if (visitor_mode_) {
                ++silence_skipped_;   // 访客在座：问候等下轮
            } else {
                const std::string speech = announcer_.PlanMealEntry(
                    *pending_meal_greeting_, now,
                    PickSalutation(active_diner_names_, all_members));
                if (!speech.empty()) {
                    LOG_INFO("[主动] {}", speech.c_str());
                    voice_interaction_.Speak(speech);
                    pending_meal_greeting_.reset();
                }
            }
        }
        // 餐后小结（进入"餐后"时记账产出；节流中挂起，按感知节拍重试）
        if (pending_meal_summary_.has_value() && now >= next_meal_retry_at_) {
            next_meal_retry_at_ = now + perception_period_;
            if (visitor_mode_) {
                ++silence_skipped_;
                pending_meal_summary_.reset();
            } else {
                const std::string speech = announcer_.PlanMealSummary(
                    *pending_meal_summary_, now,
                    PickSalutation(active_diner_names_, all_members));
                if (!speech.empty()) {
                    LOG_INFO("[主动] {}", speech.c_str());
                    const std::string sanitized = prosophor::voice::SanitizeForSpeech(
                        speech, voice_cfg_.speak_max_chars);
                    if (!sanitized.empty()) voice_interaction_.Speak(sanitized);
                    pending_meal_summary_.reset();
                }
            }
        }
    }

}

void ResidentSession::TickPerception(prosophor::SteadyClock::TimePoint now) {
    // ② 感知节拍：有新帧、未在播报、过节拍 → 看菜 → 规则 → 播报计划。
    // 节拍随场景状态收放：备餐/用餐全速（3s），空闲/餐后放慢（15s）—— VL 是
    // 板上最大的一块 NPU 开销，桌上没饭时不必每 3 秒看一眼
    // 刻意不带 !voice_interaction_.IsSpeaking()：播报期麦克风已闭麦、看菜与 TTS 分属两条
    // 线程，提醒不该因为"机器人正在说话"被推后（播报队列深度 1 丢旧保新）
    if (monitor_ != nullptr && now >= next_perception_at_) {
        const std::string frame_path = camera_.LatestFrame();
        if (!frame_path.empty() && frame_path != last_frame_path_) {
            last_frame_path_ = frame_path;
            const bool scene_active =
                (current_scene_label_ == "备餐中" || current_scene_label_ == "用餐中");
            // 节拍从本轮起点起算（VL 耗时不计入）
            next_perception_at_ =
                now + (scene_active ? perception_period_ : idle_perception_period_);
            const prosophor::nr::app::ImageFrame frame{frame_path};
            const auto pkg = monitor_->Tick(&frame);
            // 幻觉抑制：本帧出现过的菜记连续计数，连续 ≥2 帧才进入场景包。
            // 只能挡抖动型幻觉；非餐桌画面上 8B 会**稳定**回同一串菜名，
            // 这类要靠闭集 prompt 的空输出出口与场景静默门控（见装配处注释）。
            std::map<std::string, int> next_streak;
            for (const auto& food : pkg.foods) {
                const auto it = food_streak_.find(food.name);
                next_streak[food.name] = (it == food_streak_.end() ? 1 : it->second + 1);
            }
            food_streak_ = std::move(next_streak);
            auto stable_pkg = pkg;
            stable_pkg.foods.clear();
            for (const auto& food : pkg.foods) {
                if (food_streak_[food.name] >= 2) stable_pkg.foods.push_back(food);
            }
            // 存在性校验：稳定集合变化时（不是每个感知节拍）问一次"图里有没有
            // 餐桌上的饭菜"。闭集识别对非餐桌画面会稳定硬选，时间一致性挡不住；
            // 这个问题不带候选表，实测能答"没有"。同一集合只验一次；换集合才重验。
            // 无法判断/后端失败时 fail-open（不误伤真实上菜）。
            const bool has_stable_foods = !stable_pkg.foods.empty();
            if (!has_stable_foods) {
                last_presence_food_key_.clear();   // 撤桌：下一桌重新校验
                last_presence_verdict_ = true;
                consecutive_presence_rejections_ = 0;
                framing_warned_ = false;
            } else {
                const std::string candidate_key =
                    prosophor::nr::app::FoodSetKey(stable_pkg.foods);
                bool presence_checked = false;
                if (presence_backend_ && candidate_key != last_presence_food_key_) {
                    last_presence_food_key_ = candidate_key;
                    ++food_gate_checks_;
                    presence_checked = true;
                    const auto reply =
                        presence_backend_(frame_path, prosophor::nr::app::FoodPresenceQuestion());
                    const auto verdict =
                        reply.has_value()
                            ? prosophor::nr::app::ParseFoodPresenceReply(*reply)
                            : std::nullopt;
                    last_presence_verdict_ = verdict.value_or(true);
                    LOG_INFO("nutribot: 菜品存在性校验 -> {}（稳定 {} 道；模型原话：{}）",
                             !verdict.has_value() ? "无法判断，放行"
                             : (*verdict ? "有菜，放行" : "没有菜，拒绝"),
                             stable_pkg.foods.size(),
                             reply.has_value() ? reply->c_str() : "（后端失败）");
                }
                if (!last_presence_verdict_) {
                    if (presence_checked) {
                        ++food_gate_rejections_;   // 按"校验过的集合"计，不按 tick 数
                        ++consecutive_presence_rejections_;
                        // 现场取证：拒绝帧固定留存一张（frame_* 会被抓帧环覆盖）
                        const std::string snapshot_path = prosophor::JoinPath(
                            config_.camera.output_dir, "rejected_latest.jpg");
                        if (prosophor::CopyFile(frame_path, snapshot_path)) {
                            LOG_INFO("nutribot: 拒绝帧已留存 {}", snapshot_path.c_str());
                        } else {
                            LOG_WARN("nutribot: 拒绝帧留存失败（{} -> {}）",
                                     frame_path.c_str(), snapshot_path.c_str());
                        }
                        if (consecutive_presence_rejections_ >= 3 && !framing_warned_) {
                            framing_warned_ = true;
                            LOG_WARN("nutribot: 连续 {} 次稳定菜品被存在性校验拒绝 —— "
                                     "相机可能没对准餐桌（现场帧 {}）",
                                     consecutive_presence_rejections_,
                                     snapshot_path.c_str());
                        }
                    }
                    stable_pkg.foods.clear();   // 假菜也不进账本/规则/FSM
                } else if (presence_checked) {
                    consecutive_presence_rejections_ = 0;   // 恢复正常：重新计数
                    framing_warned_ = false;
                }
            }
            // 事件也按稳定菜过滤：announcer_ 的单轮提醒策略是"安全项不等稳定"，
            // 但装配层不该把单帧幻觉升级成禁忌/营养警告。代价是提醒晚一拍
            // （活跃场景 3s），换掉"空画面触发假警告"。
            std::set<std::string> stable_food_names;
            for (const auto& food : stable_pkg.foods) {
                stable_food_names.insert(food.name);
            }
            stable_pkg.events.erase(
                std::remove_if(stable_pkg.events.begin(), stable_pkg.events.end(),
                               [&](const prosophor::nr::domain::ReminderEvent& event) {
                                   return stable_food_names.count(event.food) == 0;
                               }),
                stable_pkg.events.end());
            // 场景 FSM 的输入信号取自最近一轮 VL（未答先按"有人有菜"的旧语义）
            faces_latest_ = !stable_pkg.members.empty();
            foods_latest_ = !stable_pkg.foods.empty();
            journal_.Observe(stable_pkg);   // 用餐中的账：菜品/热量/提醒累计
            // 本餐在场成员认定（识别到谁记谁；开餐清空、跨轮累积）
            for (const auto& member : pkg.members) {
                if (!member.name.empty()) active_diner_names_.insert(member.name);
            }
            const bool visitor_now = prosophor::nr::app::VisitorSilence(pkg.members);
            if (visitor_now != visitor_mode_) {
                LOG_INFO("[在场] {}（本餐已识别：{}）", visitor_now ? "无注册成员，转静默" : "识别到家庭成员",
                         active_diner_names_.empty() ? "无" : JoinNames(active_diner_names_).c_str());
                visitor_mode_ = visitor_now;
            }
            LOG_DEBUG("nutribot: 看菜 -> 稳定 {} 道（原始 {}）: {}",
                      stable_pkg.foods.size(), pkg.foods.size(),
                      JoinFoodNames(stable_pkg.foods).c_str());
            // 稳定集合变化时留档（含模型原话）：误报发生时能直接定位是
            // "模型看图看错"还是"解析/匹配出错"，不用复现现场
            const std::string stable_key =
                prosophor::nr::app::FoodSetKey(stable_pkg.foods);
            if (stable_key != last_logged_stable_key_) {
                last_logged_stable_key_ = stable_key;
                LOG_INFO("nutribot: 看菜稳定集合 -> 稳定 {} 道（原始 {}）：{} | 模型原话：{}",
                         stable_pkg.foods.size(), pkg.foods.size(),
                         JoinFoodNames(stable_pkg.foods).c_str(),
                         vl_last_reply_.empty() ? "（无/失败）" : vl_last_reply_.c_str());
            }
            // 静默期（访客 / 空桌待机）也照喂 PlanPerception —— 内部登记/
            // 去重状态要随感知推进，恢复后能接着补播。空桌待机不主动开口：
            // 非餐桌画面的稳定幻觉正是靠这道场景闸兜底。
            const bool scene_allows_speech = (current_scene_label_ != "空桌待机");
            const std::string speech = announcer_.PlanPerception(
                stable_pkg, now, !visitor_mode_ && scene_allows_speech);
            if (visitor_mode_) ++silence_skipped_;
            if (!speech.empty()) {
                LOG_INFO("[主动] {}", speech.c_str());
                const std::string sanitized = prosophor::voice::SanitizeForSpeech(
                    speech, voice_cfg_.speak_max_chars);
                if (!sanitized.empty()) voice_interaction_.Speak(sanitized);
            }
        }
    }

}

void ResidentSession::LogPeriodicStats() {
    if (++tick_count_ % 300 == 0) {   // 每 30s 打点一次统计
        const auto stats = voice_interaction_.stats();
        const auto ann = announcer_.stats();
        std::string member_label;
        if (visitor_mode_) {
            member_label = "访客静默";
        } else if (active_diner_names_.empty()) {
            member_label = "未识别";
        } else {
            member_label = JoinNames(active_diner_names_);
        }
        LOG_INFO("[统计] 帧={} 句={} 播报={} 丢弃(短/弱/忙)={}/{}/{} ASR空={} | "
                 "抓帧={} 看菜={} 存在性校验(查/拒)={}/{} "
                 "提醒(播/挂)={}/{} 报菜名={} 饭点={} 小结={} "
                 "节流推迟={} 场景静默={} | "
                 "场景={} 转移={} 账本={} | 成员={} 静默跳过={}",
                 stats.feed_frames, stats.utterances, stats.spoken,
                 stats.dropped_short, stats.dropped_lowpeak, stats.dropped_busy,
                 stats.asr_empty, camera_.frame_count(),
                 detector_ != nullptr ? detector_->query_count() : 0,
                 food_gate_checks_, food_gate_rejections_,
                 ann.reminders_spoken, ann.pending_reminders,
                 ann.food_announcements, ann.meal_announcements, ann.meal_summaries,
                 ann.suppressed_by_gap, ann.suppressed_by_scene,
                 current_scene_label_.empty() ? "未知" : current_scene_label_.c_str(),
                 scene_monitor_ != nullptr ? scene_monitor_->transitions() : 0,
                 journal_.active() ? "记账中" : "空闲",
                 member_label.c_str(), silence_skipped_);
    }
}

void ResidentSession::Shutdown() {
    if (shutdown_) return;
    shutdown_ = true;

    watchdog_stop_.store(true);
    if (inference_watchdog_.joinable()) inference_watchdog_.join();
    LOG_INFO("nutribot: 退出中……");
    voice_interaction_.StopCapture();
    camera_.Stop();      // 抓帧线程先停：此后不再起新的 ffmpeg 抓拍
    voice_interaction_.StopAudio();
    if (initialized_) {
        ClearLatchedMotion("退出");
        LogFinalStats();
    }
}

void ResidentSession::LogFinalStats() const {
    const auto stats = voice_interaction_.stats();
    const auto ann = announcer_.stats();
    LOG_INFO("nutribot: 统计 帧={} 句={} 播报={} 丢弃(短/弱/忙)={}/{}/{} ASR空={} | "
             "抓帧={} 看菜={} 存在性校验(查/拒)={}/{} 提醒(播/挂)={}/{} "
             "报菜名={} 饭点={} 小结={} 节流推迟={} 场景静默={} | 场景={} 转移={}",
             stats.feed_frames, stats.utterances, stats.spoken, stats.dropped_short,
             stats.dropped_lowpeak, stats.dropped_busy, stats.asr_empty,
             camera_.frame_count(), detector_ != nullptr ? detector_->query_count() : 0,
             food_gate_checks_, food_gate_rejections_,
             ann.reminders_spoken, ann.pending_reminders, ann.food_announcements,
             ann.meal_announcements, ann.meal_summaries,
             ann.suppressed_by_gap, ann.suppressed_by_scene,
             current_scene_label_.empty() ? "未知" : current_scene_label_.c_str(),
             scene_monitor_ != nullptr ? scene_monitor_->transitions() : 0);
    LOG_INFO("nutribot 常驻循环结束");
}

}  // namespace prosophor::nr::nutribot

// resident_session：常驻回路会话（Init / Run / Shutdown）
//
// 实现按职责拆分：
//   resident_lifecycle.cc  初始化/主循环/退出/统计
//   resident_voice.cc      唤醒/聆听/播报状态机
//   resident_agent.cc      agent 回合/周期感知 JSON
//   resident_meal.cc       饭点/小结/场景与餐账
//   tools/nutribot_tools.cc        成员档案工具注册
//   tools/robot_control_tools.cc   机器人动作工具注册
//
// 把初始化、主循环与退出收尾收进一个对象：
//   Init()  信号/引擎/语音/感知/视觉装配（阻塞等模型装载）/引擎会话/计时器
//           （失败只可能来自语音后端；视觉装载失败只降级为看菜停用）
//   Run()   100ms tick 主循环（唯一 agent/视觉调用点），返回前调用 Shutdown()
//   Shutdown() 顺序敏感：采集线程 -> camera -> playback/voice/ASR/TTS
#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "perception/camera_source.h"
#include "common/time_wrapper.h"
#include "prosophor_core/public/config.h"
#include "meal/dish_nutrition.h"
#include "meal/meal_journal.h"
#include "meal/meal_schedule.h"
#include "storage/member_repository.h"
#include "conversation/utterance_coalescer.h"
#include "voice_interaction/tts_chunker.h"
#include "voice_interaction/voice_interaction.h"

namespace prosophor {
class AgentEngine;
}  // namespace prosophor

namespace prosophor::nr::nutribot {

class ResidentSession {
public:
    ResidentSession();
    ~ResidentSession();
    ResidentSession(const ResidentSession&) = delete;
    ResidentSession& operator=(const ResidentSession&) = delete;

    /// 初始化；仅语音后端失败会返回 false（reason 写入 error）。
    bool Init(std::string& error);

    /// 运行主循环；返回前调用 Shutdown()。
    int Run();

    /// 幂等、顺序敏感的退出收尾；Init 前后调用都安全。
    void Shutdown();

private:
    // 100ms tick 的各阶段（按顺序调用）
    void TickConversation(prosophor::SteadyClock::TimePoint now);
    void TickMealWindow(prosophor::SteadyClock::TimePoint now);
    void TickPerception(prosophor::SteadyClock::TimePoint now);

    // ── Agent 驱动 ──
    struct AgentTurnResult {
        std::string reply;
        bool ok = false;
    };
    // 一次任务事件 → prompt → 同一个 agent 会话（带工具）
    // include_table_fact：即使没有冲突也注入最新感知事实（视觉类问题用）
    AgentTurnResult RunAgentTurn(const std::string& utterance,
                                 const std::string& frame_path,
                                 const std::string& event = {},
                                 bool include_table_fact = false);
    // 周期感知：独立 session（无工具、保留文字上下文）的严格 JSON 提取，
    // 图片只服务当轮；结果写入 pending_table_report_，不产生播报
    void RunPerceptionTurn(const std::string& frame_path);
    // 周期感知 JSON 解析出的桌面状态，等本轮 agent 结束后应用
    struct TableReport {
        std::vector<prosophor::nr::domain::FoodItem> foods;
        std::string description;   // 感知模型的一句话描述（冲突时随事实注入）
        bool has_food = false;
        std::string scene;
        bool valid = false;
    };
    // 把感知 JSON 转成桌面状态（菜名对齐 assets 标注 + 营养库）
    TableReport BuildTableReport(const nlohmann::json& args);
    TableReport pending_table_report_;
    void RegisterMemberProfileTool();
    void ApplyPendingTableReport();

    // ── 事件 1：感知事实（菜品 + 冲突 + 时间戳）──
    struct DishConflict {
        std::string member;   // 谁
        std::string dish;     // 营养库规范名
        std::string reason;   // 过敏 / 忌口 / 高糖 / 高钠 / 高脂
        std::string context;  // 档案依据：慢病标签 / 过敏原；空 = 档案没写
        std::string Key() const { return member + "|" + dish + "|" + reason; }
    };
    struct TableFact {
        std::vector<prosophor::nr::domain::FoodItem> foods;
        std::vector<DishConflict> conflicts;
        std::string description;   // 感知模型对本帧的一句话描述
        bool has_food = false;
        std::string scene;
        prosophor::SteadyClock::TimePoint observed_at{};
        bool valid = false;
    };
    // 用本轮感知结果刷新 latest_table_，并标记未提醒过的新冲突
    void UpdateTableFacts(const TableReport& report);
    // 代码侧冲突计算：禁忌/过敏 + 慢病营养阈值（只产事实，不做话术）
    std::vector<DishConflict> FindConflicts(
        const std::vector<prosophor::nr::domain::FoodItem>& foods) const;
    // 对话回合注入的 <当前餐桌> 事实块；过期/无效时返回空
    // include_without_conflicts=false 时只有存在冲突才返回
    std::string ComposeTableFactBlock(bool include_without_conflicts) const;
    // 最新感知事实是否仍在新鲜窗口内
    bool HasFreshTableFact() const;
    // 新冲突的主动提醒事件文本（只播报，不调用动作工具）
    std::string BuildConflictEvent() const;
    // 2.5s 合并窗：用户没开口就把新冲突交给对话会话主动播报
    void TickPendingConflict(prosophor::SteadyClock::TimePoint now);

    TableFact latest_table_;
    std::set<std::string> notified_conflicts_;
    std::optional<prosophor::SteadyClock::TimePoint> pending_conflict_at_;

    // 场景标签切换 → 餐账开/停（由 agent 上报驱动）
    void ApplySceneLabel(const std::string& label, prosophor::SteadyClock::TimePoint now);
    void LogPeriodicStats();

    // ── 对话状态（严格半双工；LLM 推理同步完成，不单独设"思考"态）──
    enum class Phase { kStandby, kListening, kSpeaking };
    void TickStandby(prosophor::SteadyClock::TimePoint now);
    void TickListening(prosophor::SteadyClock::TimePoint now);
    bool IsWake(const std::string& text) const;
    void DispatchConversationTurn(const std::string& text);
    /// 派发后、LLM 首句到手前先播一条轮转填充语；看菜类问题用看菜话术
    void QueueThinkingFiller(bool vision_question);
    /// 流式正文 → 分句 → 逐句入 TTS；done=true 时把残留一并吐出
    void SpeakStreamReady(bool done);
    /// 入队一句播报并进入 kSpeaking；返回 false = 未入队（TTS 不可用/正忙）
    /// queue_behind=true 用于回复：当前填充语还在播时排在其后，不拒播
    bool StartRobotSpeech(const std::string& speech, bool queue_behind = false);
    /// 播报开始：向底盘发 talking 口型，并安排下一次续期
    void BeginSpeakingFace();
    /// 播报期口型表情续期：底盘 talking 只保持 5s，长句需要补发
    void RefreshSpeakingFace(prosophor::SteadyClock::TimePoint now);
    /// agent 回复 → 沉默过滤/净化/播报；proactive=true 时记录 <上次主动播报>。
    /// 返回 false = 沉默、净化后为空或 TTS 未入队。
    bool SpeakAgentReply(const std::string& reply, bool proactive,
                         const char* source = nullptr);
    /// 主动播报只能从待机发起，且用户未在说话、机器人未忙
    bool CanStartProactiveSpeech() const;
    /// reset_input=false 用于"用户还在说"的唤醒路径：不能重置 VAD，否则
    /// 正在累积的需求句会被丢掉
    void EnterListening(prosophor::SteadyClock::TimePoint now,
                        bool reset_input = true);
    void EnterStandby();

    void InitPerception();
    void InitEngineSession();
    /// 阻塞等待 aidgen 模型装载后一次性装配视觉感知；失败只降级（看菜停用）。
    bool InitVisionPerception();
    void LogFinalStats() const;

    prosophor::ProsophorConfig& config_;
    const prosophor::VoiceConfig& voice_cfg_;
    prosophor::AgentEngine* engine_ = nullptr;   // Init 中在信号处理器之后构造
    prosophor::voice::VoiceInteraction voice_interaction_;

    // ── 感知 ──
    prosophor::nr::infra::CameraSource camera_;
    bool camera_ok_ = false;
    std::unique_ptr<prosophor::nr::infra::JsonMemberRepository> members_;

    int agent_turns_ = 0;   // agent 回合数（统计）
    std::string last_proactive_reply_;   // 给下一轮 prompt 参考，是否重复由模型判断

    // ── 对话/记账/播报 ──
    prosophor::nr::app::MealJournal journal_;
    prosophor::nr::app::UtteranceCoalescer utterance_coalescer_;
    Phase phase_ = Phase::kStandby;
    prosophor::SteadyClock::TimePoint awake_until_{};
    prosophor::SteadyClock::TimePoint next_speaking_face_at_{};
    bool speaking_claw_open_ = true;       // 说话时小爪开合微动

    // ── 流式播报 ──
    prosophor::voice::TtsChunker tts_chunker_;   // LLM delta → 完整句
    bool stream_turn_active_ = false;   // 主循环回合进行中（回调只在此期内处理流式）
    bool stream_spoke_ = false;         // 本轮已有正文入 TTS（避免整段重播）
    bool stream_reply_queued_ = false;  // 本轮回复已开始入队（丢弃填充语只做一次）
    bool stream_in_tool_block_ = false; // 流式正文正处于 <tool_call>…</tool_call> 内
    size_t stream_spoken_chars_ = 0;    // 本轮已入 TTS 的码点数（受 speak_max_chars 限制）
    size_t filler_index_ = 0;           // 填充语轮转游标
    bool filler_queued_this_turn_ = false;   // 本轮排过填充语（回复可排在其后）
    std::optional<prosophor::nr::app::DishNutritionTable> nutrition_table_;
    prosophor::nr::app::MealSchedule schedule_;
    std::string session_id_;
    std::string last_turn_reply_;   // 回调在 COMPLETE 时写入，send 包装读取
    bool last_turn_tool_used_ = false;   // 本轮是否执行过工具（无文字时兜底播报）
    std::string perception_session_id_;   // 周期感知专用会话（无工具、保留文字上下文）
    std::string last_perception_reply_;   // 感知会话回调写入，SendUserMessage 同步读

    // ── 主循环跨 tick 状态 ──
    prosophor::SteadyClock::TimePoint next_tick_{};
    prosophor::Milliseconds perception_period_{3000};
    prosophor::Milliseconds idle_perception_period_{15000};
    prosophor::SteadyClock::TimePoint next_perception_at_{};
    prosophor::SteadyClock::TimePoint next_meal_retry_at_{};
    std::string last_frame_path_;
    std::string current_scene_label_;
    std::string current_meal_name_;
    std::optional<std::string> pending_meal_greeting_;
    std::optional<prosophor::nr::app::MealSummary> pending_meal_summary_;
    bool no_member_profiles_ = false;   // 档案库为空：没有可参与提醒的成员
    int silence_skipped_ = 0;
    int tick_count_ = 0;

    bool initialized_ = false;
    bool shutdown_ = false;
};

}  // namespace prosophor::nr::nutribot

// resident_session：常驻回路会话（Init / Run / Shutdown）
//
// 把原先 RunResident 里的初始化、主循环与退出收尾收进一个对象：
//   Init()  信号/引擎/语音/感知/引擎会话/计时器/看门狗（失败只可能来自语音后端）
//   Run()   100ms tick 主循环（唯一 aidgen 调用点），返回前调用 Shutdown()
//   Shutdown() 顺序敏感：watchdog -> 采集线程 -> camera -> playback/voice/ASR/TTS
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "perception/camera_source.h"
#include "common/time_wrapper.h"
#include "config/config.h"
#include "meal/dish_nutrition.h"
#include "meal/meal_journal.h"
#include "meal/meal_monitor.h"
#include "meal/meal_schedule.h"
#include "storage/member_repository.h"
#include "conversation/proactive_announcer.h"
#include "voice_interaction/voice_interaction.h"
#include "perception/scene_monitor.h"
#include "perception/vision_stub.h"
#include "perception/vl_food_detector.h"

namespace prosophor {
class AgentEngine;
class LLMProvider;
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
    // 100ms tick 的各阶段（按顺序调用；TickConversation 返回 false = 本 tick 跳过后续阶段）
    void TickVisionLoader(prosophor::SteadyClock::TimePoint now);
    void TickScene(prosophor::SteadyClock::TimePoint now);
    bool TickConversation(prosophor::SteadyClock::TimePoint now);
    void TickMealWindow(prosophor::SteadyClock::TimePoint now);
    void TickPerception(prosophor::SteadyClock::TimePoint now);
    void LogPeriodicStats();

    void InitPerception();
    void InitEngineSession();
    void InitVisionPerception(const std::shared_ptr<prosophor::LLMProvider>& provider,
                              prosophor::SteadyClock::TimePoint now);
    void StartWatchdog();
    void LogFinalStats() const;

    prosophor::ProsophorConfig& config_;
    const prosophor::VoiceConfig& voice_cfg_;
    prosophor::AgentEngine* engine_ = nullptr;   // Init 中在信号处理器之后构造
    prosophor::voice::VoiceInteraction voice_interaction_;

    // ── 感知 ──
    prosophor::nr::infra::CameraSource camera_;
    bool camera_ok_ = false;
    std::unique_ptr<prosophor::nr::infra::JsonMemberRepository> members_;
    prosophor::nr::app::StubFaceRecognitionPort stub_vision_;

    std::unique_ptr<prosophor::nr::app::VlFoodDetector> detector_;
    prosophor::nr::app::VisionQueryBackend presence_backend_;
    std::string vl_last_reply_;
    std::unique_ptr<prosophor::nr::app::NutritionEnrichingDetector> enriched_detector_;
    std::unique_ptr<prosophor::nr::app::MealMonitor> monitor_;
    std::unique_ptr<prosophor::nr::infra::SceneMonitor> scene_monitor_;
    prosophor::Milliseconds scene_period_{3000};

    // ── 对话/记账/播报 ──
    prosophor::nr::app::MealJournal journal_;
    prosophor::nr::app::ProactiveAnnouncer announcer_;
    std::optional<prosophor::nr::app::DishNutritionTable> nutrition_table_;
    prosophor::nr::app::MealSchedule schedule_;
    std::string session_id_;
    bool turn_had_tool_ = false;

    // ── 主循环跨 tick 状态 ──
    prosophor::SteadyClock::TimePoint next_tick_{};
    prosophor::Milliseconds perception_period_{3000};
    prosophor::Milliseconds idle_perception_period_{15000};
    prosophor::SteadyClock::TimePoint next_perception_at_{};
    std::map<std::string, int> food_streak_;
    std::string last_logged_stable_key_;
    std::string last_presence_food_key_;
    bool last_presence_verdict_ = true;
    int food_gate_checks_ = 0;
    int food_gate_rejections_ = 0;
    int consecutive_presence_rejections_ = 0;
    bool framing_warned_ = false;
    prosophor::SteadyClock::TimePoint next_scene_at_{};
    prosophor::SteadyClock::TimePoint next_meal_retry_at_{};
    prosophor::SteadyClock::TimePoint next_vl_probe_at_{};
    prosophor::SteadyClock::TimePoint run_started_at_{};
    bool vl_probe_warned_ = false;
    std::string last_frame_path_;
    std::string last_scene_frame_path_;
    std::string current_scene_label_;
    bool faces_latest_ = false;
    bool foods_latest_ = true;
    std::string current_meal_name_;
    std::optional<std::string> pending_meal_greeting_;
    std::optional<prosophor::nr::app::MealSummary> pending_meal_summary_;
    std::set<std::string> active_diner_names_;
    bool visitor_mode_ = false;
    int silence_skipped_ = 0;
    int tick_count_ = 0;

    // ── 看门狗 ──
    std::atomic<bool> watchdog_stop_{false};
    std::thread inference_watchdog_;

    bool initialized_ = false;
    bool shutdown_ = false;
};

}  // namespace prosophor::nr::nutribot

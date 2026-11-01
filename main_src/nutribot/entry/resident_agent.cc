// resident agent 回合：RunAgentTurn（对话/动作）/ RunPerceptionTurn（JSON 感知）
#include "entry/resident_session.h"

#include <cstdio>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "prosophor_core/public/agent_engine.h"
#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "prosophor_core/public/core_types.h"
#include "conversation/phrasing.h"
#include "entry/nutribot_support.h"
#include "tools/robot_control_tools.h"

namespace prosophor::nr::nutribot {

namespace {

// 留证：把实际传给 LLM 的当前帧复制到进程当前目录，方便核对画面与回答
void PersistLlmInputFrame(const std::string& frame_path) {
    if (frame_path.empty()) return;
    const std::string copy_path =
        (std::filesystem::current_path() / "nutribot_llm_input.jpg").string();
    if (prosophor::CopyFile(frame_path, copy_path)) {
        std::error_code size_error;
        const auto size = std::filesystem::file_size(copy_path, size_error);
        LOG_INFO("[流程] LLM 输入帧: {} ({} bytes)", copy_path.c_str(),
                 size_error ? 0ULL : static_cast<unsigned long long>(size));
    } else {
        LOG_WARN("[流程] LLM 输入帧复制失败: {}", frame_path.c_str());
    }
}

// 营养数值（营养库是每份口径）→ 可注入事实块/兜底播报的一句话
std::string DescribeNutrition(const prosophor::nr::domain::Nutrition& nutrition) {
    char text[192];
    std::snprintf(text, sizeof(text),
                  "每份约 %.0f 千卡，蛋白质 %.0f 克，脂肪 %.0f 克，碳水 %.0f 克，"
                  "钠 %.0f 毫克，糖 %.0f 克",
                  nutrition.energy_kcal, nutrition.protein_g, nutrition.fat_g,
                  nutrition.carb_g, nutrition.sodium_mg, nutrition.sugar_g);
    return text;
}

// 事实块新鲜度：超过这个年龄就不再注入对话，让模型回到当前图片
constexpr int table_fact_max_age_ms = 15000;
// 新冲突的合并窗：用户在这段时间内开口就并入用户回合，不单独主动播报
constexpr int conflict_combine_ms = 2500;
// 慢病营养阈值：单道菜达到日上限的 40% 视为明显偏高
constexpr double conflict_daily_ratio = 0.4;
constexpr double daily_sodium_mg = 2000.0;
constexpr double daily_sugar_g = 50.0;
constexpr double daily_fat_g = 70.0;

bool MatchesTerms(const std::string& food_name,
                  const std::vector<std::string>& terms) {
    for (const auto& term : terms) {
        if (term.empty()) continue;
        if (food_name == term || food_name.find(term) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool HasHealthTag(const prosophor::nr::domain::Member& member,
                  const char* tag_part) {
    for (const auto& tag : member.profile.health_tags) {
        if (tag.find(tag_part) != std::string::npos) return true;
    }
    return false;
}

double DailyLimit(const prosophor::nr::domain::Member& member, const char* key,
                  double fallback) {
    const auto it = member.profile.dri_overrides.find(key);
    return (it != member.profile.dri_overrides.end() && it->second > 0.0)
               ? it->second
               : fallback;
}

std::string TabooReason(const prosophor::nr::domain::Member& member,
                        const std::string& food_name) {
    return MatchesTerms(food_name, member.profile.allergies) ? "过敏" : "忌口";
}

// 命中的过敏原（"白灼虾" 命中 "虾"）；没命中返回空
std::string MatchedAllergen(const prosophor::nr::domain::Member& member,
                            const std::string& food_name) {
    for (const auto& term : member.profile.allergies) {
        if (!term.empty() &&
            (food_name == term || food_name.find(term) != std::string::npos)) {
            return term;
        }
    }
    return {};
}

// 命中的慢病标签原文（"2型糖尿病" 而非 "糖尿病"）
std::string MatchedHealthTag(const prosophor::nr::domain::Member& member,
                             const char* part) {
    for (const auto& tag : member.profile.health_tags) {
        if (tag.find(part) != std::string::npos) return tag;
    }
    return {};
}

// 档案里的全部健康标签（"2型糖尿病、高血压"），忌口时说明"为什么"
std::string MatchedHealthTagLine(const prosophor::nr::domain::Member& member) {
    std::string tags;
    for (const auto& tag : member.profile.health_tags) {
        if (!tags.empty()) tags += "、";
        tags += tag;
    }
    return tags;
}

}  // namespace

void ResidentSession::RunPerceptionTurn(const std::string& frame_path) {
    if (engine_ == nullptr || frame_path.empty()) return;

    PersistLlmInputFrame(frame_path);

    if (perception_session_id_.empty()) {
        LOG_WARN("nutribot: 感知会话未创建，跳过本轮");
        return;
    }

    // 独立感知 session：无工具、保留文字上下文，图片由 SetClearImagesAfterTurn
    // 每轮从历史里剥掉 —— 模型看到的历史只有文字，视觉事实只来自当前帧。
    const std::string prompt =
        "当前消息只附带这一帧图片，历史轮次没有图片。判断当前画面里有没有饭菜："
        "端着的盘子、拿在手里的菜品、近距离的食物特写、菜单/菜品照片，都算有菜；"
        "只要明显能看到食物，就 has_food=true 并列出菜名，不要因为没有餐桌就判空。"
        "如果画面里确实没有任何食物（只有人、椅子、墙、床、柜子等），才 "
        "has_food=false、dishes=[]、scene=空桌待机，不要猜。历史里的 has_food 和"
        "场景结论一律忽略，必须只根据当前图片重新判断；即使历史全是空桌，当前"
        "图片有菜也必须 has_food=true。如果菜品来自照片/菜单，reply 直接按菜品"
        "描述，不要强调“图片/画”。只输出 JSON：{\"has_food\": true/false, "
        "\"dishes\": [\"...\"], \"scene\": \"空桌待机|备餐中|用餐中|餐后\", "
        "\"reply\": \"用一句话对用户说你看到了什么\"}。";

    LOG_INFO("[流程] LLM 入: 事件=周期感知(JSON) 图片=有 prompt={}字",
             prompt.size());

    last_perception_reply_.clear();
    const auto started = prosophor::SteadyClock::Now();
    engine_->SendUserMessage(perception_session_id_, prompt,
                             std::vector<std::string>{frame_path});

    std::string text = last_perception_reply_;
    LOG_INFO("[流程] LLM 出: {}ms 回复={}字（周期感知 JSON）",
             prosophor::SteadyClock::ElapsedMillis(started), text.size());
    LOG_INFO("[感知] 原始 JSON: {}", text.c_str());
    if (text.empty()) {
        LOG_WARN("nutribot: 周期感知无回复");
        return;
    }

    const size_t begin = text.find('{');
    const size_t end = text.rfind('}');
    if (begin == std::string::npos || end == std::string::npos || end <= begin) {
        LOG_WARN("nutribot: 周期感知未返回 JSON: {}", text.c_str());
        return;
    }
    auto parsed = nlohmann::json::parse(text.substr(begin, end - begin + 1),
                                        nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        LOG_WARN("nutribot: 周期感知 JSON 解析失败: {}", text.c_str());
        return;
    }

    pending_table_report_ = BuildTableReport(parsed);
}

ResidentSession::AgentTurnResult ResidentSession::RunAgentTurn(
    const std::string& utterance, const std::string& frame_path,
    const std::string& event, bool include_table_fact) {
    AgentTurnResult result;
    if (engine_ == nullptr) return result;

    TakeLastRequestedFace();        // 丢弃上一轮未消费的收尾表情
    TakeLastRequestedClawAngle();   // 丢弃上一轮未消费的收尾爪子动作
    // 家庭成员块保持在最前（角色 prompt 按"消息开头"约定读它）。
    // 无人脸识别：所有已建档成员都参与，新增成员下一轮立即生效。
    std::vector<prosophor::nr::domain::Member> diners;
    if (members_ != nullptr) diners = members_->list_members();
    std::string message = prosophor::nr::app::ComposeDinerPromptBlock(diners);
    message += ComposeTableFactBlock(include_table_fact);
    if (!last_proactive_reply_.empty()) {
        message += "<上次主动播报>" + last_proactive_reply_ + "</上次主动播报>\n";
    }

    if (!event.empty()) {
        message += "任务事件：" + event + "\n";
    } else {
        message += "任务事件：用户语音。用户说：" + utterance + "\n";
    }
    if (!utterance.empty()) {
        message += "直接回答用户；消息里有 <当前餐桌> 时以它为准，没有就以当前图片为准，"
                   "不要复述历史里的旧菜名；用户展示的菜品照片/菜单按真实菜品处理，"
                   "不要评论“这是图片/画”，直接按菜名分析营养和禁忌；"
                   "用户问营养/热量时，把 <当前餐桌> 里的营养数值说具体（千卡、蛋白质、"
                   "脂肪、钠、糖等），不要只做定性描述；没有数值就说不确定；"
                   "提醒忌口时必须说明原因（档案里的糖尿病/高血压/高血脂/过敏原等）"
                   "和关键营养数值，不能只说“忌口、少吃点”；涉及禁忌/风险时"
                   "可调用 robot_face(sad) 并 robot_claw(close)，菜品营养好或被夸奖时"
                   "可调用 robot_face(happy) 并 robot_claw(open)，普通不需要表情；"
                   "不要用文字标签表示表情；用户是在问“你在干什么/你能看到什么”这类"
                   "自身状态时不要调用 robot_*，先文字回答；表达对菜品的情绪不受此限；"
                   "动作工具只在明确命令动作（张开/合上爪子、移动、做表情）时调用；"
                   "任何工具调用都必须同时给用户一句文字回复，不能只调工具不说话；"
                   "不要输出工具调用文本。";
    } else if (!event.empty()) {
        message += "按事件要求用一到两句话回应，不要复述事件说明本身；"
                   "只播报；表达情绪可调用 robot_face 和 robot_claw，不要调用 robot_move；"
                   "点名提醒时要说明原因（档案依据）和关键营养数值，不能只说忌口；"
                   "调用工具时也必须同时说一句话，不能只调工具不说话。";
    }

    PersistLlmInputFrame(frame_path);

    const std::string flow_event =
        event.empty() ? (utterance.empty() ? "周期感知" : "用户语音") : event;
    LOG_INFO("[流程] LLM 入: 事件={} 语音={}字 图片={} prompt={}字",
             flow_event.c_str(), utterance.size(),
             frame_path.empty() ? "无" : "有", message.size());

    const auto started = prosophor::SteadyClock::Now();
    last_turn_reply_.clear();
    last_turn_tool_used_ = false;
    // 流式播报状态按轮清零：回调在 SendUserMessage 内同步驱动
    tts_chunker_.Reset();
    stream_spoken_chars_ = 0;
    stream_spoke_ = false;
    stream_reply_queued_ = false;
    stream_in_tool_block_ = false;
    stream_turn_active_ = true;
    if (frame_path.empty()) {
        engine_->SendUserMessage(session_id_, message);
    } else {
        engine_->SendUserMessage(session_id_, message,
                                 std::vector<std::string>{frame_path});
    }
    stream_turn_active_ = false;
    ++agent_turns_;
    result.ok = true;
    result.reply = last_turn_reply_;
    if (result.reply.empty() && last_turn_tool_used_) {
        // 开环 AgentChat：模型只调工具、没给同轮文字时补一句，避免完全静音。
        // 看菜类问题优先用当前餐桌事实兜底，别让"这是什么菜"只回一句"好的"。
        if (include_table_fact && latest_table_.valid && latest_table_.has_food &&
            !latest_table_.foods.empty()) {
            result.reply = "这是" + JoinFoodNames(latest_table_.foods) + "。";
            const auto& first_food = latest_table_.foods.front();
            if (first_food.has_nutrition) {
                result.reply += DescribeNutrition(first_food.nutrition) + "。";
            }
            if (!latest_table_.conflicts.empty()) {
                const auto& conflict = latest_table_.conflicts.front();
                result.reply += conflict.member;
                if (!conflict.context.empty()) {
                    result.reply += "（" + conflict.context + "）";
                }
                result.reply += "要注意" + conflict.reason + "，少吃点。";
            }
            LOG_INFO("[流程] 工具调用无同轮文字，用当前餐桌事实兜底");
        } else if (!event.empty() && !latest_table_.conflicts.empty()) {
            std::string reminder = "提醒一下：";
            for (size_t i = 0; i < latest_table_.conflicts.size(); ++i) {
                if (i > 0) reminder += "；";
                const auto& conflict = latest_table_.conflicts[i];
                reminder += conflict.member;
                if (!conflict.context.empty()) {
                    reminder += "（" + conflict.context + "）";
                }
                if (conflict.reason == "忌口" || conflict.reason == "过敏") {
                    reminder += "不能吃" + conflict.dish;
                } else {
                    reminder += "吃" + conflict.dish + "要注意" + conflict.reason;
                }
                for (const auto& food : latest_table_.foods) {
                    if (food.name == conflict.dish && food.has_nutrition) {
                        reminder += "，" + DescribeNutrition(food.nutrition);
                        break;
                    }
                }
            }
            result.reply = reminder + "。";
            LOG_INFO("[流程] 工具调用无同轮文字，用冲突事实兜底");
        } else {
            result.reply = "好的";
            LOG_INFO("[流程] 工具调用无同轮文字，使用兜底回复");
        }
    }
    LOG_INFO("[流程] LLM 出: {}ms 回复={}字（事件={} 语音={}字）",
             prosophor::SteadyClock::ElapsedMillis(started),
             result.reply.size(), flow_event.c_str(), utterance.size());
    return result;
}

void ResidentSession::ApplyPendingTableReport() {
    if (!pending_table_report_.valid) return;
    const TableReport report = std::move(pending_table_report_);
    pending_table_report_ = {};

    if (!report.scene.empty()) {
        ApplySceneLabel(report.scene, prosophor::SteadyClock::Now());
    }
    journal_.ObserveFoods(report.foods);
    UpdateTableFacts(report);

    if (report.has_food) {
        LOG_INFO("nutribot: agent 上报 -> {} 道：{}",
                 report.foods.size(), JoinFoodNames(report.foods).c_str());
    }

    LOG_INFO("[感知] 上报: 有菜={} 菜品={} 场景={} 冲突={}",
             report.has_food ? "是" : "否",
             JoinFoodNames(report.foods).c_str(),
             current_scene_label_.empty() ? "未变" : current_scene_label_.c_str(),
             latest_table_.conflicts.size());
}

void ResidentSession::UpdateTableFacts(const TableReport& report) {
    // 运行期新建档的成员下一拍就能参与提醒
    no_member_profiles_ =
        members_ == nullptr || members_->list_members().empty();
    TableFact fact;
    fact.valid = report.valid;
    fact.has_food = report.has_food;
    fact.scene = report.scene;
    fact.description = report.description;
    fact.foods = report.foods;
    fact.observed_at = prosophor::SteadyClock::Now();
    if (fact.valid && fact.has_food) {
        fact.conflicts = FindConflicts(fact.foods);
    }

    // 去重集合每次重建：菜离桌后旧键自动消失，重新上桌会再次提醒
    std::set<std::string> current;
    bool has_new = false;
    for (const auto& conflict : fact.conflicts) {
        const std::string key = conflict.Key();
        current.insert(key);
        if (notified_conflicts_.count(key) == 0) {
            has_new = true;
            LOG_INFO("[冲突] {}：{}（{}）", conflict.member.c_str(),
                     conflict.dish.c_str(), conflict.reason.c_str());
        }
    }
    notified_conflicts_ = std::move(current);

    if (fact.conflicts.empty()) {
        pending_conflict_at_.reset();
    } else if (has_new) {
        pending_conflict_at_ = prosophor::SteadyClock::Now();
        LOG_INFO("[冲突] 新冲突 {} 条，{}ms 后主动提醒（用户先开口则合并）",
                 fact.conflicts.size(), conflict_combine_ms);
    }

    latest_table_ = std::move(fact);
}

std::vector<ResidentSession::DishConflict> ResidentSession::FindConflicts(
    const std::vector<prosophor::nr::domain::FoodItem>& foods) const {
    std::vector<DishConflict> conflicts;
    if (members_ == nullptr || foods.empty()) return conflicts;

    for (const auto& member : members_->list_members()) {
        for (const auto& food : foods) {
            if (member.has_taboo(food.name)) {
                DishConflict conflict;
                conflict.member = member.name;
                conflict.dish = food.name;
                conflict.reason = TabooReason(member, food.name);
                // 过敏优先点名过敏原；普通忌口带上慢病标签作为"为什么"
                const std::string allergen = MatchedAllergen(member, food.name);
                conflict.context = !allergen.empty() ? "对" + allergen + "过敏"
                                                     : MatchedHealthTagLine(member);
                conflicts.push_back(std::move(conflict));
                continue;
            }
            if (!food.has_nutrition) continue;
            const auto& nutrition = food.nutrition;
            if (HasHealthTag(member, "糖尿病") &&
                nutrition.sugar_g >=
                    DailyLimit(member, "sugar_g", daily_sugar_g) *
                        conflict_daily_ratio) {
                conflicts.push_back({member.name, food.name, "高糖",
                                     MatchedHealthTag(member, "糖尿病")});
            } else if (HasHealthTag(member, "高血压") &&
                       nutrition.sodium_mg >=
                           DailyLimit(member, "sodium_mg", daily_sodium_mg) *
                               conflict_daily_ratio) {
                conflicts.push_back({member.name, food.name, "高钠",
                                     MatchedHealthTag(member, "高血压")});
            } else if (HasHealthTag(member, "高血脂") &&
                       nutrition.fat_g >=
                           DailyLimit(member, "fat_g", daily_fat_g) *
                               conflict_daily_ratio) {
                conflicts.push_back({member.name, food.name, "高脂",
                                     MatchedHealthTag(member, "高血脂")});
            }
        }
    }
    return conflicts;
}

bool ResidentSession::HasFreshTableFact() const {
    if (!latest_table_.valid) return false;
    const int64_t age_ms =
        prosophor::SteadyClock::ElapsedMillis(latest_table_.observed_at);
    return age_ms >= 0 && age_ms <= table_fact_max_age_ms;
}

std::string ResidentSession::ComposeTableFactBlock(
    bool include_without_conflicts) const {
    if (!latest_table_.valid) return {};
    // 普通问话只在有冲突时注入，保持上下文精简；视觉类问题可强制注入
    if (!include_without_conflicts && latest_table_.conflicts.empty()) return {};
    const int64_t age_ms =
        prosophor::SteadyClock::ElapsedMillis(latest_table_.observed_at);
    if (age_ms < 0 || age_ms > table_fact_max_age_ms) return {};

    std::string block = "<当前餐桌>系统感知（" +
                        std::to_string(age_ms / 1000) + "秒前）：";
    if (!latest_table_.description.empty()) {
        block += latest_table_.description;
    } else if (!latest_table_.foods.empty()) {
        block += JoinFoodNames(latest_table_.foods);
    } else {
        block += "未发现饭菜";
    }
    // 营养库数值随事实注入：角色要求"营养数值以 <当前餐桌> 为准"，
    // 不注入的话模型只能定性说"油大糖多"，说不出千卡/蛋白质。
    bool has_nutrition = false;
    for (const auto& food : latest_table_.foods) {
        if (!food.has_nutrition) continue;
        block += has_nutrition ? "；" : "；营养：";
        has_nutrition = true;
        block += food.name + " " + DescribeNutrition(food.nutrition);
    }
    if (!latest_table_.conflicts.empty()) {
        block += "；注意：";
        for (size_t i = 0; i < latest_table_.conflicts.size(); ++i) {
            if (i > 0) block += "；";
            const auto& conflict = latest_table_.conflicts[i];
            block += conflict.member;
            if (!conflict.context.empty()) block += "（" + conflict.context + "）";
            block += "-" + conflict.dish + "（" + conflict.reason + "）";
        }
    }
    block += "</当前餐桌>\n";
    return block;
}

std::string ResidentSession::BuildConflictEvent() const {
    std::string event = "系统感知到本桌风险：";
    for (size_t i = 0; i < latest_table_.conflicts.size(); ++i) {
        if (i > 0) event += "；";
        const auto& conflict = latest_table_.conflicts[i];
        event += conflict.member;
        if (!conflict.context.empty()) event += "（" + conflict.context + "）";
        event += " 的 " + conflict.dish + "（" + conflict.reason + "）";
    }
    event += "。请用一句话点名提醒，只播报；可调用 robot_face(sad) 和 "
             "robot_claw(close) 表达关切，不要调用 robot_move；风险不明确就不说。";
    return event;
}

void ResidentSession::TickPendingConflict(
    prosophor::SteadyClock::TimePoint now) {
    if (!pending_conflict_at_.has_value()) return;
    if (latest_table_.conflicts.empty()) {
        pending_conflict_at_.reset();
        return;
    }
    if (now < *pending_conflict_at_ + Milliseconds(conflict_combine_ms)) return;
    // 用户/播报占用时保留 pending，回待机再补
    if (!CanStartProactiveSpeech()) return;

    pending_conflict_at_.reset();
    const AgentTurnResult turn = RunAgentTurn("", "", BuildConflictEvent());
    if (!turn.ok) return;
    if (SpeakAgentReply(turn.reply, /*proactive=*/true, "禁忌提醒")) {
        LOG_INFO("[冲突] 已主动提醒");
    } else {
        LOG_INFO("[冲突] 模型未产出可播报提醒");
    }
}

void ResidentSession::TickPerception(prosophor::SteadyClock::TimePoint now) {
    // 未到节拍的 100ms 主循环调用不打印，避免每秒十几条刷屏；
    // 真正进入本拍/提前退出（语音占用、无新帧）都会打印。
    if (!camera_ok_ || now < next_perception_at_) return;

    const char* phase = phase_ == Phase::kStandby     ? "standby"
                        : phase_ == Phase::kListening ? "listening"
                                                      : "speaking";
    LOG_INFO("[感知] Tick 入: phase={} 语音={} 待ASR={} 待派发={} 播报中={}", phase,
             voice_interaction_.IsSpeechActive() ? "yes" : "no",
             voice_interaction_.HasPendingAsr() ? "yes" : "no",
             utterance_coalescer_.Empty() ? "no" : "yes",
             voice_interaction_.IsSpeaking() ? "yes" : "no");

    // 语音优先：agent 回合是秒级同步调用，和用户说话/播报撞上时会把唤醒应答堵在
    // 主循环外。这里只推迟本拍，不丢帧。
    // 并句窗口里的待派发话轮同样优先：否则感知回合会把窗口拖成秒级，
    // 用户说完话要等一拍感知结束才轮到对话。
    if (voice_interaction_.IsSpeechActive() || voice_interaction_.HasPendingAsr() ||
        !utterance_coalescer_.Empty() || voice_interaction_.IsSpeaking() ||
        phase_ == Phase::kSpeaking) {
        LOG_INFO("[感知] Tick 出: 语音占用，推迟 300ms");
        next_perception_at_ = now + Milliseconds(300);
        return;
    }
    const std::string frame_path = camera_.LatestFrame();
    if (frame_path.empty() || frame_path == last_frame_path_) {
        LOG_INFO("[感知] Tick 出: 无新帧（{}）",
                 frame_path.empty() ? "empty" : "same");
        return;
    }
    last_frame_path_ = frame_path;

    // 饭点窗口内 10s 一拍，非饭点 30s
    const bool meal_window = schedule_.Lookup(LocalMinutesOfDay()).has_value();
    const Milliseconds period =
        meal_window ? perception_period_ : idle_perception_period_;

    const auto started = prosophor::SteadyClock::Now();
    LOG_INFO("[感知] Tick 开始: frame={} 饭点={} 周期={}ms", frame_path.c_str(),
             meal_window ? "yes" : "no", period.count());
    RunPerceptionTurn(frame_path);
    ApplyPendingTableReport();
    next_perception_at_ = prosophor::SteadyClock::Now() + period;
    LOG_INFO("[感知] Tick 出: 耗时 {}ms，下一拍 {}ms",
             prosophor::SteadyClock::ElapsedMillis(started), period.count());
}

}  // namespace prosophor::nr::nutribot

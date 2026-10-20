// VlFoodDetector 实现 —— VL 模型看帧报菜名（语义对照 LlmSceneJudger）
#include "perception/vl_food_detector.h"

#include <cctype>
#include <filesystem>
#include <set>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

namespace prosophor::nr::app {

using nlohmann::json;

namespace {

// 菜品数上限：prompt 与解析共用同一约束（模型不守约时由解析兜底）
constexpr size_t max_food_items = 8;

// 输出约束：一句短指令，**不带示例**。
//
// 实测（qwen2p5-vl-3b，板上）,三条都是同一个病根 —— 小模型会把 prompt 里
// 出现的"具体字面"当成可抄的答案：
//   1. 给 JSON 示例 ["清蒸鲈鱼","白灼虾"] → 遇到不认识/非餐桌的图，直接回吐这串示例
//      （实测 4 张毫不相干的图 —— 芦笋、炖梨、西兰花、心形盘 —— 全部答成"清蒸鲈鱼、白灼虾"，
//       而同一张图换成开放式提问却能正确描述，证明图确实进了模型）
//   2. 写"看不清就输出 []" → 对着真菜照直接回 []
//   3. 给"左上/右上/…"位置词 → 回 ["中间","左下","右上",…]，把位置词当菜名
// 故：不给示例、不给空数组提示、不给位置词。"不要遗漏"经多菜样本实测有效（2→4 道），保留。
// 措辞取规范形（"餐桌上的菜品"而非"餐桌上的所有菜品"）：模型回吐的是 prompt 的
// 原字面，规范形才让下方回吐守卫的子串匹配抓得住（实测它抄的是去掉"所有"的版本）。
constexpr const char* kFoodDetectPrompt =
    "逐个列出图中餐桌上的菜品，不要遗漏。只输出一个 JSON 字符串数组，不要解释。";

// 闭集模式的指令前缀/后缀：候选菜名整体拼进 prompt（66 道 ≈ 300 token，2048 上下文
// 有余量）。同样不给 JSON 示例、不给空数组提示 —— 与开放模式同一批实测教训。
// "按可能性从高到低、最多 5 个"是评测口径（Top-5 命中）的直接来源。
//
// 为什么不给"没有菜就输出 []"的出口（2026-10-18 实测）：
//   非餐桌画面（洞洞板/机械臂/墙面）上 8B 会**稳定**编造候选菜名，连续多轮
//   一模一样；时间一致性门控（food_streak / settled）拦不住稳定幻觉（现场：
//   空画面报出 5 道菜、1030 千卡并触发两条假提醒）。试过在闭集 prompt 加空桌
//   出口：负样本确实回 []，但 12 张单菜图 Top-1 从 3/12 掉到 0/12 —— 空输出
//   被模型当成"不确定时的安全答案"，伤召回。
//   最终方案：识别 prompt 保持原样；装配层在稳定菜品集合出现时，用
//   FoodPresenceQuestion 单独做一次二分类校验（菜图"有"/非餐桌"没有"），
//   拒绝才清空 —— 两个问题互不污染，召回不变。
constexpr const char* kClosedPromptPrefix =
    "从下面的候选菜名里，挑出图中餐桌上真实存在的菜品，按可能性从高到低排序，"
    "最多 5 个。候选菜名：";
constexpr const char* kClosedPromptSuffix =
    "。只输出一个 JSON 字符串数组（数组元素必须是候选里的菜名），不要解释。";

// 存在性校验提问：刻意与识别 prompt 分离，不带候选列表、不预设"必须有菜"。
constexpr const char* kFoodPresenceQuestion =
    "这张图里有餐桌上的饭菜吗？只回答“有”或“没有”，不要解释。";

// UTF-8 码点数（闭集包含匹配的"两侧≥2 个字符"门槛；中文一字 3 字节）
size_t CodepointCount(const std::string& text) {
    size_t count = 0;
    for (const unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++count;
    }
    return count;
}

// 列表分隔符的字节长度（0 = 不是分隔符）：ASCII 的 '.' ')' ' ' ':' 各 1 字节；
// 全角顿号/右括号/冒号/句点各 3 字节（中文标点不能当字符常量比较，按字节序列比）
size_t SeparatorLength(const std::string& text, size_t pos) {
    const char c = text[pos];
    if (c == '.' || c == ')' || c == ' ' || c == ':') return 1;
    for (const char* sep : {"、", "）", "：", "．"}) {
        const size_t len = std::string_view(sep).size();
        if (text.compare(pos, len, sep) == 0) return len;
    }
    return 0;
}

// 去掉模型惯加的列表编号/项目符号（"1. 红烧肉" / "2、红烧肉" / "- 红烧肉"）
std::string StripListPrefix(const std::string& raw) {
    size_t begin = 0;
    while (begin < raw.size() && std::isdigit(static_cast<unsigned char>(raw[begin]))) {
        ++begin;
    }
    if (begin > 0) {
        for (size_t sep = SeparatorLength(raw, begin); begin < raw.size() && sep > 0;
             sep = SeparatorLength(raw, begin)) {
            begin += sep;
        }
        if (begin < raw.size()) return raw.substr(begin);
    }
    if (raw.size() >= 2 && (raw.compare(0, 2, "- ") == 0 || raw.compare(0, 2, "* ") == 0)) {
        return raw.substr(2);
    }
    return raw;
}

// 帧 -> 图片路径（帧视图为 ImageFrame；空帧视为无图）
std::string ImagePathOf(FrameRef frame) {
    if (frame == nullptr) return {};
    return static_cast<const ImageFrame*>(frame)->image_path;
}

// 帧标识：路径|mtime|size（对齐 provider 的视觉缓存键，同一张图不重复识别）；
// stat 失败（如路径不存在）退化为纯路径
std::string FrameKey(const std::string& image_path) {
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(image_path, ec);
    if (ec) return image_path;
    const auto size = std::filesystem::file_size(image_path, ec);
    if (ec) return image_path;
    return image_path + "|" +
           std::to_string(static_cast<long long>(mtime.time_since_epoch().count())) +
           "|" + std::to_string(size);
}

std::string Trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// 占位名：模型分不清时会回 "菜品1"、"菜2"、"食物3"、"dish 4" 这类占位标签
// （实测 8B 分帧漂移，这些占位名被当真菜名播报出来："桌上现在有：菜品1、
// 菜品2…"）。去掉尾部编号后若只剩泛称/类别词，视为无识别。
bool IsPlaceholderDishName(const std::string& name) {
    std::string core = name;
    while (!core.empty()) {
        const unsigned char c = core.back();
        if ((c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '_' || c == '#') {
            core.pop_back();
            continue;
        }
        bool trailing_cn_numeral = false;
        for (const char* numeral : {"零", "一", "二", "三", "四", "五",
                                    "六", "七", "八", "九", "十"}) {
            const std::string_view sv(numeral);
            if (core.size() >= sv.size() &&
                core.compare(core.size() - sv.size(), sv.size(), sv) == 0) {
                core.erase(core.size() - sv.size());
                trailing_cn_numeral = true;
                break;
            }
        }
        if (!trailing_cn_numeral) break;
    }
    static const std::set<std::string> placeholders = {
        "菜品", "菜", "菜肴", "食物", "食品", "菜名", "dish", "food",
        "item", "unknown", "未知", "无", "none", "n/a",
        "主菜", "配菜", "汤品", "主食", "甜点", "凉菜", "热菜"};
    return placeholders.count(core) > 0;
}

// 菜名合理性：模型复述指令或描述画面时给出的串，喂给禁忌规则会造误报。
// 中文菜名都短且无句读，按这两道闸门拦。
bool PlausibleDishName(const std::string& name) {
    if (IsPlaceholderDishName(name)) return false;
    if (name.size() > 30) return false;   // 30 字节 ≈ 10 个汉字
    for (const char* punct : {"，", "。", "；", "、", "！", "？", ",", ".", ";"}) {
        if (name.find(punct) != std::string::npos) return false;
    }
    // 回吐守卫：模型不认识时会把 prompt 里的字面抄回来当答案（实测抄过示例
    // "清蒸鲈鱼/白灼虾"、抄过指令 "餐桌上的菜品" / "菜"）。示例已从 prompt 去掉，
    // 故合法菜名不可能出现在 prompt 里 —— 凡是能在 prompt 中找到的字面一律丢弃。
    if (std::string_view(kFoodDetectPrompt).find(name) != std::string_view::npos) {
        return false;
    }
    return true;
}

// 取单个数组元素的菜名。模型输出形状会漂移，两种都收：
//   纯字符串          ["白灼虾"]
//   对象（带字段名）   {"food": "白灼虾"} —— 字段名实测出现过 name / food / dish
std::string ItemName(const json& item) {
    if (item.is_string()) return item.get<std::string>();
    if (!item.is_object()) return {};
    for (const char* key : {"name", "food", "dish"}) {
        const auto it = item.find(key);
        if (it != item.end() && it->is_string()) return it->get<std::string>();
    }
    for (const auto& entry : item.items()) {   // 未知字段名：退而取首个字符串值
        if (entry.value().is_string()) return entry.value().get<std::string>();
    }
    return {};
}

}  // namespace

std::string build_food_detect_prompt(const std::vector<std::string>& candidates) {
    if (candidates.empty()) return kFoodDetectPrompt;
    std::string prompt = kClosedPromptPrefix;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i > 0) prompt += "、";
        prompt += candidates[i];
    }
    prompt += kClosedPromptSuffix;
    return prompt;
}

const char* FoodPresenceQuestion() { return kFoodPresenceQuestion; }

std::optional<bool> ParseFoodPresenceReply(const std::string& reply) {
    const std::string text = Trim(reply);
    if (text.empty()) {
        return std::nullopt;
    }
    // 否定优先："没有" 里含 "有"，顺序反了会把拒绝判成通过
    if (text.find("没有") != std::string::npos || text.find("没") != std::string::npos ||
        text.find("无") != std::string::npos) {
        return false;
    }
    std::string lower;
    lower.reserve(text.size());
    for (const unsigned char c : text) {
        lower += static_cast<char>(std::tolower(c));
    }
    if (lower.find("no") != std::string::npos) {
        return false;
    }
    if (text.find("有") != std::string::npos ||
        lower.find("yes") != std::string::npos) {
        return true;
    }
    return std::nullopt;   // 答非所问：调用方 fail-open
}

std::optional<std::string> MatchCandidateName(
    const std::string& raw, const std::vector<std::string>& candidates) {
    if (raw.empty() || candidates.empty()) return std::nullopt;
    // 1) 精确
    for (const auto& candidate : candidates) {
        if (candidate == raw) return candidate;
    }
    // 2) 最长包含（两侧 ≥2 个字符，"汤/肉"这类单字不乱配）
    if (CodepointCount(raw) < 2) return std::nullopt;
    const std::string* best = nullptr;
    for (const auto& candidate : candidates) {
        if (CodepointCount(candidate) < 2) continue;
        if (raw.find(candidate) == std::string::npos &&
            candidate.find(raw) == std::string::npos) {
            continue;
        }
        if (best == nullptr || candidate.size() > best->size()) best = &candidate;
    }
    if (best == nullptr) return std::nullopt;
    return *best;
}

std::vector<prosophor::nr::domain::FoodItem> parse_food_detect_response(
    const std::string& text, const std::vector<std::string>& candidates) {
    std::vector<prosophor::nr::domain::FoodItem> foods;
    if (text.empty()) return foods;
    try {
        // 模型可能回带 ```json 围栏或夹带说明：取首个 [...] 整体
        // （切片取最外层数组，故 {"dishes": [...]} 这类包装也自然落到数组上）
        const size_t b = text.find('[');
        const size_t e = text.rfind(']');
        if (b == std::string::npos || e == std::string::npos || e < b) {
            return foods;
        }
        const json data = json::parse(text.substr(b, e - b + 1));
        if (!data.is_array()) return foods;

        std::set<std::string> seen;
        for (const auto& item : data) {
            const std::string raw = Trim(ItemName(item));
            if (raw.empty()) continue;

            std::string name = raw;
            if (candidates.empty()) {
                // 开放模式（逐字保持原行为）：合理性闸门 + prompt 回吐守卫
                if (!PlausibleDishName(name)) continue;
            } else {
                // 闭集模式：去掉编号前缀，只认候选内的名字，并归一为规范名
                //（回吐守卫由候选集天然承担：候选外一律丢弃）
                name = Trim(StripListPrefix(raw));
                const auto matched = MatchCandidateName(name, candidates);
                if (!matched.has_value()) continue;
                name = *matched;
            }

            if (!seen.insert(name).second) continue;
            // food_id 暂用菜名 —— 营养库接入前菜品没有独立编号
            foods.emplace_back(name, name);
            if (foods.size() >= max_food_items) break;
        }
    } catch (...) {
        foods.clear();   // 解析失败 = 没识别到，不把半截结果交出去
    }
    return foods;
}

VlFoodDetector::VlFoodDetector(VisionQueryBackend backend,
                               double min_interval_seconds,
                               std::vector<std::string> candidates)
    : backend_(std::move(backend)),
      min_interval_seconds_(min_interval_seconds),
      candidates_(std::move(candidates)) {}

bool VlFoodDetector::IntervalElapsed() const {
    if (!has_queried_) return true;
    return prosophor::SteadyClock::ElapsedSeconds(last_query_at_) >=
           min_interval_seconds_;
}

std::vector<prosophor::nr::domain::FoodItem> VlFoodDetector::detect_domain(
    FrameRef frame) {
    if (!backend_) return {};
    const std::string image_path = ImagePathOf(frame);
    if (image_path.empty()) return {};   // 本 tick 没有可看的图

    // 同一帧：复用上次结果，不调模型（帧没变，桌上的菜也不会变）
    const std::string key = FrameKey(image_path);
    if (!last_frame_key_.empty() && key == last_frame_key_) return last_result_;

    // 节流窗口内：沿用上次结果（VL 推理是秒级，不能按帧走）
    if (!IntervalElapsed()) return last_result_;

    std::optional<std::string> reply;
    try {
        reply = backend_(image_path, build_food_detect_prompt(candidates_));
    } catch (...) {
        reply = std::nullopt;   // 推理异常不阻塞主循环（对齐 LlmSceneJudger）
    }
    // 失败同样计时 —— 否则每 tick 重试会把 NPU 打死
    ++query_count_;
    last_query_at_ = prosophor::SteadyClock::Now();
    has_queried_ = true;
    if (!reply.has_value()) {
        // 沿用上次结果而非清空：清空会让"已上桌的禁忌菜"静默消失，比短暂沿用更危险。
        // 不更新 last_frame_key_，故过窗口后同一帧还会重试。
        return last_result_;
    }

    last_frame_key_ = key;
    last_result_ = parse_food_detect_response(*reply, candidates_);
    return last_result_;
}

}  // namespace prosophor::nr::app

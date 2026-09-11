// VlFoodDetector 实现 —— VL 模型看帧报菜名（语义对照 LlmSceneJudger）
#include "vl_food_detector.h"

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

// 菜名合理性：模型复述指令或描述画面时给出的串，喂给禁忌规则会造误报。
// 中文菜名都短且无句读，按这两道闸门拦。
bool PlausibleDishName(const std::string& name) {
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

std::string build_food_detect_prompt() { return kFoodDetectPrompt; }

std::vector<prosophor::nr::domain::FoodItem> parse_food_detect_response(
    const std::string& text) {
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
            const std::string name = Trim(ItemName(item));
            if (name.empty() || !PlausibleDishName(name)) continue;
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
                               double min_interval_seconds)
    : backend_(std::move(backend)),
      min_interval_seconds_(min_interval_seconds) {}

bool VlFoodDetector::IntervalElapsed() const {
    if (!has_queried_) return true;
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - last_query_at_).count();
    return elapsed >= min_interval_seconds_;
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
        reply = backend_(image_path, build_food_detect_prompt());
    } catch (...) {
        reply = std::nullopt;   // 推理异常不阻塞主循环（对齐 LlmSceneJudger）
    }
    // 失败同样计时 —— 否则每 tick 重试会把 NPU 打死
    ++query_count_;
    last_query_at_ = std::chrono::steady_clock::now();
    has_queried_ = true;
    if (!reply.has_value()) {
        // 沿用上次结果而非清空：清空会让"已上桌的禁忌菜"静默消失，比短暂沿用更危险。
        // 不更新 last_frame_key_，故过窗口后同一帧还会重试。
        return last_result_;
    }

    last_frame_key_ = key;
    last_result_ = parse_food_detect_response(*reply);
    return last_result_;
}

}  // namespace prosophor::nr::app

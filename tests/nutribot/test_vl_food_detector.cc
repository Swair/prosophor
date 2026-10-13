// VlFoodDetector 测试：prompt 构建 / 回复解析 / 帧缓存与节流 / Fake 后端全链路
#include <map>
#include <string>
#include <vector>

#include "meal_monitor.h"
#include "micro_test.hpp"
#include "ports.h"
#include "repositories.h"
#include "vl_food_detector.h"

using namespace prosophor::nr::app;
using prosophor::nr::domain::FoodItem;
using prosophor::nr::domain::Member;
using prosophor::nr::domain::MemberRepository;
using prosophor::nr::domain::ScenePackage;

namespace {

// ---- Fake：人脸识别端口（只喂身份，不看帧） ----
class FakeVision : public FaceRecognitionPort {
public:
    std::vector<FaceObservation> result;
    std::vector<FaceObservation> detect_and_recognize(
        FrameRef, MemberRepository&, double, int) override {
        return result;
    }
    void release() override {}
};

// ---- Fake：成员仓储（内存 map） ----
class FakeRepo : public MemberRepository {
public:
    std::map<std::string, Member> store;

    std::optional<prosophor::nr::domain::MemberMatch> find_match(
        const std::vector<float>&, double) override {
        return std::nullopt;
    }
    std::vector<Member> list_members() override {
        std::vector<Member> out;
        for (auto& [k, v] : store) out.push_back(v);
        return out;
    }
    std::optional<Member> get_member(const std::string& name) override {
        auto it = store.find(name);
        if (it == store.end()) return std::nullopt;
        return it->second;
    }
    void add_member(const Member& m) override { store[m.name] = m; }
    void delete_member(const std::string& name) override { store.erase(name); }
};

// ---- Fake：看图后端（按预设文本应答，记录每次调用） ----
class FakeVisionModel {
public:
    std::string reply;            // 预设回复
    bool available = true;        // false = 后端不可用（返回 nullopt）
    bool throwing = false;        // true = 后端抛异常
    int calls = 0;
    std::string last_question;

    std::optional<std::string> operator()(const std::string&,
                                          const std::string& question) {
        ++calls;
        last_question = question;
        if (throwing) throw std::runtime_error("NPU down");
        if (!available) return std::nullopt;
        return reply;
    }
};

VisionQueryBackend backend_of(FakeVisionModel& model) {
    return [&model](const std::string& image_path, const std::string& question) {
        return model(image_path, question);
    };
}

Member make_member(const std::string& name,
                   std::vector<std::string> taboos = {}) {
    Member m;
    m.member_id = "id-" + name;
    m.name = name;
    m.profile.taboo_foods = std::move(taboos);
    return m;
}

FaceObservation known_face(const std::string& name, double sim) {
    FaceObservation o;
    o.name = name;
    o.similarity = sim;
    return o;
}

// 不存在的路径：帧标识退化为纯路径，用例无需碰文件系统
ImageFrame frame_of(const std::string& path) { return ImageFrame{path}; }

std::string names_of(const std::vector<FoodItem>& foods) {
    std::string out;
    for (const auto& f : foods) {
        if (!out.empty()) out += ",";
        out += f.name;
    }
    return out;
}

}  // namespace

// ---- prompt 构建 ----

TEST(food_detector, prompt_contains_output_contract) {
    const std::string p = build_food_detect_prompt();
    EXPECT_TRUE(p.find("JSON") != std::string::npos);
    // 不给示例：模型会回吐示例当答案（实测 4 张不相干的图全答成示例里的菜名）
    EXPECT_TRUE(p.find("[\"") == std::string::npos);
}

TEST(food_detector, prompt_has_no_empty_escape_hatch) {
    // 回归守卫：prompt 里不得出现 "[]" 这类"看不清就输出空"的逃生条款 ——
    // 实测 3B 模型会把它当出口，对着真实菜照（一盘小龙虾）直接回 []
    EXPECT_TRUE(build_food_detect_prompt().find("[]") == std::string::npos);
}

// ---- 回复解析 ----

TEST(food_detector, parse_plain_array) {
    const auto foods = parse_food_detect_response(R"(["清蒸鲈鱼", "白灼虾"])");
    EXPECT_EQ(foods.size(), 2u);
    EXPECT_EQ(foods[0].name, "清蒸鲈鱼");
    EXPECT_EQ(foods[1].name, "白灼虾");
}

TEST(food_detector, parse_with_json_fence_and_prose) {
    const auto foods = parse_food_detect_response(
        "好的，我看到了：\n```json\n[\"红烧肉\"]\n```\n以上。");
    EXPECT_EQ(foods.size(), 1u);
    EXPECT_EQ(foods[0].name, "红烧肉");
}

TEST(food_detector, parse_object_items) {
    // 模型偶尔自带字段包装：字段名实测出现过 name / food / dish，逐个都收
    const auto named = parse_food_detect_response(
        R"([{"name": "宫保鸡丁"}, {"name": "米饭", "confidence": 0.8}])");
    EXPECT_EQ(named.size(), 2u);
    EXPECT_EQ(named[0].name, "宫保鸡丁");
    EXPECT_EQ(named[1].name, "米饭");

    const auto food_key = parse_food_detect_response(R"([{"food": "湖南口味龙虾"}])");
    EXPECT_EQ(food_key.size(), 1u);
    EXPECT_EQ(food_key[0].name, "湖南口味龙虾");

    const auto unknown_key = parse_food_detect_response(R"([{"菜名": "白灼虾"}])");
    EXPECT_EQ(unknown_key.size(), 1u);      // 未知字段名退而取首个字符串值
    EXPECT_EQ(unknown_key[0].name, "白灼虾");
}

TEST(food_detector, parse_dedup_and_trim) {
    const auto foods =
        parse_food_detect_response(R"([" 米饭 ", "米饭", "", "  ", "青菜"])");
    EXPECT_EQ(foods.size(), 2u);
    EXPECT_EQ(foods[0].name, "米饭");
    EXPECT_EQ(foods[1].name, "青菜");
}

TEST(food_detector, parse_caps_at_eight) {
    const auto foods = parse_food_detect_response(
        R"(["1","2","3","4","5","6","7","8","9","10","11","12"])");
    EXPECT_EQ(foods.size(), 8u);
}

TEST(food_detector, parse_drops_prompt_echoes) {
    // 模型不认识时会回吐 prompt 里的字面（实测抄过 JSON 示例、抄过"餐桌上的菜品"）。
    // 示例已从 prompt 移除，故合法菜名不可能出现在 prompt 里 —— 能对上的一律丢弃。
    const auto foods = parse_food_detect_response(
        R"(["餐桌上的菜品", "菜品", "JSON 字符串数组", "红烧肉"])");
    EXPECT_EQ(foods.size(), 1u);
    EXPECT_EQ(foods[0].name, "红烧肉");
}

TEST(food_detector, parse_drops_implausible_names) {
    // 实测：模型偶尔复述指令/描述画面（grounding 模式下 label 甚至会是"盘子"）。
    // 带句读或过长的串不是菜名，喂给禁忌规则会造误报。
    const auto foods = parse_food_detect_response(
        R"(["食物，包括配菜、主食和豆制品", "这盘菜看起来很好吃。", "炸鸡"])");
    EXPECT_EQ(foods.size(), 1u);
    EXPECT_EQ(foods[0].name, "炸鸡");
}

TEST(food_detector, parse_keeps_long_dish_names) {
    // 闸门不能误伤正常长菜名（30 字节 ≈ 10 个汉字）
    const auto foods = parse_food_detect_response(
        R"(["湖南口味的小龙虾", "西红柿炒鸡蛋"])");
    EXPECT_EQ(foods.size(), 2u);
}

TEST(food_detector, parse_invalid_returns_empty) {
    EXPECT_EQ(parse_food_detect_response("").size(), 0u);
    EXPECT_EQ(parse_food_detect_response("没有数组").size(), 0u);
    EXPECT_EQ(parse_food_detect_response(R"({"name": "米饭"})").size(), 0u);  // 非数组
    EXPECT_EQ(parse_food_detect_response("[\"米饭\"").size(), 0u);           // 半截 JSON
}

// ---- 检测器：后端 / 缓存 / 节流 ----

TEST(food_detector, detect_with_backend) {
    FakeVisionModel model;
    model.reply = R"(["白灼虾"])";
    VlFoodDetector detector(backend_of(model));
    const ImageFrame frame = frame_of("a.jpg");

    const auto foods = detector.detect_domain(&frame);
    EXPECT_EQ(foods.size(), 1u);
    EXPECT_EQ(foods[0].name, "白灼虾");
    EXPECT_EQ(model.calls, 1);
    EXPECT_EQ(detector.query_count(), 1);
    // 提问由检测器自己组装，后端不重复拼 prompt
    EXPECT_EQ(model.last_question, build_food_detect_prompt());
}

TEST(food_detector, detect_same_frame_cached) {
    FakeVisionModel model;
    model.reply = R"(["米饭"])";
    VlFoodDetector detector(backend_of(model));
    const ImageFrame frame = frame_of("a.jpg");

    detector.detect_domain(&frame);
    detector.detect_domain(&frame);
    detector.detect_domain(&frame);
    EXPECT_EQ(model.calls, 1);   // 帧没变，菜也不会变
}

TEST(food_detector, detect_new_frame_queries_again) {
    FakeVisionModel model;
    model.reply = R"(["米饭"])";
    VlFoodDetector detector(backend_of(model), /*min_interval_seconds=*/0.0);
    const ImageFrame first = frame_of("a.jpg");
    const ImageFrame second = frame_of("b.jpg");

    detector.detect_domain(&first);
    detector.detect_domain(&second);
    EXPECT_EQ(model.calls, 2);
}

TEST(food_detector, detect_throttled_keeps_last) {
    FakeVisionModel model;
    model.reply = R"(["牛奶"])";
    // 节流窗口极大：换帧也不重调，沿用上次结果
    VlFoodDetector detector(backend_of(model), /*min_interval_seconds=*/3600.0);
    const ImageFrame first = frame_of("a.jpg");
    const ImageFrame second = frame_of("b.jpg");

    EXPECT_EQ(names_of(detector.detect_domain(&first)), "牛奶");
    EXPECT_EQ(names_of(detector.detect_domain(&second)), "牛奶");
    EXPECT_EQ(model.calls, 1);
}

TEST(food_detector, detect_backend_failure_keeps_last) {
    FakeVisionModel model;
    model.reply = R"(["牛奶"])";
    VlFoodDetector detector(backend_of(model), /*min_interval_seconds=*/0.0);
    const ImageFrame first = frame_of("a.jpg");
    const ImageFrame second = frame_of("b.jpg");

    detector.detect_domain(&first);
    model.available = false;
    // 失败沿用上次（清空会让"已上桌的禁忌菜"静默消失），且失败也计入调用次数
    EXPECT_EQ(names_of(detector.detect_domain(&second)), "牛奶");
    EXPECT_EQ(model.calls, 2);
    EXPECT_EQ(detector.query_count(), 2);
}

TEST(food_detector, detect_backend_throw_swallowed) {
    FakeVisionModel model;
    model.throwing = true;
    VlFoodDetector detector(backend_of(model), /*min_interval_seconds=*/0.0);
    const ImageFrame frame = frame_of("a.jpg");

    EXPECT_EQ(detector.detect_domain(&frame).size(), 0u);   // 不抛给主循环
    EXPECT_EQ(detector.query_count(), 1);
}

TEST(food_detector, detect_null_backend_and_frame) {
    const VisionQueryBackend missing_backend;   // 空 backend：装配失败路径
    VlFoodDetector no_backend(missing_backend);
    const ImageFrame frame = frame_of("a.jpg");
    EXPECT_EQ(no_backend.detect_domain(&frame).size(), 0u);

    FakeVisionModel model;
    model.reply = R"(["米饭"])";
    VlFoodDetector detector(backend_of(model));
    EXPECT_EQ(detector.detect_domain(nullptr).size(), 0u);   // 空帧无图可看
    EXPECT_EQ(model.calls, 0);
}

// ---- 端到端：看菜 -> 规则引擎 -> 禁忌提醒 ----

TEST(food_detector, meal_monitor_pipeline_end_to_end) {
    FakeVisionModel model;
    model.reply = R"(["牛奶", "清蒸鲈鱼"])";
    VlFoodDetector detector(backend_of(model));

    FakeVision vision;
    vision.result = {known_face("小明", 0.9)};
    FakeRepo repo;
    repo.add_member(make_member("小明", {"牛奶"}));

    MealMonitor mm(&vision, &repo, nullptr, nullptr, &detector);
    const ImageFrame frame = frame_of("a.jpg");
    const ScenePackage pkg = mm.Tick(&frame);

    EXPECT_EQ(pkg.foods.size(), 2u);
    EXPECT_EQ(pkg.events.size(), 1u);            // 只有牛奶命中禁忌
    EXPECT_EQ(pkg.events[0].member_name, "小明");
    EXPECT_EQ(pkg.events[0].food, "牛奶");
    EXPECT_EQ(model.calls, 1);                   // 管道内同样只调一次模型
}

// ---- 闭集模式（候选进 prompt，解析只认候选并归一为规范名）----

namespace {

const std::vector<std::string> kCandidates = {"红烧肉", "鲜虾豆腐煲", "清炒时蔬",
                                              "麻婆豆腐"};

}  // namespace

TEST(vl_detector_closed, prompt_lists_candidates_and_asks_ranking) {
    const std::string prompt = build_food_detect_prompt(kCandidates);
    for (const auto& name : kCandidates) {
        EXPECT_TRUE(prompt.find(name) != std::string::npos);
    }
    EXPECT_TRUE(prompt.find("最多 5 个") != std::string::npos);
    // 不给 JSON 示例、不给空数组提示（开放模式的实测教训同样适用）
    EXPECT_TRUE(prompt.find("[]") == std::string::npos);
    EXPECT_TRUE(prompt.find("\"") == std::string::npos);   // prompt 里没有任何引号字面
    // 空候选 = 开放模式（回归）
    EXPECT_EQ(build_food_detect_prompt({}), build_food_detect_prompt());
}

TEST(vl_detector_closed, parse_accepts_candidates_and_canonicalizes) {
    // 精确命中、包含命中（模型加后缀）、带编号前缀
    const auto foods = parse_food_detect_response(
        R"(["红烧肉", "鲜虾豆腐煲盖饭", "2. 麻婆豆腐"])", kCandidates);
    EXPECT_EQ(foods.size(), 3u);
    EXPECT_EQ(foods[0].name, std::string("红烧肉"));
    EXPECT_EQ(foods[1].name, std::string("鲜虾豆腐煲"));   // 归一为规范名
    EXPECT_EQ(foods[2].name, std::string("麻婆豆腐"));
}

TEST(vl_detector_closed, parse_rejects_out_of_list_names) {
    // 候选外一律丢弃（含旧回吐陷阱："汤"单字、"餐桌上的菜品"提示词字面）
    const auto foods = parse_food_detect_response(
        R"(["汤", "餐桌上的菜品", "宫保鸡丁", "清炒时蔬"])", kCandidates);
    EXPECT_EQ(foods.size(), 1u);
    EXPECT_EQ(foods[0].name, std::string("清炒时蔬"));
}

TEST(vl_detector_closed, parse_dedupes_after_canonicalization) {
    // "红烧肉" 与 "红烧肉盖饭" 归一后是同一道：去重只留一条
    const auto foods = parse_food_detect_response(
        R"(["红烧肉", "红烧肉盖饭"])", kCandidates);
    EXPECT_EQ(foods.size(), 1u);
}

TEST(vl_detector_closed, match_candidate_name_rules) {
    EXPECT_EQ(MatchCandidateName("红烧肉", kCandidates).value(), std::string("红烧肉"));
    EXPECT_EQ(MatchCandidateName(" 鲜虾豆腐煲 ", kCandidates).value(),
              std::string("鲜虾豆腐煲"));
    EXPECT_EQ(MatchCandidateName("清炒时蔬拼盘", kCandidates).value(),
              std::string("清炒时蔬"));
    EXPECT_FALSE(MatchCandidateName("汤", kCandidates).has_value());      // 单字不配
    EXPECT_FALSE(MatchCandidateName("宫保鸡丁", kCandidates).has_value());
    EXPECT_FALSE(MatchCandidateName("", kCandidates).has_value());
    EXPECT_FALSE(MatchCandidateName("红烧肉", {}).has_value());
}

TEST(vl_detector_closed, detector_uses_candidates_end_to_end) {
    // Fake 后端回一段"带干扰"的答案：候选内外混合 + 编号
    auto backend = [](const std::string&, const std::string& question)
        -> std::optional<std::string> {
        EXPECT_TRUE(question.find("鲜虾豆腐煲") != std::string::npos);   // 候选进了提问
        return R"(["1、麻婆豆腐", "牛排", "红烧肉盖饭"])";
    };
    prosophor::nr::app::VlFoodDetector detector(backend, 0.0, kCandidates);
    const prosophor::nr::app::ImageFrame frame{"tests/nutribot/assets/vl_demo.jpg"};
    const auto foods = detector.detect_domain(&frame);
    EXPECT_EQ(foods.size(), 2u);
    EXPECT_EQ(foods[0].name, std::string("麻婆豆腐"));
    EXPECT_EQ(foods[1].name, std::string("红烧肉"));   // "红烧肉盖饭" 归一
}

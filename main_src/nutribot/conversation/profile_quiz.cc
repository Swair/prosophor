// ProfileQuiz 实现 —— 语义逐行对照 Python application/profile_quiz.py
#include "conversation/profile_quiz.h"

#include <cctype>
#include <unordered_set>

namespace prosophor::nr::app {

namespace {

// 否定/无可回答的规范化词表（与 Python NEGATIVE 一致）
const std::unordered_set<std::string>& kNegativeWords() {
    static const std::unordered_set<std::string> words = {
        "没有", "无", "没", "不", "不挑食", "都吃", "啥都吃", "不忌口",
        "不过敏", "没有忌口", "无禁忌", "健康", "正常", "随便",
    };
    return words;
}

// 提取字符串中第一个数字序列（与 re.search(r"\d+") 等价）；无则 nullopt
std::optional<int> first_digits(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            int v = 0;
            while (i < s.size() &&
                   std::isdigit(static_cast<unsigned char>(s[i]))) {
                v = v * 10 + (s[i] - '0');
                if (v > 1'000'000) break;   // 防御性上限
                ++i;
            }
            return v;
        }
    }
    return std::nullopt;
}

bool is_blank(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

}  // namespace

std::vector<std::string> parse_csv_answer(const std::string& answer) {
    // 仅去首尾空白（Python str.strip()；中间空白是分隔符，保留）
    size_t b = 0, e = answer.size();
    while (b < e && std::isspace(static_cast<unsigned char>(answer[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(answer[e - 1]))) --e;
    const std::string a = answer.substr(b, e - b);
    if (a.empty()) return {};
    if (kNegativeWords().count(a)) return {};   // 整句否定

    // CSV_SPLIT = re.compile(r"[,，、;；\s]+")（'，、；' 为 UTF-8 多字节，需序列匹配）
    std::vector<std::string> out;
    std::string cur;
    const auto flush = [&out, &cur]() {
        if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    };
    const size_t n = a.size();
    for (size_t i = 0; i < n;) {
        const bool sep_mb = (a.compare(i, 3, "，") == 0) ||
                            (a.compare(i, 3, "、") == 0) ||
                            (a.compare(i, 3, "；") == 0);
        if (sep_mb) {
            flush();
            i += 3;
        } else if (a[i] == ',' || a[i] == ';' ||
                   std::isspace(static_cast<unsigned char>(a[i]))) {
            flush();
            ++i;
        } else {
            cur.push_back(a[i]);
            ++i;
        }
    }
    flush();
    return out;
}

const std::vector<QuizQuestion>& ProfileQuiz::questions() {
    static const std::vector<QuizQuestion> kQuestions = {
        {"name", "你好，我是营养机器人！请问你叫什么名字？", "text", ""},
        {"age", "今年几岁啦？", "int", "如：6"},
        {"gender", "可以告诉我你的性别吗？男或女", "text", "男/女"},
        {"taboo_foods", "有什么忌口或不能吃的食物吗？", "csv", "如：牛奶、辣椒"},
        {"allergies", "对什么食物过敏吗？", "csv", "如：花生"},
        {"eating_habits", "平时饮食习惯怎样？比如少盐、素食、不吃早餐", "csv", ""},
        {"preferences", "口味偏好呢？比如清淡、爱吃甜食", "csv", ""},
        {"health_tags", "有没有需要留意的健康情况？比如儿童、糖尿病", "csv", ""},
    };
    return kQuestions;
}

ProfileQuiz::ProfileQuiz(DialoguePort& dialogue,
                         std::optional<nr::domain::Member> existing)
    : dialogue_(&dialogue), existing_(std::move(existing)) {}

std::optional<std::string> ProfileQuiz::Ask(const QuizQuestion& q) {
    std::string text = q.text;
    if (existing_.has_value()) {
        // 未答/保留提示：已有值则提示（与 Python 一致）
        const bool has_cur = [&]() {
            if (q.key == "name") return !existing_->name.empty();
            const auto& p = existing_->profile;
            if (q.key == "age") return p.age.has_value();
            if (q.key == "gender") return !p.gender.empty();
            if (q.key == "taboo_foods") return !p.taboo_foods.empty();
            if (q.key == "allergies") return !p.allergies.empty();
            if (q.key == "eating_habits") return !p.eating_habits.empty();
            if (q.key == "preferences") return !p.preferences.empty();
            if (q.key == "health_tags") return !p.health_tags.empty();
            return false;
        }();
        if (has_cur) text += "（当前已建档，回车保留）";
    }
    return dialogue_->ask(text);
}

std::optional<nr::domain::Member> ProfileQuiz::Run() {
    // 结果容器以 existing 为初值；有效回答覆盖，未答/否定保留原值
    nr::domain::Member result = existing_.value_or(nr::domain::Member{});
    auto& prof = result.profile;

    for (const auto& q : questions()) {
        const auto ans = Ask(q);
        if (!ans.has_value() || is_blank(*ans)) {
            continue;   // 未答：新成员跳过 / 老成员保留
        }
        if (q.kind == "int") {
            if (const auto v = first_digits(*ans)) {
                // Python: 0 < v < 130 才写；越界按"无有效值"（覆盖为空）
                prof.age = (*v > 0 && *v < 130) ? std::optional<int>(*v)
                                                : std::nullopt;
            }
            // 无数字：键不写入 -> 保留 existing
        } else if (q.kind == "csv") {
            auto tags = parse_csv_answer(*ans);
            // 空(否定) -> 保 existing（Python: [] or existing 回退）
            if (tags.empty()) continue;
            if (q.key == "taboo_foods") prof.taboo_foods = std::move(tags);
            else if (q.key == "allergies") prof.allergies = std::move(tags);
            else if (q.key == "eating_habits") prof.eating_habits = std::move(tags);
            else if (q.key == "preferences") prof.preferences = std::move(tags);
            else if (q.key == "health_tags") prof.health_tags = std::move(tags);
        } else {   // text
            if (q.key == "name") result.name = *ans;
            else if (q.key == "gender") prof.gender = *ans;
        }
    }

    if (result.name.empty()) return std::nullopt;
    return result;
}

}  // namespace prosophor::nr::app

// MealSchedule 实现 —— 饭点窗口解析与区间查询（纯逻辑）
#include "meal_schedule.h"

#include <cctype>

namespace prosophor::nr::app {

namespace {

// 两侧空白剔除后的子串视图（" 07:30 " -> "07:30"）
std::string_view TrimView(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

// 纯数字串（空串不算）
bool AllDigits(std::string_view text) {
    if (text.empty()) return false;
    for (const char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

}  // namespace

std::optional<int> ParseClockMinutes(std::string_view hhmm) {
    const std::string_view text = TrimView(hhmm);
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const std::string_view hours = text.substr(0, colon);
    const std::string_view minutes = text.substr(colon + 1);
    // 分钟段再出现冒号（"1:2:3"）视为非法，不做分段猜测
    if (minutes.find(':') != std::string_view::npos) return std::nullopt;
    if (!AllDigits(hours) || !AllDigits(minutes)) return std::nullopt;
    if (hours.size() > 2 || minutes.size() > 2) return std::nullopt;

    const int hour = std::stoi(std::string(hours));
    const int minute = std::stoi(std::string(minutes));
    if (hour > 23 || minute > 59) return std::nullopt;
    return hour * 60 + minute;
}

bool MealSchedule::AddWindow(const std::string& name, const std::string& start,
                             const std::string& end) {
    if (name.empty()) return false;
    const auto start_minutes = ParseClockMinutes(start);
    const auto end_minutes = ParseClockMinutes(end);
    if (!start_minutes.has_value() || !end_minutes.has_value()) return false;
    if (*start_minutes >= *end_minutes) return false;   // 零长/倒置窗口：配置错，拒绝

    windows_.push_back({name, *start_minutes, *end_minutes});
    return true;
}

std::optional<MealWindow> MealSchedule::Lookup(int minutes_of_day) const {
    for (const auto& window : windows_) {
        if (window.contains(minutes_of_day)) return window;
    }
    return std::nullopt;
}

}  // namespace prosophor::nr::app

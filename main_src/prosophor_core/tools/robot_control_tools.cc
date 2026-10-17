// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
//
// 机器人控制工具的注册实现。通信走 HttpClient（curl 连接池），
// 参数校验在工具边界做全 —— 非法输入只返回错误文本给模型，不触达硬件。
#include "tools/robot_control_tools.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>

#include <nlohmann/json.hpp>

#include "common/log_wrapper.h"
#include "config/config.h"
#include "managers/permission_manager.h"
#include "network/curl_client.h"
#include "tools/tool_registry.h"

namespace prosophor {

namespace {

// ── HTTP 基元（带 socket 超时；其余走 HttpClient 的默认路径）──

struct HttpReply {
    long status = 0;      // HTTP 状态码；0 = 传输层失败
    std::string body;     // 响应体（status != 0 时有效）；CURLOPT_NOBODY 时为 ""
    std::string error;    // status == 0 时的传输层错误
};

// 响应体落 std::string。不设回调时 libcurl 的默认写函数把 WRITEDATA 当 FILE*
// 用 fwrite —— 传 std::string 会踩内存（板上实测：POST 发出、回包到达即崩，
// 机器人停在"前进"等不到 stop）。
static size_t WriteBodyToString(char* data, size_t size, size_t count,
                                void* user_data) {
    auto* body = static_cast<std::string*>(user_data);
    const size_t bytes = size * count;
    if (body != nullptr && bytes > 0) body->append(data, bytes);
    return bytes;
}

// 裸 curl 而不是 HttpClient：控制端点需要 CURLOPT_TIMEOUT_MS（毫秒级收紧，
// 断连时不能按秒干等），HttpClient 的 HttpRequest 只暴露秒级字段
HttpReply RobotHttp(const std::string& url, bool use_post, long timeout_ms,
                    const std::string& body, const char* content_type) {
    HttpReply reply;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        reply.error = "curl 初始化失败";
        return reply;
    }

    struct curl_slist* headers = nullptr;
    if (use_post && content_type != nullptr) {
        std::string header = std::string("Content-Type: ") + content_type;
        headers = curl_slist_append(headers, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBodyToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply.body);
    if (use_post) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(body.size()));
        if (headers != nullptr) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        reply.error = curl_easy_strerror(code);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &reply.status);
    }

    if (headers != nullptr) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return reply;
}

// 工具返回：成功时给模型看接口回执（JSON），失败给一句带原因的人话
std::string ReportReply(const HttpReply& reply, const std::string& what) {
    if (reply.status == 0) {
        return "控制接口不可达（" + reply.error + "），" + what + "未生效。";
    }
    if (reply.status >= 200 && reply.status < 300) {
        return reply.body.empty() ? "ok" : reply.body;
    }
    return "控制接口返回 HTTP " + std::to_string(reply.status) + "：" +
           reply.body + "（" + what + "未生效）";
}

// 参数校验共用：取浮点/整数并夹到范围；非法返回 nullopt
std::optional<int> ReadIntInRange(const nlohmann::json& params,
                                  const char* key, int low, int high) {
    if (!params.contains(key) || !params[key].is_number()) return std::nullopt;
    const int value = params[key].get<int>();
    if (value < low || value > high) return std::nullopt;
    return value;
}

// ── 三个工具的处理器 ──

class RobotControl {
public:
    explicit RobotControl(const RobotControlConfig& cfg) : cfg_(cfg) {}

    // 运动：方向 + 速度；固定脉冲 —— 内部计时，到点自动 stop
    std::string Move(const nlohmann::json& params) {
        static const std::vector<std::string> directions = {"forward", "backward",
                                                            "left", "right", "stop"};
        if (!params.contains("direction") || !params["direction"].is_string()) {
            return "缺少 direction 参数（枚举：forward/backward/left/right/stop）";
        }
        const std::string direction = params["direction"].get<std::string>();
        if (std::find(directions.begin(), directions.end(), direction) ==
            directions.end()) {
            return "direction 非法：" + direction +
                   "（枚举：forward/backward/left/right/stop）";
        }

        const int speed = params.value("speed", cfg_.default_speed);
        if (speed < 0 || speed > 100) return "speed 必须在 0~100";

        nlohmann::json body{{"cmd", direction}, {"speed", speed}};
        const HttpReply reply = RobotHttp(cfg_.base_url + "/api/control", true,
                                          cfg_.command_timeout_ms, body.dump(),
                                          "application/json");
        if (reply.status == 0) {
            return "控制接口不可达（" + reply.error + "），指令未生效。";
        }
        if (reply.status < 200 || reply.status >= 300) {
            return ReportReply(reply, "指令");
        }

        // stop 是显式动作，没有后续脉冲要走
        if (direction == "stop") return reply.body;

        // 固定脉冲：等到点再发 stop。duration_ms 来自配置（默认 1000ms，
        // 上限 3000ms），模型改不了这个数
        std::this_thread::sleep_for(
            std::chrono::milliseconds(cfg_.move_pulse_ms));
        nlohmann::json stop_body{{"cmd", "stop"}, {"speed", cfg_.default_speed}};
        const HttpReply stop_reply =
            RobotHttp(cfg_.base_url + "/api/control", true, cfg_.command_timeout_ms,
                      stop_body.dump(), "application/json");
        std::string out = "已按方向 " + direction + " 走了 " +
                          std::to_string(cfg_.move_pulse_ms) + "ms 并停车。";
        if (stop_reply.status < 200 || stop_reply.status >= 300) {
            out += " ⚠️ 自动停车未确认，请立刻再发一次 stop！";
        }
        return out;
    }

    // 舵机：1=左手 2=肩1 3=肩2 4=手腕1 5=手腕2 6=爪子
    std::string Servo(const nlohmann::json& params) {
        const auto servo_id = ReadIntInRange(params, "servo_id", 1, 6);
        if (!servo_id.has_value()) return "servo_id 必须在 1~6";
        const auto angle = ReadIntInRange(params, "angle", 0, 180);
        if (!angle.has_value()) return "angle 必须在 0~180";

        nlohmann::json body{{"servo", *servo_id}, {"angle", *angle}};
        const HttpReply reply = RobotHttp(cfg_.base_url + "/api/control", true,
                                          cfg_.command_timeout_ms, body.dump(),
                                          "application/json");
        return ReportReply(reply, "舵机指令");
    }

    // 表情：OLED 情绪面（保持 5 秒后自动回落），零机械风险
    std::string Face(const nlohmann::json& params) {
        static const std::vector<std::string> faces = {
            "neutral", "happy", "sad", "angry", "surprised",
            "sleepy",  "wink",  "love", "cry",   "cool", "auto"};
        if (!params.contains("face") || !params["face"].is_string()) {
            return "缺少 face 参数";
        }
        const std::string face = params["face"].get<std::string>();
        if (std::find(faces.begin(), faces.end(), face) == faces.end()) {
            return "face 非法：" + face;
        }
        nlohmann::json body{{"face", face}};
        const HttpReply reply = RobotHttp(cfg_.base_url + "/api/control", true,
                                          cfg_.command_timeout_ms, body.dump(),
                                          "application/json");
        return ReportReply(reply, "表情指令");
    }

    std::string Status() {
        const HttpReply reply =
            RobotHttp(cfg_.base_url + "/api/status", false, cfg_.command_timeout_ms,
                      "", nullptr);
        return ReportReply(reply, "状态查询");
    }

private:
    RobotControlConfig cfg_;
};

}  // namespace

void RegisterRobotControlTools(ToolRegistry& registry) {
    const RobotControlConfig& cfg = ProsophorConfig::GetInstance().robot;
    if (!cfg.enabled) {
        LOG_INFO("robot: 控制工具未启用（robot.control_enabled=false）");
        return;
    }

    // 注册即授权：机器人工具是设备本体的动作接口，常驻回路无人值守 —— 权限门
    // 默认落到 Ask，而 Ask 在无确认回调的进程里等于 DENY（逐次确认 = 永久禁用）。
    // 白名单本身就是边界：只有这 4 个工具，参数校验全在工具边界，无自由文本入口。
    {
        PermissionRule rule;
        rule.tool_name = "robot_*";
        rule.default_level = PermissionLevel::Allow;
        PermissionManager::GetInstance().AddAllowRule(rule);
    }

    auto control = std::make_shared<RobotControl>(cfg);

    {
        nlohmann::json params = nlohmann::json::object();
        params["direction"] = {{"type", "string"},
                               {"enum", {"forward", "backward", "left", "right", "stop"}},
                               {"description", "forward 前进 / backward 后退 / left·right 原地转 / stop 停车（显式发，没有松手即停）"}};
        params["speed"] = {{"type", "integer"},
                           {"description", "速度 0~100，缺省 50"}};
        registry.RegisterTool(
            "robot_move",
            "控制机器人移动。它是固定短脉冲：按方向走约 1 秒后自动停车（防止跑飞），"
            "要移动更长距离须多次调用；先调 robot_status 确认 actual≈0（没在动）再发指令。",
            params, [control](const nlohmann::json& p) { return control->Move(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["servo_id"] = {{"type", "integer"},
                              {"description",
                               "1=左手(0~180) 2=肩1-底座回转(60~160) 3=肩2-近肩垂直(0~130) "
                               "4=手腕1(0~180) 5=手腕2-近腕垂直(70~160) 6=爪子(25=闭合~70=全张)。"
                               "超出机械行程的角度会被固件静默夹到边界"}};
        params["angle"] = {{"type", "integer"}, {"description", "角度 0~180"}};
        registry.RegisterTool(
            "robot_servo",
            "控制机器人舵机（手臂/手腕/爪子）转到指定角度。无回读，位置只能靠再发指令改变。",
            params, [control](const nlohmann::json& p) { return control->Servo(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        registry.RegisterTool(
            "robot_status",
            "读取机器人遥测：左右腿的目标/实际转速、PWM 占空比、编码器脉冲计数。"
            "不含舵机角度（无回读）。发运动指令前先用它确认当前状态。",
            params, [control](const nlohmann::json&) { return control->Status(); });
    }

    // 语义级动作工具：把"爪子开合"从机械参数（6 号舵机、25/70 度）里解耦。
    // 端侧模型不需要知道硬件细节 —— 指令越短，模型越不容易在参数上出错
    //（板上实测：让模型自己把"张开"映射成 servo_id=6/angle=70 时经常选错或空输出）。
    {
        nlohmann::json params = nlohmann::json::object();
        params["action"] = {{"type", "string"},
                            {"enum", {"open", "close"}},
                            {"description", "open 张开 / close 合上"}};
        registry.RegisterTool(
            "robot_claw",
            "开合机械爪：张开 action=open，合上 action=close。",
            params, [control](const nlohmann::json& p) {
                if (!p.contains("action") || !p["action"].is_string()) {
                    return std::string("缺少 action 参数（open/close）");
                }
                const std::string action = p["action"].get<std::string>();
                if (action != "open" && action != "close") {
                    return "action 非法：" + action + "（枚举：open/close）";
                }
                // 复用 robot_servo 的校验与 HTTP 路径：action → 6 号舵机角度
                return control->Servo(
                    {{"servo_id", 6}, {"angle", action == "open" ? 70 : 25}});
            });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["face"] = {{"type", "string"},
                          {"enum", {"neutral", "happy", "sad", "angry", "surprised",
                                    "sleepy", "wink", "love", "cry", "cool", "auto"}},
                          {"description", "neutral平静/happy开心/sad难过/angry生气/surprised惊讶/sleepy困/wink调皮/love喜欢/cry哭/cool酷，auto=恢复默认"}};
        registry.RegisterTool(
            "robot_face",
            "让机器人 OLED 显示一个情绪表情（保持 5 秒后自动回落默认表情）。"
            "表达情绪、回应夸奖或配合对话时使用。",
            params, [control](const nlohmann::json& p) { return control->Face(p); });
    }

    LOG_INFO("robot: 控制工具已注册（{}，脉冲 {}ms）", cfg.base_url.c_str(),
             cfg.move_pulse_ms);
}

}  // namespace prosophor

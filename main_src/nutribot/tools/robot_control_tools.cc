// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
//
// 机器人控制工具的注册实现。HTTP 走 base/network/HttpClient（毫秒级超时），
// 参数校验在工具边界做全 —— 非法输入只返回错误文本给模型，不触达硬件。
#include "tools/robot_control_tools.h"

#include <algorithm>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "base/network/curl_client.h"
#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "prosophor_core/public/agent_engine.h"
#include "prosophor_core/public/config.h"

namespace prosophor::nr::nutribot {

namespace {

// ── HTTP 基元（带 socket 超时；其余走 HttpClient 的默认路径）──

struct HttpReply {
    long status = 0;      // HTTP 状态码；0 = 传输层失败
    std::string body;     // 响应体（status != 0 时有效）；CURLOPT_NOBODY 时为 ""
    std::string error;    // status == 0 时的传输层错误
};

// 控制端点要毫秒级超时：直接用 base 的 HttpClient，请求级设置
// timeout_ms / connect_timeout_ms（默认值不受影响）。
HttpReply RobotHttp(const std::string& url, bool use_post, long timeout_ms,
                    const std::string& body, const char* content_type) {
    HttpReply reply;
    HttpRequest request;
    request.url = url;
    request.body = body;
    request.timeout_ms = timeout_ms;
    request.connect_timeout_ms = 2000;

    HeaderList headers;
    if (use_post && content_type != nullptr) {
        headers.append((std::string("Content-Type: ") + content_type).c_str());
        request.headers = headers.get();
    }

    HttpResponse response = use_post ? HttpClient::Instance().Post(request)
                                     : HttpClient::Instance().Get(request);
    reply.body = std::move(response.body);
    if (response.curl_code != CURLE_OK) {
        reply.error = response.error_msg.empty() ? "HTTP 请求失败"
                                                 : response.error_msg;
        LOG_WARN("[底盘] {} {} -> 传输失败: {}", use_post ? "POST" : "GET",
                 body.c_str(), reply.error.c_str());
        return reply;
    }
    reply.status = response.status_code;
    LOG_INFO("[底盘] {} {} -> HTTP {}", use_post ? "POST" : "GET",
             body.c_str(), reply.status);
    return reply;
}

// 表情是装饰性指令：播报口型每几秒刷新一次，底盘离线时不能拖住主循环
constexpr long expression_timeout_ms = 800;

// robot_face / robot_claw 工具最近一次成功下发的动作。内部 talking/auto/微动不记录。
std::string g_last_requested_face;
int g_last_requested_claw_angle = 0;

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
        std::this_thread::sleep_for(Milliseconds(cfg_.move_pulse_ms));
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
        LOG_INFO("robot: servo={} angle={} -> HTTP {}", *servo_id, *angle,
                 reply.status);
        return ReportReply(reply, "舵机指令");
    }

    // 表情：OLED 情绪面（保持 5 秒后自动回落），零机械风险。
    // talking 不开放给模型（见工具 schema），但它是合法固件表情，
    // 播报状态机走 SetFace 使用。
    std::string Face(const nlohmann::json& params) {
        static const std::vector<std::string> faces = {
            "neutral", "happy", "sad",   "angry", "surprised", "sleepy",
            "wink",    "love",  "cry",   "cool",  "talking",   "auto"};
        if (!params.contains("face") || !params["face"].is_string()) {
            return "缺少 face 参数";
        }
        const std::string face = params["face"].get<std::string>();
        if (std::find(faces.begin(), faces.end(), face) == faces.end()) {
            return "face 非法：" + face;
        }
        const HttpReply reply =
            SendFace(face, cfg_.command_timeout_ms);
        if (reply.status >= 200 && reply.status < 300) {
            g_last_requested_face = face;   // 播报结束后恢复这个表情
        }
        return ReportReply(reply, "表情指令");
    }

    /// 运行时表情（播报口型）：失败不打扰用户，只返回 false
    bool SetFace(const std::string& face) {
        const HttpReply reply = SendFace(face, expression_timeout_ms);
        return reply.status >= 200 && reply.status < 300;
    }

    /// 运行时舵机（说话微动）：失败不打扰用户，只返回 false
    bool SetServo(int servo_id, int angle) {
        nlohmann::json body{{"servo", servo_id}, {"angle", angle}};
        const HttpReply reply = RobotHttp(cfg_.base_url + "/api/control", true,
                                          expression_timeout_ms, body.dump(),
                                          "application/json");
        return reply.status >= 200 && reply.status < 300;
    }

    // 安全停车：不经过 LLM/工具/权限链路，启动与退出时直接调用
    bool Stop() {
        nlohmann::json body{{"cmd", "stop"}, {"speed", cfg_.default_speed}};
        const HttpReply reply = RobotHttp(cfg_.base_url + "/api/control", true,
                                          cfg_.command_timeout_ms, body.dump(),
                                          "application/json");
        if (reply.status < 200 || reply.status >= 300) {
            LOG_WARN("robot: 停车失败（HTTP {} / {}）", reply.status,
                     reply.error.c_str());
            return false;
        }
        return true;
    }

private:
    HttpReply SendFace(const std::string& face, long timeout_ms) {
        nlohmann::json body{{"face", face}};
        return RobotHttp(cfg_.base_url + "/api/control", true, timeout_ms,
                         body.dump(), "application/json");
    }

    RobotControlConfig cfg_;
};

}  // namespace

bool StopRobotMotion() {
    const RobotControlConfig& cfg = ProsophorConfig::GetInstance().robot;
    if (!cfg.enabled) return false;
    RobotControl control(cfg);
    return control.Stop();
}

bool SetRobotFace(const std::string& face) {
    const RobotControlConfig& cfg = ProsophorConfig::GetInstance().robot;
    if (!cfg.enabled) return false;
    RobotControl control(cfg);
    return control.SetFace(face);
}

bool SetRobotServo(int servo_id, int angle) {
    const RobotControlConfig& cfg = ProsophorConfig::GetInstance().robot;
    if (!cfg.enabled) return false;
    RobotControl control(cfg);
    return control.SetServo(servo_id, angle);
}

std::string TakeLastRequestedFace() {
    std::string face = std::move(g_last_requested_face);
    g_last_requested_face.clear();
    return face;
}

int TakeLastRequestedClawAngle() {
    const int angle = g_last_requested_claw_angle;
    g_last_requested_claw_angle = 0;
    return angle;
}

void RegisterRobotControlTools(prosophor::AgentEngine& engine) {
    const RobotControlConfig& cfg = ProsophorConfig::GetInstance().robot;
    if (!cfg.enabled) {
        LOG_INFO("robot: 控制工具未启用（robot.control_enabled=false）");
        return;
    }

    auto control = std::make_shared<RobotControl>(cfg);

    {
        nlohmann::json params = nlohmann::json::object();
        params["direction"] = {{"type", "string"},
                               {"enum", {"forward", "backward", "left", "right", "stop"}},
                               {"description", "forward 前进 / backward 后退 / left·right 原地转 / stop 停车（显式发，没有松手即停）"}};
        params["speed"] = {{"type", "integer"},
                           {"description", "速度 0~100，缺省 50"}};
        engine.RegisterTool(
            "robot_move",
            "控制机器人移动。它是固定短脉冲：按方向走约 1 秒后自动停车（防止跑飞），"
            "要移动更长距离须多次调用。",
            params, [control](const nlohmann::json& p) { return control->Move(p); });
    }

    // 不注册 robot_servo / robot_status：AgentChat 开环执行，结果不回灌，
    // 查询类工具（状态/角度）拿不到结果没有意义。语义级的 robot_claw 内部
    // 复用 Servo()，端侧模型不需要机械参数。

    // 语义级动作工具：把"爪子开合"从机械参数（6 号舵机、25/70 度）里解耦。
    // 端侧模型不需要知道硬件细节 —— 指令越短，模型越不容易在参数上出错
    //（板上实测：让模型自己把"张开"映射成 servo_id=6/angle=70 时经常选错或空输出）。
    {
        nlohmann::json params = nlohmann::json::object();
        params["action"] = {{"type", "string"},
                            {"enum", {"open", "close"}},
                            {"description", "open 张开 / close 合上"}};
        engine.RegisterTool(
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
                const int angle = action == "open" ? 70 : 25;
                const std::string result =
                    control->Servo({{"servo_id", 6}, {"angle", angle}});
                if (result.find("\"ok\"") != std::string::npos &&
                    result.find("false") == std::string::npos) {
                    g_last_requested_claw_angle = angle;   // 播报结束后恢复
                }
                return result;
            });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["face"] = {{"type", "string"},
                          {"enum", {"neutral", "happy", "sad", "angry", "surprised",
                                    "sleepy", "wink", "love", "cry", "cool", "auto"}},
                          {"description", "neutral平静/happy开心/sad难过/angry生气/surprised惊讶/sleepy困/wink调皮/love喜欢/cry哭/cool酷，auto=恢复默认"}};
        engine.RegisterTool(
            "robot_face",
            "让机器人 OLED 显示一个情绪表情（保持 5 秒后自动回落默认表情）。"
            "表达情绪、回应夸奖或配合对话时使用。",
            params, [control](const nlohmann::json& p) { return control->Face(p); });
    }

    LOG_INFO("robot: 控制工具已注册（{}，脉冲 {}ms）", cfg.base_url.c_str(),
             cfg.move_pulse_ms);
}

}  // namespace prosophor::nr::nutribot

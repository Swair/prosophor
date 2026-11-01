// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
//
// 机器人底盘/舵机控制工具：把远程主控的 HTTP 接口（POST /api/control、
// GET /api/status）注册成 LLM 可调用的 tool，由 AgentCore 工具循环驱动。
//
// 归属：这是 NutriBot 产品域的设备动作，不属于 prosophor_core。TUI/SDL/Web
// 不注册这组工具；core 也不再为它们添加权限规则。
//
// 安全设计（运动是物理动作，和文件操作不同一个量级）：
//   1. 总开关 robot.control_enabled 默认关 —— 关闭时本组工具根本不注册
//   2. 运动是**固定短脉冲**：工具内部计时，到点自动发 stop（不给"一直走"的
//      调用形态 —— 模型不会记得发 stop，这是拿物理设备赌生成稳定性）
//   3. 对外只暴露动作：移动 / 爪子 / 表情；AgentChat 开环执行，查询类工具不注册
//   4. 播报口型（talking）不交给模型：由常驻会话的播报状态机直接驱动，
//      说完自动交还底盘默认表情（auto）
#pragma once

#include <string>

namespace prosophor {
class AgentEngine;
}  // namespace prosophor

namespace prosophor::nr::nutribot {

/// 注册机器人控制工具（仅当 robot.control_enabled 时有效）。
/// 必须在创建使用这些工具的角色会话之前调用。
void RegisterRobotControlTools(prosophor::AgentEngine& engine);

/// 系统安全动作：直接向主控发 stop，绕过 agent/工具/权限链路。
/// 启动与退出时调用；robot 未启用或接口不可达时返回 false。
bool StopRobotMotion();

/// 运行时表情（说话口型等）：绕过 agent/工具/权限链路，失败只返回 false。
/// 装饰性指令，超时短于常规控制指令，避免底盘离线时拖住主循环。
bool SetRobotFace(const std::string& face);

/// 运行时舵机（说话微动等）：绕过 agent/工具/权限链路，失败只返回 false。
bool SetRobotServo(int servo_id, int angle);

/// 取出并清空最近一次由 robot_face 工具请求的表情（播报结束后恢复用）。
/// 空串表示没有待恢复的表情。
std::string TakeLastRequestedFace();

/// 取出并清空最近一次由 robot_claw 工具请求的爪子角度（播报结束后恢复用）。
/// 0 表示没有待恢复的爪子动作。
int TakeLastRequestedClawAngle();

}  // namespace prosophor::nr::nutribot

// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
//
// 机器人底盘/舵机控制工具：把远程主控的 HTTP 接口（POST /api/control、
// GET /api/status）注册成 LLM 可调用的 tool，由 AgentCore 工具循环驱动。
//
// 安全设计（运动是物理动作，和文件操作不同一个量级）：
//   1. 总开关 robot.control_enabled 默认关 —— 关闭时本组工具根本不注册
//   2. 运动是**固定短脉冲**：工具内部计时，到点自动发 stop（不给"一直走"的
//      调用形态 —— 模型不会记得发 stop，这是拿物理设备赌生成稳定性）
//   3. 对外只暴露三件套：移动 / 舵机 / 读状态；每发一次指令模型都能就地读到回执
#pragma once

namespace prosophor {

class ToolRegistry;

/// 注册机器人控制工具（仅当 robot.control_enabled 时有效）。
/// 由 ToolRegistry::RegisterBuiltinTools 调用。
void RegisterRobotControlTools(ToolRegistry& registry);

}  // namespace prosophor

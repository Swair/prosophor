// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "common/input_event.h"
#include "common/noncopyable.h"
#include "core/agent_state_visualizer.h"  // For AgentRuntimeState
#include <memory>
#include <atomic>
#include <functional>

namespace aicode {

/// 状态回调类型
using StateCallback = std::function<void(AgentRuntimeState, const std::string&)>;

/// 输入管理器基类 - 所有输入源的抽象接口
class InputManager : public Noncopyable {
public:
    virtual ~InputManager() = default;

    /// 启动输入循环
    virtual void Start() = 0;

    /// 停止输入循环
    virtual void Stop() = 0;

    /// 设置事件回调
    virtual void SetCallback(InputEventCallback cb) { callback_ = cb; }

    /// 设置状态回调（用于在输入框下方显示状态）
    virtual void SetStateCallback(StateCallback cb) { state_callback_ = cb; }

    /// 判断是否正在运行
    virtual bool IsRunning() const = 0;

protected:
    InputEventCallback callback_;
    StateCallback state_callback_;
};

/// 工厂方法：创建终端输入管理器
std::unique_ptr<InputManager> CreateTerminalInput();

}  // namespace aicode

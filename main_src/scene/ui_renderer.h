// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "components/ui_types.h"
#include "scene/layout_config.h"
#include <string>
#include <functional>
#include <memory>

// ============================================================================
// UI 渲染器 - 统一处理 SDL + ImGui 渲染，对外不暴露 ImGui/SDL 头文件
// 注意：不能依赖 scene/core/managers 等业务模块，只允许依赖 common/ 和第三方库
// ============================================================================

namespace prosophor {

class ChatPanel;
class InputPanel;
class StatusBar;

/// UI 渲染器 - 统一处理 SDL + ImGui 渲染
class UIRenderer {
public:
    static UIRenderer& Instance();

    // 初始化
    void Initialize();

    // SDL 渲染部分
    void Render();

    // ImGui 渲染部分（在 ImGuiNewFrame 之后调用）
    void RenderImGui();

    // 更新布局（窗口大小改变时调用）
    void UpdateLayout();

    // 输入处理
    bool ProcessInput(std::string& out_message);

    // 消息管理
    void SendToChatPanel(const std::string& role, const std::string& content);
    void StartAssistantMessage();  // 开始流式响应时创建空消息
    void UpdateLastMessage(const std::string& content);
    void SubmitUserMessage(const std::string& message);
    void ClearHistory();

    // 状态管理
    void SetStatusText(const std::string& status);
    void SetVisible(bool visible);
    bool IsVisible() const { return visible_; }

    // 设置状态（不依赖外部 header）
    void SetAgentState(AgentRuntimeState state);
    AgentRuntimeState GetAgentState() const { return current_state_; }

    // 回调：消息提交时触发
    using MessageSubmitCallback = std::function<void(const std::string&)>;
    void SetOnMessageSubmit(MessageSubmitCallback cb);

    // 设置状态视觉属性获取器（回调方式，避免依赖）
    using StatePropsGetter = std::function<StateVisualProps(AgentRuntimeState)>;
    void SetStatePropsGetter(StatePropsGetter getter);

    // 浮动文本渲染（不依赖 ImGui 的 SDL 文本渲染）
    void RenderFloatingText(const std::string& text, float x, float y,
                           uint8_t r, uint8_t g, uint8_t b, float alpha = 1.0f);

private:
    UIRenderer();
    ~UIRenderer();

    // 成员变量
    bool visible_ = true;

    std::string status_text_ = "Ready";

    float chat_area_height_ = 700.0f;
    float input_area_height_ = 100.0f;
    float bottom_status_height_ = 30.0f;

    // 聚合组件
    std::unique_ptr<ChatPanel> chat_panel_;
    std::unique_ptr<InputPanel> input_panel_;
    std::unique_ptr<StatusBar> status_bar_;

    MessageSubmitCallback on_message_submit_;
    StatePropsGetter state_props_getter_;
    AgentRuntimeState current_state_ = AgentRuntimeState::IDLE;
};

}  // namespace prosophor

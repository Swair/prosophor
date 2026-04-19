// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "ui_panel.h"
#include "ui_types.h"
#include <string>
#include <functional>
#include <memory>

namespace aicode {

/// 状态栏 - 可复用的状态显示组件
class StatusBar {
public:
    StatusBar(float x, float y, float width, float height);
    ~StatusBar() = default;

    void SetPosition(float x, float y);
    void SetSize(float width, float height);

    void Render() const;
    void RenderContent(const std::string& status_text, AgentRuntimeState state);

    using StatePropsGetter = std::function<StateVisualProps(AgentRuntimeState)>;
    void SetStatePropsGetter(StatePropsGetter getter);

    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }

private:
    std::unique_ptr<UIPanel> panel_;
    StatePropsGetter state_props_getter_;
    bool visible_ = true;
};

}  // namespace aicode

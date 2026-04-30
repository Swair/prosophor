// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "ui_component/ui_panel.h"
#include <functional>
#include <string>

namespace prosophor {

/// 容器面板 - 带 ImGui 内容回调的面板
///
/// 用法：
///   UIContainer container(x, y, w, h, style);
///   container.SetContentCallback([](float cx, float cy, float cw, float ch) {
///       // 在内容区域内渲染 ImGui 控件
///   });
class UIContainer : public UIPanel {
public:
    using ContentCallback = std::function<void(float content_x, float content_y,
                                                float content_width, float content_height)>;

    UIContainer(float x, float y, float width, float height,
                PanelStyle style = PanelStyle::Default());

    void SetContentCallback(ContentCallback cb);
    void RenderContent(const std::string& name = "ContainerContent");

private:
    ContentCallback content_callback_;
};

}  // namespace prosophor

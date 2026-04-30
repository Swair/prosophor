// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "media/colors.h"
#include <string>

namespace prosophor {

/// 标题栏组件 - 用于面板顶部的标题条
class HeaderBar {
public:
    HeaderBar(float x, float y, float width, float height);

    void Render(const std::string& title, Color bg_color = Color(30, 30, 30, 220)) const;
    void SetPosition(float x, float y) { x_ = x; y_ = y; }
    void SetSize(float width, float height) { width_ = width; height_ = height; }

private:
    float x_, y_;
    float width_, height_;
};

}  // namespace prosophor

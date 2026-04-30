// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/noncopyable.h"
#include <string>
#include <functional>

namespace prosophor {

/// UI 运行模式
enum class UIMode {
    HOME,       // 首页选择
    GALGAME,    // 剧情模式
    VIRTUAL_HUMAN,  // 虚拟人交互
    TERMINAL,   // 终端模式
};

/// HomeScreen: 首页三模式选择器
class HomeScreen : public Noncopyable {
 public:
    static HomeScreen& GetInstance();

    void Initialize();

    /// 渲染首页（ImGui 窗口）
    void Render();

    /// 模式选择回调
    using ModeSelectCallback = std::function<void(UIMode mode)>;
    void SetOnModeSelect(ModeSelectCallback cb);

 private:
    HomeScreen() = default;

    void DrawTitle();
    void DrawModeButtons();
    void DrawFooter();

    ModeSelectCallback on_mode_select_;
    float animation_time_ = 0.0f;
};

}  // namespace prosophor

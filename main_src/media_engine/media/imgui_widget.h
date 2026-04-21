#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <cstdarg>
#include "colors.h"

// ============================================================================
// ImGui 组件封装 - 使用 Impl 模式，对外不暴露 imgui.h
// 所有组件都使用 Pimpl 模式，头文件不依赖 ImGui
// ============================================================================

namespace imgui_widget {

// 回调类型定义
using VoidCallback = std::function<void()>;
using BoolCallback = std::function<void(bool)>;
using FloatCallback = std::function<void(float)>;
using StringCallback = std::function<void(const std::string&)>;
using ColorCallback = std::function<void(float[4])>;

// ============================================================================
// 按钮组件
// ============================================================================
class Button {
public:
    Button(const std::string& label, VoidCallback on_click = nullptr);
    ~Button();

    bool Render();

    void SetLabel(const std::string& label);
    void SetOnClick(VoidCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 图标按钮组件
// ============================================================================
class IconButton {
public:
    IconButton(const std::string& icon, const std::string& tooltip,
               VoidCallback on_click = nullptr);
    ~IconButton();

    bool Render();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 滑块组件
// ============================================================================
class Slider {
public:
    Slider(const std::string& label, float min_val, float max_val,
           float default_val, FloatCallback on_value_changed = nullptr);
    ~Slider();

    float GetValue() const;
    void SetValue(float val);
    bool Render();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 复选框组件
// ============================================================================
class Checkbox {
public:
    Checkbox(const std::string& label, bool default_state = false,
             BoolCallback on_state_changed = nullptr);
    ~Checkbox();

    bool GetState() const;
    void SetState(bool state);
    bool Render();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 颜色选择器组件
// ============================================================================
class ColorPicker {
public:
    ColorPicker(const std::string& label,
                float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f,
                ColorCallback on_color_changed = nullptr);
    ~ColorPicker();

    void GetColor(float out_color[4]) const;
    void SetColor(float r, float g, float b, float a);
    bool Render();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 文本输入框组件
// ============================================================================
class InputText {
public:
    InputText(const std::string& label, const std::string& default_text = "",
              int max_length = 256, StringCallback on_text_changed = nullptr);
    ~InputText();

    std::string GetText() const;
    void SetText(const std::string& text);

    // 获取底层 buffer 指针（用于细粒度控制）
    char* GetBuffer();
    int GetBufferSize() const;

    // 设置标志
    void SetEnterReturnsTrue(bool enable);
    bool IsEnterPressed() const;  // 检查 Enter 是否被按下

    bool Render();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 确认对话框
// ============================================================================
class ConfirmDialog {
public:
    using ConfirmCallback = VoidCallback;
    using CancelCallback = VoidCallback;

    ConfirmDialog(const std::string& title, const std::string& message,
                  ConfirmCallback on_confirm = nullptr,
                  CancelCallback on_cancel = nullptr);
    ~ConfirmDialog();

    void Open();
    void Close();
    bool IsOpen() const;
    void Render();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 消息对话框（仅确定按钮）
// ============================================================================
class MessageDialog {
public:
    MessageDialog(const std::string& title, const std::string& message,
                  VoidCallback on_close = nullptr);
    ~MessageDialog();

    void Open();
    void Close();
    bool IsOpen() const;
    void Render();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 文件选择对话框
// ============================================================================
class FilePicker {
public:
    using FileSelectedCallback = StringCallback;

    FilePicker(const std::string& title, const std::string& filter = "All Files,*.*",
               FileSelectedCallback on_file_selected = nullptr);
    ~FilePicker();

    void Open();
    void Close();
    bool IsOpen() const;
    void Render();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 日志控制台组件
// ============================================================================
class LogConsole {
public:
    LogConsole(size_t max_lines = 1000);
    ~LogConsole();

    void Log(const std::string& message, unsigned int color = 0xFFFFFFFF);
    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);
    void Success(const std::string& message);

    void Render(const std::string& title, bool* open = nullptr);
    void Clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 进度条组件
// ============================================================================
class ProgressBar {
public:
    ProgressBar();
    ~ProgressBar();

    void Render(const std::string& label, float fraction,
                float width = -1.0f, float height = 0.0f);
    void RenderWithOverlay(const std::string& label, float fraction,
                           const std::string& overlay,
                           float width = -1.0f, float height = 0.0f);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// ImGui 工具函数封装 - 避免外部文件直接 include imgui.h
// ============================================================================

// ImGui 窗口标志（常用）
constexpr int ImGuiWindowFlags_NoDecoration = 1 << 1;
constexpr int ImGuiWindowFlags_NoMove = 1 << 2;
constexpr int ImGuiWindowFlags_NoSavedSettings = 1 << 5;
constexpr int ImGuiWindowFlags_NoFocusOnAppearing = 1 << 6;
constexpr int ImGuiWindowFlags_NoScrollbar = 1 << 7;
constexpr int ImGuiWindowFlags_NoScrollWithMouse = 1 << 8;
constexpr int ImGuiWindowFlags_AlwaysVerticalScrollbar = 1 << 9;
constexpr int ImGuiWindowFlags_None = 0;

// ImGui Child 标志
constexpr int ImGuiChildFlags_None = 0;
constexpr int ImGuiChildFlags_Borders = 1 << 1;

// ImGui 样式颜色
constexpr int ImGuiCol_ChildBg = 2;
constexpr int ImGuiCol_Border = 5;

// ImVec2 封装
struct ImVec2Wrapper {
    float x, y;
    ImVec2Wrapper(float _x = 0, float _y = 0) : x(_x), y(_y) {}
};

// ImVec4 封装
struct ImVec4Wrapper {
    float x, y, z, w;
    ImVec4Wrapper(float _x = 0, float _y = 0, float _z = 0, float _w = 0) : x(_x), y(_y), z(_z), w(_w) {}
};

/// 获取程序运行时间（秒）
double GetImGuiTime();

/// 窗口位置/尺寸设置
void SetImGuiNextWindowPos(float x, float y);
void SetImGuiNextWindowSize(float w, float h);
void SetImGuiNextWindowBgAlpha(float alpha);

/// 窗口控制
bool ImGuiBegin(const char* name, bool* open = nullptr, int flags = 0);
void ImGuiEnd();

/// 布局控制
void ImGuiPushItemWidth(float width);
void ImGuiPopItemWidth();
void ImGuiSameLine();
void ImGuiSetCursorPos(float x, float y);

/// 样式控制
void ImGuiPushTextWrapPos(float wrap_width);
void ImGuiPopTextWrapPos();
void ImGuiPushStyleVar_ItemSpacing(float x, float y);
void ImGuiPopStyleVar(int count = 1);

/// 样式颜色
void ImGuiPushStyleColor_TitleText(const Color& color);
void ImGuiPopStyleColor_TitleText(int count = 1);

/// 文本渲染
void ImGuiText(const char* fmt, ...);
void ImGuiTextUnformatted(const char* text);
/// 文本渲染（自动换行，wrap_width=0 表示窗口右边界）
void ImGuiTextWrapped(const char* text, float wrap_width = 0.0f, const Color& color = Colors::White);
/// 文本渲染（带颜色，无换行）
void ImGuiTextColored(const Color& color, const char* text);
/// 文本渲染（统一接口：颜色 + 可选换行）
void ImGuiText(const char* text, const Color& color = Colors::White, float wrap_width = 0.0f);

/// 滚动
void ImGuiSetScrollHereY(float center_y_ratio = 0.5f);

/// Child 窗口
bool BeginChild(const char* name, float width = 0.0f, float height = 0.0f, int child_flags = 0);
bool BeginChild(const char* name, float width, float height, int child_flags, int window_flags);
void EndChild();

/// 光标位置
float GetCursorPosY();
void SetCursorPosY(float y);

/// 文本尺寸计算
void CalcTextSize(float* out_x, float* out_y, const char* text_begin, const char* text_end = nullptr, bool hide_text_after_double_hash = false, float wrap_width = -1.0f);

/// 滚动控制
float GetScrollY();
float GetScrollMaxY();
void SetScrollY(float scroll_y);

/// 占位符（用于增长窗口边界）
void Dummy(float width, float height);

/// 样式颜色
void PushStyleColor(int color_index, const Color& color);
void PopStyleColor(int count = 1);

// ============================================================================
// 属性编辑器组件
// ============================================================================
class PropertyEditor {
public:
    PropertyEditor();
    ~PropertyEditor();

    void Begin(const std::string& label);
    void End();

    void AddText(const std::string& key, const std::string& value);
    void AddSlider(const std::string& key, float* value, float min, float max);
    void AddCheckbox(const std::string& key, bool* value);
    void AddColorPicker(const std::string& key, float value[4]);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// 滚动窗口 - 通用滚动容器
// ============================================================================
// 用法：
//   ScrollWindow scrollWin(x, y, w, h);
//   scrollWin.Begin("MyScroll");
//   // ... 添加内容 ...
//   scrollWin.End();
// ============================================================================
class ScrollWindow {
public:
    ScrollWindow(float x, float y, float width, float height);
    ~ScrollWindow();

    bool Begin(const std::string& name, const Color* title_color = nullptr);
    void End();

    void SetPosition(float x, float y);
    void SetSize(float width, float height);

    float GetX() const { return x_; }
    float GetY() const { return y_; }
    float GetWidth() const { return width_; }
    float GetHeight() const { return height_; }

    void ScrollToBottom();
    bool IsScrolledToBottom() const;

private:
    float x_, y_;
    float width_, height_;
    bool scroll_to_bottom_ = false;
    bool has_title_color_ = false;  // 记录是否设置了标题颜色
};

} // namespace imgui_widget

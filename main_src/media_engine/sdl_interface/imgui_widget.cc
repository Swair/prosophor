/**
 * @file imgui_widget.cc
 * @brief ImGui 组件封装实现 - Impl 模式，对外不暴露 ImGui 依赖
 *
 * 所有 ImGui 相关代码都隐藏在此文件中，外部模块通过 imgui_widget.h 调用。
 */

#include "media/imgui_widget.h"
#include "media/colors.h"

// 所有 ImGui 依赖都隐藏在实现文件中
#include "imgui.h"
#include "imgui_internal.h"

namespace imgui_widget {

// ============================================================================
// Button 实现 - 按钮组件
// ============================================================================
struct Button::Impl {
    std::string label_;         // 按钮文本
    VoidCallback on_click_;     // 点击回调
};

/**
 * @brief 构造按钮对象
 * @param label 按钮显示文本
 * @param on_click 点击回调函数
 */
Button::Button(const std::string& label, VoidCallback on_click)
    : impl_(std::make_unique<Impl>()) {
    impl_->label_ = label;
    impl_->on_click_ = on_click;
}

/**
 * @brief 析构函数
 */
Button::~Button() = default;

/**
 * @brief 渲染按钮
 * @return true 表示按钮被点击
 */
bool Button::Render() {
    if (ImGui::Button(impl_->label_.c_str())) {
        if (impl_->on_click_) {
            impl_->on_click_();
        }
        return true;
    }
    return false;
}

/**
 * @brief 设置按钮文本
 * @param label 新的按钮文本
 */
void Button::SetLabel(const std::string& label) {
    impl_->label_ = label;
}

/**
 * @brief 设置点击回调
 * @param cb 点击回调函数
 */
void Button::SetOnClick(VoidCallback cb) {
    impl_->on_click_ = cb;
}

// ============================================================================
// IconButton 实现 - 图标按钮组件
// ============================================================================
struct IconButton::Impl {
    std::string icon_;          // 图标文本（如 "X", "✓"）
    std::string tooltip_;       // 悬停提示
    VoidCallback on_click_;     // 点击回调
};

/**
 * @brief 构造图标按钮对象
 * @param icon 图标文本
 * @param tooltip 悬停时显示的提示信息
 * @param on_click 点击回调函数
 */
IconButton::IconButton(const std::string& icon, const std::string& tooltip,
                       VoidCallback on_click)
    : impl_(std::make_unique<Impl>()) {
    impl_->icon_ = icon;
    impl_->tooltip_ = tooltip;
    impl_->on_click_ = on_click;
}

/**
 * @brief 析构函数
 */
IconButton::~IconButton() = default;

/**
 * @brief 渲染图标按钮
 * @return true 表示按钮被点击
 */
bool IconButton::Render() {
    ImGui::PushID(this);  // 使用指针作为 ID，避免同名冲突
    bool clicked = ImGui::Button(impl_->icon_.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", impl_->tooltip_.c_str());
    }
    if (clicked && impl_->on_click_) {
        impl_->on_click_();
    }
    ImGui::PopID();
    return clicked;
}

// ============================================================================
// Slider 实现 - 滑块组件
// ============================================================================
struct Slider::Impl {
    std::string label_;                 // 标签
    float min_val_;                     // 最小值
    float max_val_;                     // 最大值
    float value_;                       // 当前值
    FloatCallback on_value_changed_;    // 值变化回调
};

/**
 * @brief 构造滑块对象
 * @param label 滑块标签
 * @param min_val 最小值
 * @param max_val 最大值
 * @param default_val 默认值
 * @param on_value_changed 值变化回调函数
 */
Slider::Slider(const std::string& label, float min_val, float max_val,
               float default_val, FloatCallback on_value_changed)
    : impl_(std::make_unique<Impl>()) {
    impl_->label_ = label;
    impl_->min_val_ = min_val;
    impl_->max_val_ = max_val;
    impl_->value_ = default_val;
    impl_->on_value_changed_ = on_value_changed;
}

/**
 * @brief 析构函数
 */
Slider::~Slider() = default;

/**
 * @brief 获取当前值
 * @return 当前滑块值
 */
float Slider::GetValue() const {
    return impl_->value_;
}

/**
 * @brief 设置当前值
 * @param val 新的值
 */
void Slider::SetValue(float val) {
    impl_->value_ = val;
}

/**
 * @brief 渲染滑块
 * @return true 表示值发生变化
 */
bool Slider::Render() {
    if (ImGui::SliderFloat(impl_->label_.c_str(), &impl_->value_,
                           impl_->min_val_, impl_->max_val_)) {
        if (impl_->on_value_changed_) {
            impl_->on_value_changed_(impl_->value_);
        }
        return true;
    }
    return false;
}

// ============================================================================
// Checkbox 实现 - 复选框组件
// ============================================================================
struct Checkbox::Impl {
    std::string label_;             // 标签
    bool state_;                    // 当前状态
    BoolCallback on_state_changed_; // 状态变化回调
};

/**
 * @brief 构造复选框对象
 * @param label 复选框标签
 * @param default_state 默认状态（true=选中）
 * @param on_state_changed 状态变化回调函数
 */
Checkbox::Checkbox(const std::string& label, bool default_state,
                   BoolCallback on_state_changed)
    : impl_(std::make_unique<Impl>()) {
    impl_->label_ = label;
    impl_->state_ = default_state;
    impl_->on_state_changed_ = on_state_changed;
}

/**
 * @brief 析构函数
 */
Checkbox::~Checkbox() = default;

/**
 * @brief 获取当前状态
 * @return true 表示选中
 */
bool Checkbox::GetState() const {
    return impl_->state_;
}

/**
 * @brief 设置状态
 * @param state 新的状态（true=选中）
 */
void Checkbox::SetState(bool state) {
    impl_->state_ = state;
}

/**
 * @brief 渲染复选框
 * @return true 表示状态发生变化
 */
bool Checkbox::Render() {
    if (ImGui::Checkbox(impl_->label_.c_str(), &impl_->state_)) {
        if (impl_->on_state_changed_) {
            impl_->on_state_changed_(impl_->state_);
        }
        return true;
    }
    return false;
}

// ============================================================================
// ColorPicker 实现 - 颜色选择器组件
// ============================================================================
struct ColorPicker::Impl {
    std::string label_;             // 标签
    float color_[4];                // RGBA 颜色值 [0-1]
    ColorCallback on_color_changed_; // 颜色变化回调
};

/**
 * @brief 构造颜色选择器对象
 * @param label 标签
 * @param r 红色分量 (0-1)
 * @param g 绿色分量 (0-1)
 * @param b 蓝色分量 (0-1)
 * @param a 透明度分量 (0-1)
 * @param on_color_changed 颜色变化回调函数
 */
ColorPicker::ColorPicker(const std::string& label,
                         float r, float g, float b, float a,
                         ColorCallback on_color_changed)
    : impl_(std::make_unique<Impl>()) {
    impl_->label_ = label;
    impl_->color_[0] = r;
    impl_->color_[1] = g;
    impl_->color_[2] = b;
    impl_->color_[3] = a;
    impl_->on_color_changed_ = on_color_changed;
}

/**
 * @brief 析构函数
 */
ColorPicker::~ColorPicker() = default;

/**
 * @brief 获取当前颜色
 * @param out_color 输出数组 [r,g,b,a]
 */
void ColorPicker::GetColor(float out_color[4]) const {
    out_color[0] = impl_->color_[0];
    out_color[1] = impl_->color_[1];
    out_color[2] = impl_->color_[2];
    out_color[3] = impl_->color_[3];
}

/**
 * @brief 设置颜色
 * @param r 红色分量 (0-1)
 * @param g 绿色分量 (0-1)
 * @param b 蓝色分量 (0-1)
 * @param a 透明度分量 (0-1)
 */
void ColorPicker::SetColor(float r, float g, float b, float a) {
    impl_->color_[0] = r;
    impl_->color_[1] = g;
    impl_->color_[2] = b;
    impl_->color_[3] = a;
}

/**
 * @brief 渲染颜色选择器
 * @return true 表示颜色发生变化
 */
bool ColorPicker::Render() {
    if (ImGui::ColorEdit4(impl_->label_.c_str(), impl_->color_)) {
        if (impl_->on_color_changed_) {
            impl_->on_color_changed_(impl_->color_);
        }
        return true;
    }
    return false;
}

// ============================================================================
// InputText 实现 - 文本输入框组件
// ============================================================================
struct InputText::Impl {
    std::string label_;             // 标签
    std::vector<char> buffer_;      // 输入缓冲区
    StringCallback on_text_changed_; // 文本变化回调
    bool enter_returns_true_ = false; // Enter 键返回 true
    bool enter_pressed_ = false;    // Enter 是否被按下
};

/**
 * @brief 构造文本输入框对象
 * @param label 标签
 * @param default_text 默认文本
 * @param max_length 最大长度
 * @param on_text_changed 文本变化回调函数
 */
InputText::InputText(const std::string& label, const std::string& default_text,
                     int max_length, StringCallback on_text_changed)
    : impl_(std::make_unique<Impl>()) {
    impl_->label_ = label;
    impl_->buffer_.resize(max_length);
    strncpy(impl_->buffer_.data(), default_text.c_str(), max_length - 1);
    impl_->buffer_[max_length - 1] = '\0';
    impl_->on_text_changed_ = on_text_changed;
}

/**
 * @brief 析构函数
 */
InputText::~InputText() = default;

/**
 * @brief 获取当前文本
 * @return 当前输入的文本
 */
std::string InputText::GetText() const {
    return std::string(impl_->buffer_.data());
}

/**
 * @brief 设置文本
 * @param text 新的文本内容
 */
void InputText::SetText(const std::string& text) {
    strncpy(impl_->buffer_.data(), text.c_str(), impl_->buffer_.size() - 1);
}

/**
 * @brief 获取底层缓冲区指针（用于细粒度控制）
 * @return 缓冲区指针
 */
char* InputText::GetBuffer() {
    return impl_->buffer_.data();
}

/**
 * @brief 获取缓冲区大小
 * @return 缓冲区容量
 */
int InputText::GetBufferSize() const {
    return static_cast<int>(impl_->buffer_.size());
}

/**
 * @brief 设置 Enter 键返回 true
 * @param enable true=启用
 */
void InputText::SetEnterReturnsTrue(bool enable) {
    impl_->enter_returns_true_ = enable;
}

/**
 * @brief 检查 Enter 键是否被按下
 * @return true 表示 Enter 被按下
 */
bool InputText::IsEnterPressed() const {
    return impl_->enter_pressed_;
}

/**
 * @brief 渲染输入框
 * @return true 表示文本发生变化
 */
bool InputText::Render() {
    impl_->enter_pressed_ = false;

    ImGuiInputTextFlags flags = 0;
    if (impl_->enter_returns_true_) {
        flags |= ImGuiInputTextFlags_EnterReturnsTrue;
    }

    if (ImGui::InputText(impl_->label_.c_str(), impl_->buffer_.data(),
                         impl_->buffer_.size(), flags)) {
        if (impl_->on_text_changed_) {
            impl_->on_text_changed_(std::string(impl_->buffer_.data()));
        }
        if (impl_->enter_returns_true_) {
            impl_->enter_pressed_ = true;
        }
        return true;
    }

    // 保持输入框焦点（每帧自动聚焦）
    ImGui::SetItemDefaultFocus();

    return false;
}

// ============================================================================
// ConfirmDialog 实现 - 确认对话框（OK/Cancel）
// ============================================================================
struct ConfirmDialog::Impl {
    std::string title_;             // 窗口标题
    std::string message_;           // 提示消息
    bool is_open_ = false;          // 是否打开
    ConfirmCallback on_confirm_;    // 确认回调
    CancelCallback on_cancel_;      // 取消回调
};

/**
 * @brief 构造确认对话框对象
 * @param title 窗口标题
 * @param message 提示消息
 * @param on_confirm 确认回调函数
 * @param on_cancel 取消回调函数
 */
ConfirmDialog::ConfirmDialog(const std::string& title, const std::string& message,
                             ConfirmCallback on_confirm, CancelCallback on_cancel)
    : impl_(std::make_unique<Impl>()) {
    impl_->title_ = title;
    impl_->message_ = message;
    impl_->on_confirm_ = on_confirm;
    impl_->on_cancel_ = on_cancel;
}

/**
 * @brief 析构函数
 */
ConfirmDialog::~ConfirmDialog() = default;

/**
 * @brief 打开对话框
 */
void ConfirmDialog::Open() {
    impl_->is_open_ = true;
}

/**
 * @brief 关闭对话框
 */
void ConfirmDialog::Close() {
    impl_->is_open_ = false;
}

/**
 * @brief 检查对话框是否打开
 * @return true 表示对话框处于打开状态
 */
bool ConfirmDialog::IsOpen() const {
    return impl_->is_open_;
}

/**
 * @brief 渲染对话框
 */
void ConfirmDialog::Render() {
    if (!impl_->is_open_) return;

    ImGui::OpenPopup(impl_->title_.c_str());

    // 窗口居中显示
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0));

    if (ImGui::BeginPopupModal(impl_->title_.c_str(), &impl_->is_open_,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", impl_->message_.c_str());
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (impl_->on_confirm_) {
                impl_->on_confirm_();
            }
            impl_->is_open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            if (impl_->on_cancel_) {
                impl_->on_cancel_();
            }
            impl_->is_open_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ============================================================================
// MessageDialog 实现 - 消息对话框（仅 OK 按钮）
// ============================================================================
struct MessageDialog::Impl {
    std::string title_;             // 窗口标题
    std::string message_;           // 提示消息
    bool is_open_ = false;          // 是否打开
    VoidCallback on_close_;         // 关闭回调
};

/**
 * @brief 构造消息对话框对象
 * @param title 窗口标题
 * @param message 提示消息
 * @param on_close 关闭回调函数
 */
MessageDialog::MessageDialog(const std::string& title, const std::string& message,
                             VoidCallback on_close)
    : impl_(std::make_unique<Impl>()) {
    impl_->title_ = title;
    impl_->message_ = message;
    impl_->on_close_ = on_close;
}

/**
 * @brief 析构函数
 */
MessageDialog::~MessageDialog() = default;

/**
 * @brief 打开对话框
 */
void MessageDialog::Open() {
    impl_->is_open_ = true;
}

/**
 * @brief 关闭对话框
 */
void MessageDialog::Close() {
    impl_->is_open_ = false;
}

/**
 * @brief 检查对话框是否打开
 * @return true 表示对话框处于打开状态
 */
bool MessageDialog::IsOpen() const {
    return impl_->is_open_;
}

/**
 * @brief 渲染对话框
 */
void MessageDialog::Render() {
    if (!impl_->is_open_) return;

    ImGui::OpenPopup(impl_->title_.c_str());

    // 窗口居中显示
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(impl_->title_.c_str(), &impl_->is_open_,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", impl_->message_.c_str());
        ImGui::Separator();

        // 点击 OK 或按 Enter 键关闭
        if (ImGui::Button("OK", ImVec2(150, 0)) || ImGui::IsKeyDown(ImGuiKey_Enter)) {
            if (impl_->on_close_) {
                impl_->on_close_();
            }
            impl_->is_open_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ============================================================================
// FilePicker 实现 - 文件选择对话框
// ============================================================================
struct FilePicker::Impl {
    std::string title_;                         // 窗口标题
    std::string filter_;                        // 文件过滤器（如 "*.txt"）
    std::string current_directory_;             // 当前目录
    bool is_open_ = false;                      // 是否打开
    std::vector<char> filename_buffer_;         // 文件名输入缓冲
    FileSelectedCallback on_file_selected_;     // 文件选择回调

    Impl() : filename_buffer_(256) {}
};

/**
 * @brief 构造文件选择对话框对象
 * @param title 窗口标题
 * @param filter 文件过滤器（如 "All Files,*.*"）
 * @param on_file_selected 文件选择回调函数
 */
FilePicker::FilePicker(const std::string& title, const std::string& filter,
                       FileSelectedCallback on_file_selected)
    : impl_(std::make_unique<Impl>()) {
    impl_->title_ = title;
    impl_->filter_ = filter;
    impl_->on_file_selected_ = on_file_selected;
}

/**
 * @brief 析构函数
 */
FilePicker::~FilePicker() = default;

/**
 * @brief 打开对话框
 */
void FilePicker::Open() {
    impl_->is_open_ = true;
}

/**
 * @brief 关闭对话框
 */
void FilePicker::Close() {
    impl_->is_open_ = false;
}

/**
 * @brief 检查对话框是否打开
 * @return true 表示对话框处于打开状态
 */
bool FilePicker::IsOpen() const {
    return impl_->is_open_;
}

/**
 * @brief 渲染对话框
 */
void FilePicker::Render() {
    if (!impl_->is_open_) return;

    ImGui::OpenPopup(impl_->title_.c_str());

    // 窗口居中显示
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 400));

    if (ImGui::BeginPopupModal(impl_->title_.c_str(), &impl_->is_open_,
                               ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Current Directory: %s", impl_->current_directory_.c_str());
        ImGui::Separator();

        // 文件列表（占位符，需要集成文件系统）
        if (ImGui::BeginListBox("##files", ImVec2(-FLT_MIN, 250))) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "[File system integration required]");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "Integrate with SDL_filesystem or std::filesystem");
            ImGui::EndListBox();
        }

        ImGui::InputText("File Name", impl_->filename_buffer_.data(),
                         impl_->filename_buffer_.size());

        ImGui::Separator();

        if (ImGui::Button("Open", ImVec2(100, 0))) {
            if (impl_->on_file_selected_) {
                impl_->on_file_selected_(std::string(impl_->filename_buffer_.data()));
            }
            impl_->is_open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            impl_->is_open_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ============================================================================
// LogConsole 实现 - 日志控制台组件
// ============================================================================
struct LogConsole::Impl {
    struct LogMessage {
        std::string text;         // 日志文本
        unsigned int color;       // 颜色（ARGB）
    };

    std::vector<LogMessage> messages_;  // 日志消息列表
    size_t max_lines_;                   // 最大行数
    bool auto_scroll_ = true;            // 自动滚动

    Impl(size_t max_lines) : max_lines_(max_lines) {}
};

/**
 * @brief 构造日志控制台对象
 * @param max_lines 最大显示行数
 */
LogConsole::LogConsole(size_t max_lines)
    : impl_(std::make_unique<Impl>(max_lines)) {}

/**
 * @brief 析构函数
 */
LogConsole::~LogConsole() = default;

/**
 * @brief 添加日志消息
 * @param message 日志文本
 * @param color 文本颜色（ARGB 格式）
 */
void LogConsole::Log(const std::string& message, unsigned int color) {
    impl_->messages_.push_back({message, color});
    if (impl_->messages_.size() > impl_->max_lines_) {
        impl_->messages_.erase(impl_->messages_.begin());
    }
}

/**
 * @brief 添加普通信息（灰色）
 * @param message 日志文本
 */
void LogConsole::Info(const std::string& message) {
    // 灰色 - 普通信息
    Log(message, IM_COL32(200, 200, 200, 255));
}

/**
 * @brief 添加警告信息（黄色）
 * @param message 日志文本
 */
void LogConsole::Warning(const std::string& message) {
    // 黄色 - 警告信息
    Log(message, IM_COL32(255, 200, 0, 255));
}

/**
 * @brief 添加错误信息（红色）
 * @param message 日志文本
 */
void LogConsole::Error(const std::string& message) {
    // 红色 - 错误信息
    Log(message, IM_COL32(255, 50, 50, 255));
}

/**
 * @brief 添加成功信息（绿色）
 * @param message 日志文本
 */
void LogConsole::Success(const std::string& message) {
    // 绿色 - 成功信息
    Log(message, IM_COL32(50, 255, 100, 255));
}

/**
 * @brief 清空所有日志
 */
void LogConsole::Clear() {
    impl_->messages_.clear();
}

/**
 * @brief 渲染日志控制台窗口
 * @param title 窗口标题
 * @param open 窗口打开状态指针（可选）
 */
void LogConsole::Render(const std::string& title, bool* open) {
    if (ImGui::Begin(title.c_str(), open)) {
        if (ImGui::Button("Clear")) {
            Clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &impl_->auto_scroll_);

        ImGui::Separator();

        ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (const auto& msg : impl_->messages_) {
            Color color = ColorFromUInt32(msg.color);
            ImVec4 imgui_color = ImVec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, imgui_color);
            ImGui::TextUnformatted(msg.text.c_str());
            ImGui::PopStyleColor();
        }

        if (impl_->auto_scroll_ && !impl_->messages_.empty()) {
            ImGui::SetScrollHereY(1.0f);  // 滚动到底部
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// ============================================================================
// ProgressBar 实现 - 进度条组件
// ============================================================================
struct ProgressBar::Impl {
    // 无需额外成员
};

/**
 * @brief 构造进度条对象
 */
ProgressBar::ProgressBar() : impl_(std::make_unique<Impl>()) {}

/**
 * @brief 析构函数
 */
ProgressBar::~ProgressBar() = default;

/**
 * @brief 渲染进度条
 * @param label 标签文本
 * @param fraction 进度值 (0.0-1.0)
 * @param width 宽度（-1 表示自动）
 * @param height 高度（0 表示默认）
 */
void ProgressBar::Render(const std::string& label, float fraction,
                         float width, float height) {
    ImVec2 size(width, height);
    ImGui::ProgressBar(fraction, size, label.c_str());
}

/**
 * @brief 渲染带覆盖文本的进度条
 * @param label 标签（未使用）
 * @param fraction 进度值 (0.0-1.0)
 * @param overlay 覆盖在进度条上的文本
 * @param width 宽度（-1 表示自动）
 * @param height 高度（0 表示默认）
 */
void ProgressBar::RenderWithOverlay(const std::string& label, float fraction,
                                    const std::string& overlay,
                                    float width, float height) {
    ImVec2 size(width, height);
    ImGui::ProgressBar(fraction, size, overlay.c_str());
}

// ============================================================================
// PropertyEditor 实现 - 属性编辑器组件
// ============================================================================
struct PropertyEditor::Impl {
    // 无需额外成员
};

/**
 * @brief 构造属性编辑器对象
 */
PropertyEditor::PropertyEditor() : impl_(std::make_unique<Impl>()) {}

/**
 * @brief 析构函数
 */
PropertyEditor::~PropertyEditor() = default;

/**
 * @brief 开始属性编辑区域
 * @param label 区域标签
 */
void PropertyEditor::Begin(const std::string& label) {
    ImGui::BeginGroup();
    ImGui::Text("%s", label.c_str());
    ImGui::Separator();
}

/**
 * @brief 结束属性编辑区域
 */
void PropertyEditor::End() {
    ImGui::EndGroup();
}

/**
 * @brief 添加文本属性行
 * @param key 属性名
 * @param value 属性值
 */
void PropertyEditor::AddText(const std::string& key, const std::string& value) {
    ImGui::Text("%s:", key.c_str());
    ImGui::SameLine(150);  // 在 150 像素处对齐
    ImGui::Text("%s", value.c_str());
}

/**
 * @brief 添加滑块属性
 * @param key 属性名
 * @param value 值指针
 * @param min 最小值
 * @param max 最大值
 */
void PropertyEditor::AddSlider(const std::string& key, float* value, float min, float max) {
    ImGui::Text("%s:", key.c_str());
    ImGui::SameLine(150);
    ImGui::PushID(key.c_str());  // 避免同名 key 冲突
    ImGui::SliderFloat("##slider", value, min, max);
    ImGui::PopID();
}

/**
 * @brief 添加复选框属性
 * @param key 属性名
 * @param value 值指针
 */
void PropertyEditor::AddCheckbox(const std::string& key, bool* value) {
    ImGui::Text("%s:", key.c_str());
    ImGui::SameLine(150);
    ImGui::PushID(key.c_str());
    ImGui::Checkbox("##checkbox", value);
    ImGui::PopID();
}

/**
 * @brief 添加颜色选择器属性
 * @param key 属性名
 * @param value RGBA 值数组 [r,g,b,a]
 */
void PropertyEditor::AddColorPicker(const std::string& key, float value[4]) {
    ImGui::Text("%s:", key.c_str());
    ImGui::SameLine(150);
    ImGui::PushID(key.c_str());
    ImGui::ColorEdit4("##color", value);
    ImGui::PopID();
}

} // namespace imgui_widget

// ============================================================================
// ScrollWindow 实现 - 滚动窗口（通用滚动容器）
// ============================================================================

/**
 * @brief 构造滚动窗口对象
 * @param x 窗口 X 坐标
 * @param y 窗口 Y 坐标
 * @param width 窗口宽度
 * @param height 窗口高度
 */
imgui_widget::ScrollWindow::ScrollWindow(float x, float y, float width, float height)
    : x_(x), y_(y), width_(width), height_(height) {
}

/**
 * @brief 析构函数
 */
imgui_widget::ScrollWindow::~ScrollWindow() = default;

/**
 * @brief 开始滚动窗口渲染
 * @param name 窗口名称（可用作标题）
 * @param title_color 标题颜色（可选，nullptr 表示不使用自定义颜色）
 * @return true 表示窗口打开成功
 */
bool imgui_widget::ScrollWindow::Begin(const std::string& name, const Color* title_color) {
    // 设置窗口位置和大小
    SetImGuiNextWindowPos(x_, y_);
    SetImGuiNextWindowSize(width_, height_);
    SetImGuiNextWindowBgAlpha(0.0f);  // 背景透明

    // 设置标题颜色（如果提供）
    has_title_color_ = (title_color != nullptr);
    if (has_title_color_) {
        ImGuiPushStyleColor_TitleText(*title_color);
    }

    // 窗口标志：
    // - NoResize: 不可调整大小
    // - NoMove: 不可移动
    // - NoBackground: 不绘制背景（包括边框）
    // - NoSavedSettings: 不保存窗口设置
    // - NoFocusOnAppearing: 出现时不聚焦
    // - AlwaysVerticalScrollbar: 始终显示垂直滚动条
    ImGuiBegin(name.c_str(), nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGuiPushStyleVar_ItemSpacing(4, 4);  // 设置内容间距

    return true;
}

/**
 * @brief 结束滚动窗口渲染
 */
void imgui_widget::ScrollWindow::End() {
    // 如果需要滚动到底部
    if (scroll_to_bottom_) {
        ImGuiSetScrollHereY(1.0f);
        scroll_to_bottom_ = false;
    }

    ImGuiPopStyleVar();  // 恢复间距样式
    if (has_title_color_) {
        ImGuiPopStyleColor_TitleText();  // 恢复标题颜色
    }
    ImGuiEnd();
}

/**
 * @brief 设置窗口位置
 * @param x X 坐标
 * @param y Y 坐标
 */
void imgui_widget::ScrollWindow::SetPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

/**
 * @brief 设置窗口尺寸
 * @param width 宽度
 * @param height 高度
 */
void imgui_widget::ScrollWindow::SetSize(float width, float height) {
    width_ = width;
    height_ = height;
}

/**
 * @brief 标记需要滚动到底部
 */
void imgui_widget::ScrollWindow::ScrollToBottom() {
    scroll_to_bottom_ = true;
}

/**
 * @brief 检查是否已滚动到底部
 * @return true 表示已滚动到底部
 */
bool imgui_widget::ScrollWindow::IsScrolledToBottom() const {
    return scroll_to_bottom_ || GetScrollY() >= GetScrollMaxY() - 1.0f;
}

// ============================================================================
// ImGui 工具函数封装实现
// ============================================================================

/**
 * @brief 获取程序运行时间
 * @return 运行时间（秒）
 */
double imgui_widget::GetImGuiTime() {
    return ImGui::GetTime();
}

/**
 * @brief 设置下一窗口位置
 * @param x X 坐标
 * @param y Y 坐标
 */
void imgui_widget::SetImGuiNextWindowPos(float x, float y) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
}

/**
 * @brief 设置下一窗口尺寸
 * @param w 宽度
 * @param h 高度
 */
void imgui_widget::SetImGuiNextWindowSize(float w, float h) {
    ImGui::SetNextWindowSize(ImVec2(w, h));
}

/**
 * @brief 设置下一窗口背景透明度
 * @param alpha 透明度 (0.0-1.0)
 */
void imgui_widget::SetImGuiNextWindowBgAlpha(float alpha) {
    ImGui::SetNextWindowBgAlpha(alpha);
}

/**
 * @brief 开始窗口
 * @param name 窗口名称
 * @param open 窗口打开状态指针（可选）
 * @param flags 窗口标志
 * @return true 表示窗口打开成功
 */
bool imgui_widget::ImGuiBegin(const char* name, bool* open, int flags) {
    return ImGui::Begin(name, open, flags);
}

/**
 * @brief 结束窗口
 */
void imgui_widget::ImGuiEnd() {
    ImGui::End();
}

/**
 * @brief 推入项目宽度
 * @param width 宽度值
 */
void imgui_widget::ImGuiPushItemWidth(float width) {
    ImGui::PushItemWidth(width);  // 设置下一项的宽度
}

/**
 * @brief 弹出项目宽度
 */
void imgui_widget::ImGuiPopItemWidth() {
    ImGui::PopItemWidth();
}

/**
 * @brief 在同一行渲染下一项
 */
void imgui_widget::ImGuiSameLine() {
    ImGui::SameLine();
}

/**
 * @brief 设置光标位置
 * @param x X 坐标
 * @param y Y 坐标
 */
void imgui_widget::ImGuiSetCursorPos(float x, float y) {
    ImGui::SetCursorPos(ImVec2(x, y));
}

void imgui_widget::ImGuiSetCursorScreenPos(float x, float y) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
}

/**
 * @brief 推入文本换行位置
 * @param wrap_width 换行宽度
 */
void imgui_widget::ImGuiPushTextWrapPos(float wrap_width) {
    ImGui::PushTextWrapPos(wrap_width);
}

/**
 * @brief 弹出文本换行位置
 */
void imgui_widget::ImGuiPopTextWrapPos() {
    ImGui::PopTextWrapPos();
}

/**
 * @brief 推入项目间距样式
 * @param x 水平间距
 * @param y 垂直间距
 */
void imgui_widget::ImGuiPushStyleVar_ItemSpacing(float x, float y) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(x, y));
}

void imgui_widget::ImGuiPushStyleVar_WindowPadding(float x, float y) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(x, y));
}

void imgui_widget::ImGuiPushStyleVar_FramePadding(float x, float y) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(x, y));
}

void imgui_widget::ImGuiPushStyleVar_ItemInnerSpacing(float x, float y) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(x, y));
}

/**
 * @brief 弹出样式变量
 * @param count 弹出数量
 */
void imgui_widget::ImGuiPopStyleVar(int count) {
    ImGui::PopStyleVar(count);
}

bool imgui_widget::ImGuiInvisibleButton(const char* id, float w, float h) {
    return ImGui::InvisibleButton(id, ImVec2(w, h));
}

void imgui_widget::DrawFilledRoundRect(float x, float y, float w, float h,
                                        float radius, uint32_t color_rgba) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), color_rgba, radius);
}

void imgui_widget::DrawRoundRectOutline(float x, float y, float w, float h,
                                         float radius, uint32_t color_rgba,
                                         float thickness) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color_rgba, radius, 0, thickness);
}

/**
 * @brief 渲染格式化文本
 * @param fmt 格式字符串
 */
void imgui_widget::ImGuiText(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

/**
 * @brief 渲染未格式化文本（直接输出）
 * @param text 文本内容
 */
void imgui_widget::ImGuiTextUnformatted(const char* text) {
    ImGui::TextUnformatted(text);
}

/**
 * @brief 渲染带自动换行的文本
 * @param text 文本内容
 * @param wrap_width 换行宽度（0 表示窗口右边界）
 * @param color 文本颜色（默认白色）
 */
void imgui_widget::ImGuiTextWrapped(const char* text, float wrap_width, const Color& color) {
    ImGuiText(text, color, wrap_width);
}

/**
 * @brief 渲染带颜色的文本（统一接口）
 * @param text 文本内容
 * @param color 文本颜色（默认白色）
 * @param wrap_width 换行宽度（0 表示无换行）
 */
void imgui_widget::ImGuiText(const char* text, const Color& color, float wrap_width) {
    if (wrap_width > 0.0f) {
        ImGuiPushTextWrapPos(wrap_width);
    }
    PushStyleColor(ImGuiCol_Text, color);
    if (wrap_width > 0.0f) {
        ImGui::TextWrapped("%s", text);
    } else {
        ImGui::TextUnformatted(text);
    }
    PopStyleColor();
    if (wrap_width > 0.0f) {
        ImGuiPopTextWrapPos();
    }
}

/**
 * @brief 渲染带颜色的文本（自动恢复样式）
 * @param color 文本颜色
 * @param text 文本内容
 */
void imgui_widget::ImGuiTextColored(const Color& color, const char* text) {
    ImGuiText(text, color, 0.0f);  // 委托给统一接口
}

/**
 * @brief 设置滚动位置到当前可见区域
 * @param center_y_ratio 垂直位置比例 (0=顶部，1=底部，0.5=居中)
 */
void imgui_widget::ImGuiSetScrollHereY(float center_y_ratio) {
    ImGui::SetScrollHereY(center_y_ratio);
}

/**
 * @brief 开始子窗口（Child）
 * @param name 子窗口名称
 * @param width 宽度
 * @param height 高度
 * @param child_flags 子窗口标志
 * @return true 表示子窗口打开成功
 */
bool imgui_widget::BeginChild(const char* name, float width, float height, int child_flags) {
    ImVec2 size(width, height);
    return ImGui::BeginChild(name, size, child_flags);
}

/**
 * @brief 开始子窗口（Child）带窗口标志
 * @param name 子窗口名称
 * @param width 宽度
 * @param height 高度
 * @param child_flags 子窗口标志
 * @param window_flags 窗口标志
 * @return true 表示子窗口打开成功
 */
bool imgui_widget::BeginChild(const char* name, float width, float height, int child_flags, int window_flags) {
    ImVec2 size(width, height);
    return ImGui::BeginChild(name, size, child_flags, window_flags);
}

/**
 * @brief 结束子窗口
 */
void imgui_widget::EndChild() {
    ImGui::EndChild();
}

/**
 * @brief 获取光标 Y 位置
 * @return 光标 Y 坐标
 */
float imgui_widget::GetCursorPosY() {
    return ImGui::GetCursorPosY();
}

/**
 * @brief 设置光标 Y 位置
 * @param y Y 坐标
 */
void imgui_widget::SetCursorPosY(float y) {
    ImGui::SetCursorPosY(y);
}

/**
 * @brief 计算文本尺寸
 * @param out_x 输出宽度（可选）
 * @param out_y 输出高度（可选）
 * @param text_begin 文本起始指针
 * @param text_end 文本结束指针（可选）
 * @param hide_text_after_double_hash 是否隐藏##后的文本
 * @param wrap_width 换行宽度
 */
void imgui_widget::CalcTextSize(float* out_x, float* out_y, const char* text_begin,
                                 const char* text_end, bool hide_text_after_double_hash,
                                 float wrap_width) {
    ImVec2 size = ImGui::CalcTextSize(text_begin, text_end, hide_text_after_double_hash, wrap_width);
    if (out_x) *out_x = size.x;
    if (out_y) *out_y = size.y;
}

/**
 * @brief 获取当前滚动位置
 * @return 滚动 Y 坐标
 */
float imgui_widget::GetScrollY() {
    return ImGui::GetScrollY();
}

/**
 * @brief 获取最大滚动位置
 * @return 最大滚动 Y 坐标
 */
float imgui_widget::GetScrollMaxY() {
    return ImGui::GetScrollMaxY();
}

/**
 * @brief 设置滚动位置
 * @param scroll_y 滚动 Y 坐标
 */
void imgui_widget::SetScrollY(float scroll_y) {
    ImGui::SetScrollY(scroll_y);
}

/**
 * @brief 绘制占位符（用于添加间距）
 * @param width 宽度
 * @param height 高度
 */
void imgui_widget::Dummy(float width, float height) {
    ImGui::Dummy(ImVec2(width, height));
}

/**
 * @brief 推送样式颜色
 * @param color_index ImGuiCol 枚举值（如 0=ImGuiCol_Text）
 * @param color 颜色值
 */
void imgui_widget::PushStyleColor(int color_index, const Color& color) {
    ImGui::PushStyleColor((ImGuiCol)color_index, ImVec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f));
}

/**
 * @brief 弹出样式颜色
 * @param count 弹出数量
 */
void imgui_widget::PopStyleColor(int count) {
    ImGui::PopStyleColor(count);
}

/**
 * @brief 推送窗口标题颜色（使用 ImGuiCol_Text，调用后需立即 Begin 窗口）
 * @param color 标题颜色
 */
void imgui_widget::ImGuiPushStyleColor_TitleText(const Color& color) {
    ImVec4 color_vec(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, color_vec);
}

/**
 * @brief 弹出窗口标题颜色
 * @param count 弹出数量（默认 1）
 */
void imgui_widget::ImGuiPopStyleColor_TitleText(int count) {
    ImGui::PopStyleColor(count);
}

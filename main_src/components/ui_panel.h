// ============================================================================
// UI 面板组件 - 可复用的容器组件
// ============================================================================
// 注意：本文件依赖 media/imgui_widget.h 中的 ScrollWindow
// ============================================================================

#pragma once

#include "colors.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "media/imgui_widget.h"

// ============================================================================
// PanelStyle 预设样式
// ============================================================================

namespace aicode {

/// 面板样式配置 - 定义面板的视觉属性
struct PanelStyle {
    Color background_color{20, 20, 20, 200};      // 背景色（RGBA）
    Color border_color{80, 80, 80, 255};          // 边框色（RGBA）
    float border_width = 1.0f;                     // 边框宽度（像素）
    float corner_radius = 0.0f;                    // 圆角半径（暂不支持）
    bool has_border = true;                        // 是否绘制边框
    float padding = 10.0f;                         // 内边距（内容与边框的距离）
    Color header_bg_color{30, 30, 30, 220};       // 标题栏背景色
    bool has_header = false;                       // 是否显示标题栏
    float header_height = 28.0f;                   // 标题栏高度（像素）

    // 预设样式
    static PanelStyle Default();                   // 默认样式
    static PanelStyle InputField();                // 输入框样式（深色背景）
    static PanelStyle StatusBar();                 // 状态栏样式
    static PanelStyle ChatPanel();                 // 聊天面板样式
    static PanelStyle MessageUser();               // 用户消息气泡
    static PanelStyle MessageAgent();              // Agent 消息气泡
    static PanelStyle MessageSystem();             // 系统消息气泡
    static PanelStyle Card();                      // 卡片样式（圆角）
};

/// 浮动文本位置枚举
enum class FloatPosition {
    TopLeft,      // 左上角
    TopRight,     // 右上角
    BottomLeft,   // 左下角
    BottomRight,  // 右下角
    Center        // 居中
};

/// 面板组件 - 管理背景、边框和内容区域
///
/// 核心功能：
///   - 背景渲染（SDL 层）
///   - 边框渲染（SDL 层）
///   - 内容区域计算（考虑内边距和标题栏）
///   - 浮动文本渲染（如标题）
///
/// 生命周期：
///   1. 创建：指定位置、尺寸、样式
///   2. 渲染：每帧调用 Render()
///   3. 销毁：自动析构
class UIPanel {
public:
    /// 构造函数
    /// @param x 面板左上角 X 坐标
    /// @param y 面板左上角 Y 坐标
    /// @param width 面板宽度
    /// @param height 面板高度
    /// @param style 面板样式（默认 Default）
    UIPanel(float x, float y, float width, float height, PanelStyle style = PanelStyle::Default());
    ~UIPanel() = default;

    // --- SDL 渲染方法 ---

    /// 绘制背景（SDL 层调用）
    void RenderBackground() const;

    /// 绘制边框（SDL 层调用）
    void RenderBorder() const;

    /// 绘制完整面板（背景 + 边框）
    void Render() const;

    // --- 几何属性 ---

    /// 获取内容区域 X 坐标（考虑内边距和标题栏）
    float GetContentX() const;

    /// 获取内容区域 Y 坐标（考虑内边距和标题栏）
    float GetContentY() const;

    /// 获取内容区域宽度（考虑内边距）
    float GetContentWidth() const;

    /// 获取内容区域高度（考虑内边距和标题栏）
    float GetContentHeight() const;

    /// 获取面板 X 坐标
    float GetX() const { return x_; }

    /// 获取面板 Y 坐标
    float GetY() const { return y_; }

    /// 获取面板宽度
    float GetWidth() const { return width_; }

    /// 获取面板高度
    float GetHeight() const { return height_; }

    // --- 设置方法 ---

    /// 设置面板位置
    /// @param x 新的 X 坐标
    /// @param y 新的 Y 坐标
    void SetPosition(float x, float y);

    /// 设置面板尺寸
    /// @param width 新的宽度
    /// @param height 新的高度
    void SetSize(float width, float height);

    /// 设置面板样式
    /// @param style 新的样式配置
    void SetStyle(const PanelStyle& style);

    // --- 可见性控制 ---

    /// 设置可见性
    /// @param visible true=可见，false=隐藏
    void SetVisible(bool visible) { visible_ = visible; }

    /// 检查是否可见
    bool IsVisible() const { return visible_; }

    // --- 文本渲染 ---

    /// 绘制浮动文本（如标题）
    /// @param text 文本内容
    /// @param pos 文本位置（默认左上角）
    /// @param offset_x X 方向偏移量
    /// @param offset_y Y 方向偏移量
    void RenderFloatText(const std::string& text, FloatPosition pos = FloatPosition::TopLeft,
                         float offset_x = 10.0f, float offset_y = 8.0f) const;

protected:
    float x_, y_;           ///< 面板位置
    float width_, height_;  ///< 面板尺寸
    PanelStyle style_;      ///< 样式配置
    bool visible_ = true;   ///< 可见性标志
};

// ============================================================================
// 容器面板 - 带 ImGui 内容回调的面板
// ============================================================================
// 用法：
//   1. 创建容器：UIContainer container(x, y, w, h, style);
//   2. SDL 层：container.Render();
//   3. ImGui 层：container.RenderContent("MyContent");
//
// 或使用回调方式：
//   container.SetContentCallback([](float cx, float cy, float cw, float ch) {
//       // 在内容区域内渲染 ImGui 控件
//   });
// ============================================================================
class UIContainer : public UIPanel {
public:
    /// 内容渲染回调类型
    /// @param content_x 内容区域 X 坐标
    /// @param content_y 内容区域 Y 坐标
    /// @param content_width 内容区域宽度
    /// @param content_height 内容区域高度
    using ContentCallback = std::function<void(float content_x, float content_y,
                                                float content_width, float content_height)>;

    /// 构造函数
    UIContainer(float x, float y, float width, float height,
                PanelStyle style = PanelStyle::Default());

    /// 设置内容渲染回调
    /// @param cb 回调函数
    void SetContentCallback(ContentCallback cb);

    /// 渲染 ImGui 内容（在 ImGui 层调用）
    /// @param name 窗口名称（默认 "ContainerContent"）
    void RenderContent(const std::string& name = "ContainerContent");

private:
    ContentCallback content_callback_;  ///< 内容渲染回调
};

// ============================================================================
// 标题栏组件 - 用于面板顶部的标题条
// ============================================================================
// 用法：
//   HeaderBar header(x, y, w, h);
//   header.Render("我的标题");
// ============================================================================
class HeaderBar {
public:
    /// 构造函数
    /// @param x 标题栏 X 坐标
    /// @param y 标题栏 Y 坐标
    /// @param width 标题栏宽度
    /// @param height 标题栏高度
    HeaderBar(float x, float y, float width, float height);

    /// 渲染标题栏
    /// @param title 标题文本
    /// @param bg_color 背景颜色（默认深灰色）
    void Render(const std::string& title, Color bg_color = Color(30, 30, 30, 220)) const;

    /// 设置标题栏位置
    void SetPosition(float x, float y) { x_ = x; y_ = y; }

    /// 设置标题栏尺寸
    void SetSize(float width, float height) { width_ = width; height_ = height; }

private:
    float x_, y_;           ///< 标题栏位置
    float width_, height_;  ///< 标题栏尺寸
};

}  // namespace aicode

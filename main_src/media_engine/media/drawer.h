#pragma once

#include <string>
#include <stdint.h>
#include "colors.h"

// ============================================================================
// 渲染器 - 封装 SDL 绘图渲染功能
// ============================================================================
class Drawer {
public:
    static Drawer& Instance() {
        static Drawer instance;
        return instance;
    }

    // 基础绘制
    void Clear(const Color& color);

    // 矩形绘制（边框）
    void DrawRect(float x, float y, float w, float h, const Color& color);

    // 矩形绘制（填充）
    void DrawFillRect(float x, float y, float w, float h, const Color& color);

    // 矩形绘制（填充 + 边框，一步完成）
    void DrawFilledRectWithBorder(float x, float y, float w, float h,
                                  const Color& fill_color, const Color& border_color);

    // 直线绘制
    void DrawLine(float x1, float y1, float x2, float y2, const Color& color);

    // 圆形绘制（边框）
    void DrawCircle(float x0, float y0, float radius, const Color& color);

    // 圆形绘制（填充）
    void DrawFillCircle(float x0, float y0, float radius, const Color& color);

    // 三角形绘制（边框）
    void DrawTriangle(float x1, float y1, float x2, float y2, float x3, float y3, const Color& color);

    // 三角形绘制（填充）
    void DrawFillTriangle(float x1, float y1, float x2, float y2, float x3, float y3, const Color& color);

    // 圆角矩形绘制（边框）
    void DrawRoundRect(float x, float y, float w, float h, float radius, const Color& color);

    // 圆角矩形绘制（填充）
    void DrawFillRoundRect(float x, float y, float w, float h, float radius, const Color& color);

    // 箭头绘制（边框）
    void DrawArrow(float x1, float y1, float x2, float y2, float arrowWidth, const Color& color);

    // 箭头绘制（填充）
    void DrawFillArrow(float x1, float y1, float x2, float y2, float arrowWidth, const Color& color);

    // 正多边形绘制（边框）
    void DrawRegularPolygon(float centerX, float centerY, int sides, float radius, float rotation,
                            const Color& color);

    // 正多边形绘制（填充）
    void DrawFillRegularPolygon(float centerX, float centerY, int sides, float radius, float rotation,
                                const Color& color);

    // 椭圆绘制（边框）
    void DrawEllipse(float x0, float y0, float radiusX, float radiusY, const Color& color);

    // 椭圆绘制（填充）
    void DrawFillEllipse(float x0, float y0, float radiusX, float radiusY, const Color& color);

    // 像素点绘制
    void DrawPixel(float x, float y, const Color& color);

    // 辅助绘制
    void DrawGameOverOverlay();

private:
    Drawer();
    ~Drawer();
};

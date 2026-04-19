#include "drawer.h"
#include "sdl_common.h"
#include <cmath>


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Drawer::Drawer() = default;

Drawer::~Drawer() = default;

void Drawer::Clear(const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);
    SDL_RenderClear(SdlResource::Instance().GetRender());
}

void Drawer::DrawRect(float x, float y, float w, float h, const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);
    SDL_FRect rect = {x, y, w, h};
    SDL_RenderRect(SdlResource::Instance().GetRender(), &rect);
}

void Drawer::DrawFillRect(float x, float y, float w, float h, const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);
    SDL_FRect rect = {x, y, w, h};
    SDL_RenderFillRect(SdlResource::Instance().GetRender(), &rect);
}

void Drawer::DrawFilledRectWithBorder(float x, float y, float w, float h,
                                      const Color& fill_color, const Color& border_color) {
    DrawFillRect(x, y, w, h, fill_color);
    DrawRect(x, y, w, h, border_color);
}

void Drawer::DrawPixel(float x, float y, const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);
    SDL_RenderPoint(SdlResource::Instance().GetRender(), x, y);
}

void Drawer::DrawLine(float x1, float y1, float x2, float y2, const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);
    SDL_RenderLine(SdlResource::Instance().GetRender(), x1, y1, x2, y2);
}

void Drawer::DrawCircle(float x0, float y0, float radius, const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    const int numSegments = static_cast<int>(radius * 2);
    const float angleStep = 2.0f * M_PI / numSegments;

    float prevX = x0 + radius;
    float prevY = y0;

    for (int i = 1; i <= numSegments; ++i) {
        const float angle = i * angleStep;
        const float x = x0 + radius * std::cos(angle);
        const float y = y0 + radius * std::sin(angle);
        SDL_RenderLine(SdlResource::Instance().GetRender(), prevX, prevY, x, y);
        prevX = x;
        prevY = y;
    }
}

void Drawer::DrawFillCircle(float x0, float y0, float radius, const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    // 使用扫描线算法填充圆形
    const float radiusSquared = radius * radius;

    for (float y = -radius; y <= radius; ++y) {
        const float xOffset = std::sqrt(radiusSquared - y * y);
        const float xStart = x0 - xOffset;
        const float xEnd = x0 + xOffset;
        SDL_RenderLine(SdlResource::Instance().GetRender(), xStart, y0 + y, xEnd, y0 + y);
    }
}

void Drawer::DrawTriangle(float x1, float y1, float x2, float y2, float x3, float y3,
                          const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);
    SDL_RenderLine(SdlResource::Instance().GetRender(), x1, y1, x2, y2);
    SDL_RenderLine(SdlResource::Instance().GetRender(), x2, y2, x3, y3);
    SDL_RenderLine(SdlResource::Instance().GetRender(), x3, y3, x1, y1);
}

void Drawer::DrawFillTriangle(float x1, float y1, float x2, float y2, float x3, float y3,
                              const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    // 按 Y 坐标排序顶点
    float vertices[3][2] = {{x1, y1}, {x2, y2}, {x3, y3}};

    // 简单冒泡排序按 Y 坐标排序
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2 - i; ++j) {
            if (vertices[j][1] > vertices[j + 1][1]) {
                float tempX = vertices[j][0];
                float tempY = vertices[j][1];
                vertices[j][0] = vertices[j + 1][0];
                vertices[j][1] = vertices[j + 1][1];
                vertices[j + 1][0] = tempX;
                vertices[j + 1][1] = tempY;
            }
        }
    }

    const float xMin = vertices[0][0], yMin = vertices[0][1];
    const float xMid = vertices[1][0], yMid = vertices[1][1];
    const float xMax = vertices[2][0], yMax = vertices[2][1];

    // 填充上半部分
    if (yMid != yMin) {
        const float slope1 = (xMid - xMin) / (yMid - yMin);
        const float slope2 = (xMax - xMin) / (yMax - yMin);

        for (float y = yMin; y < yMid; ++y) {
            const float xLeft = xMin + slope1 * (y - yMin);
            const float xRight = xMin + slope2 * (y - yMin);
            SDL_RenderLine(SdlResource::Instance().GetRender(), xLeft, y, xRight, y);
        }
    }

    // 填充下半部分
    if (yMax != yMid) {
        const float slope1 = (xMax - xMid) / (yMax - yMid);
        const float slope2 = (xMax - xMin) / (yMax - yMin);

        for (float y = yMid; y < yMax; ++y) {
            const float xLeft = xMid + slope1 * (y - yMid);
            const float xRight = xMin + slope2 * (y - yMin);
            SDL_RenderLine(SdlResource::Instance().GetRender(), xLeft, y, xRight, y);
        }
    }
}

void Drawer::DrawRoundRect(float x, float y, float w, float h, float radius,
                           const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    // 绘制四条直线
    SDL_RenderLine(SdlResource::Instance().GetRender(), x + radius, y, x + w - radius, y);  // 上
    SDL_RenderLine(SdlResource::Instance().GetRender(), x + w, y + radius, x + w, y + h - radius);  // 右
    SDL_RenderLine(SdlResource::Instance().GetRender(), x + w - radius, y + h, x + radius, y + h);  // 下
    SDL_RenderLine(SdlResource::Instance().GetRender(), x, y + radius, x, y + h - radius);  // 左

    // 绘制四个圆角
    const int numSegments = static_cast<int>(radius);
    const float angleStep = M_PI / 2.0f / numSegments;

    // 左上角
    float prevX = x + radius;
    float prevY = y;
    for (int i = 0; i <= numSegments; ++i) {
        const float angle = M_PI + i * angleStep;
        const float cx = x + radius + radius * std::cos(angle);
        const float cy = y + radius + radius * std::sin(angle);
        SDL_RenderLine(SdlResource::Instance().GetRender(), prevX, prevY, cx, cy);
        prevX = cx;
        prevY = cy;
    }

    // 右上角
    prevX = x + w;
    prevY = y + radius;
    for (int i = 0; i <= numSegments; ++i) {
        const float angle = 1.5f * M_PI + i * angleStep;
        const float cx = x + w - radius + radius * std::cos(angle);
        const float cy = y + radius + radius * std::sin(angle);
        SDL_RenderLine(SdlResource::Instance().GetRender(), prevX, prevY, cx, cy);
        prevX = cx;
        prevY = cy;
    }

    // 右下角
    prevX = x + w - radius;
    prevY = y + h;
    for (int i = 0; i <= numSegments; ++i) {
        const float angle = i * angleStep;
        const float cx = x + w - radius + radius * std::cos(angle);
        const float cy = y + h - radius + radius * std::sin(angle);
        SDL_RenderLine(SdlResource::Instance().GetRender(), prevX, prevY, cx, cy);
        prevX = cx;
        prevY = cy;
    }

    // 左下角
    prevX = x;
    prevY = y + h - radius;
    for (int i = 0; i <= numSegments; ++i) {
        const float angle = 0.5f * M_PI + i * angleStep;
        const float cx = x + radius + radius * std::cos(angle);
        const float cy = y + h - radius + radius * std::sin(angle);
        SDL_RenderLine(SdlResource::Instance().GetRender(), prevX, prevY, cx, cy);
        prevX = cx;
        prevY = cy;
    }
}

void Drawer::DrawFillRoundRect(float x, float y, float w, float h, float radius,
                               const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    // 填充中间矩形区域
    SDL_FRect rect = {x, y + radius, w, h - 2 * radius};
    SDL_RenderFillRect(SdlResource::Instance().GetRender(), &rect);

    // 填充四个四分之一圆
    const int numSegments = static_cast<int>(radius);
    const float angleStep = M_PI / 2.0f / numSegments;

    // 左上角
    for (int i = 0; i < numSegments; ++i) {
        const float angle1 = M_PI + i * angleStep;
        const float angle2 = M_PI + (i + 1) * angleStep;
        const float x1 = x + radius + radius * std::cos(angle1);
        const float y1 = y + radius + radius * std::sin(angle1);
        const float x2 = x + radius + radius * std::cos(angle2);
        const float y2 = y + radius + radius * std::sin(angle2);
        SDL_RenderLine(SdlResource::Instance().GetRender(), x + radius, y + radius, x1, y1);
        SDL_RenderLine(SdlResource::Instance().GetRender(), x + radius, y + radius, x2, y2);
    }

    // 右上角
    for (int i = 0; i < numSegments; ++i) {
        const float angle1 = 1.5f * M_PI + i * angleStep;
        const float angle2 = 1.5f * M_PI + (i + 1) * angleStep;
        const float x1 = x + w - radius + radius * std::cos(angle1);
        const float y1 = y + radius + radius * std::sin(angle1);
        const float x2 = x + w - radius + radius * std::cos(angle2);
        const float y2 = y + radius + radius * std::sin(angle2);
        SDL_RenderLine(SdlResource::Instance().GetRender(), x + w - radius, y + radius, x1, y1);
        SDL_RenderLine(SdlResource::Instance().GetRender(), x + w - radius, y + radius, x2, y2);
    }

    // 右下角
    for (int i = 0; i < numSegments; ++i) {
        const float angle1 = i * angleStep;
        const float angle2 = (i + 1) * angleStep;
        const float x1 = x + w - radius + radius * std::cos(angle1);
        const float y1 = y + h - radius + radius * std::sin(angle1);
        const float x2 = x + w - radius + radius * std::cos(angle2);
        const float y2 = y + h - radius + radius * std::sin(angle2);
        SDL_RenderLine(SdlResource::Instance().GetRender(), x + w - radius, y + h - radius, x1, y1);
        SDL_RenderLine(SdlResource::Instance().GetRender(), x + w - radius, y + h - radius, x2, y2);
    }

    // 左下角
    for (int i = 0; i < numSegments; ++i) {
        const float angle1 = 0.5f * M_PI + i * angleStep;
        const float angle2 = 0.5f * M_PI + (i + 1) * angleStep;
        const float x1 = x + radius + radius * std::cos(angle1);
        const float y1 = y + h - radius + radius * std::sin(angle1);
        const float x2 = x + radius + radius * std::cos(angle2);
        const float y2 = y + h - radius + radius * std::sin(angle2);
        SDL_RenderLine(SdlResource::Instance().GetRender(), x + radius, y + h - radius, x1, y1);
        SDL_RenderLine(SdlResource::Instance().GetRender(), x + radius, y + h - radius, x2, y2);
    }
}

void Drawer::DrawArrow(float x1, float y1, float x2, float y2, float arrowWidth,
                       const Color& color) {
    // 绘制箭头的直线部分
    DrawLine(x1, y1, x2, y2, color);

    // 计算箭头头部
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = std::sqrt(dx * dx + dy * dy);

    if (length == 0) return;

    const float ux = dx / length;
    const float uy = dy / length;

    // 箭头底部的两个点
    const float arrowLength = arrowWidth * 2.0f;
    const float baseX = x2 - ux * arrowLength;
    const float baseY = y2 - uy * arrowLength;

    // 垂直向量
    const float perpX = -uy;
    const float perpY = ux;

    const float leftX = baseX + perpX * arrowWidth;
    const float leftY = baseY + perpY * arrowWidth;
    const float rightX = baseX - perpX * arrowWidth;
    const float rightY = baseY - perpY * arrowWidth;

    // 绘制箭头两侧的线
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);
    SDL_RenderLine(SdlResource::Instance().GetRender(), x2, y2, leftX, leftY);
    SDL_RenderLine(SdlResource::Instance().GetRender(), x2, y2, rightX, rightY);
}

void Drawer::DrawFillArrow(float x1, float y1, float x2, float y2, float arrowWidth,
                           const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    // 计算箭头头部
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = std::sqrt(dx * dx + dy * dy);

    if (length == 0) return;

    const float ux = dx / length;
    const float uy = dy / length;

    // 箭头底部的两个点
    const float arrowLength = arrowWidth * 2.0f;
    const float baseX = x2 - ux * arrowLength;
    const float baseY = y2 - uy * arrowLength;

    // 垂直向量
    const float perpX = -uy;
    const float perpY = ux;

    const float leftX = baseX + perpX * arrowWidth;
    const float leftY = baseY + perpY * arrowWidth;
    const float rightX = baseX - perpX * arrowWidth;
    const float rightY = baseY - perpY * arrowWidth;

    // 填充箭头头部三角形
    DrawFillTriangle(x2, y2, leftX, leftY, rightX, rightY, color);

    // 绘制箭身（较粗的线，用矩形近似）
    const float shaftWidth = arrowWidth * 0.5f;
    const float tailX = x1 + ux * arrowLength;
    const float tailY = y1 + uy * arrowLength;

    // 箭身矩形的四个点
    const float rectLeftX1 = tailX + perpX * shaftWidth;
    const float rectLeftY1 = tailY + perpY * shaftWidth;
    const float rectLeftX2 = baseX + perpX * shaftWidth;
    const float rectLeftY2 = baseY + perpY * shaftWidth;
    const float rectRightX1 = tailX - perpX * shaftWidth;
    const float rectRightY1 = tailY - perpY * shaftWidth;
    const float rectRightX2 = baseX - perpX * shaftWidth;
    const float rectRightY2 = baseY - perpY * shaftWidth;

    // 填充箭身（两个三角形）
    DrawFillTriangle(rectLeftX1, rectLeftY1, rectLeftX2, rectLeftY2, rectRightX2, rectRightY2, color);
    DrawFillTriangle(rectLeftX1, rectLeftY1, rectRightX1, rectRightY1, rectRightX2, rectRightY2, color);
}

void Drawer::DrawRegularPolygon(float centerX, float centerY, int sides, float radius, float rotation,
                                const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    const float angleStep = 2.0f * M_PI / sides;

    float prevX = centerX + radius * std::cos(rotation);
    float prevY = centerY + radius * std::sin(rotation);

    for (int i = 1; i <= sides; ++i) {
        const float angle = rotation + i * angleStep;
        const float x = centerX + radius * std::cos(angle);
        const float y = centerY + radius * std::sin(angle);
        SDL_RenderLine(SdlResource::Instance().GetRender(), prevX, prevY, x, y);
        prevX = x;
        prevY = y;
    }
}

void Drawer::DrawFillRegularPolygon(float centerX, float centerY, int sides, float radius, float rotation,
                                    const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    const float angleStep = 2.0f * M_PI / sides;

    // 使用扇形填充
    float prevX = centerX + radius * std::cos(rotation);
    float prevY = centerY + radius * std::sin(rotation);

    for (int i = 1; i <= sides; ++i) {
        const float angle = rotation + i * angleStep;
        const float x = centerX + radius * std::cos(angle);
        const float y = centerY + radius * std::sin(angle);

        // 填充一个三角形（中心点到边）
        DrawFillTriangle(centerX, centerY, prevX, prevY, x, y, color);

        prevX = x;
        prevY = y;
    }
}

void Drawer::DrawEllipse(float x0, float y0, float radiusX, float radiusY,
                         const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    const int numSegments = static_cast<int>((radiusX + radiusY) * 2);
    const float angleStep = 2.0f * M_PI / numSegments;

    float prevX = x0 + radiusX;
    float prevY = y0;

    for (int i = 1; i <= numSegments; ++i) {
        const float angle = i * angleStep;
        const float x = x0 + radiusX * std::cos(angle);
        const float y = y0 + radiusY * std::sin(angle);
        SDL_RenderLine(SdlResource::Instance().GetRender(), prevX, prevY, x, y);
        prevX = x;
        prevY = y;
    }
}

void Drawer::DrawFillEllipse(float x0, float y0, float radiusX, float radiusY,
                             const Color& color) {
    SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), color.r, color.g, color.b, color.a);

    // 使用扫描线算法填充椭圆
    const float radiusXSquared = radiusX * radiusX;
    const float radiusYSquared = radiusY * radiusY;

    for (float y = -radiusY; y <= radiusY; ++y) {
        const float xOffset = radiusX * std::sqrt(1.0f - (y * y) / radiusYSquared);
        SDL_RenderLine(SdlResource::Instance().GetRender(), x0 - xOffset, y0 + y, x0 + xOffset, y0 + y);
    }
}

void Drawer::DrawGameOverOverlay() {
    // 半透明遮罩
    DrawFillRect(0, 0, 1280, 960, Colors::Overlay);
}

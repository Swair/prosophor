// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#include "scene/pixel_character.h"
#include "media_core.h"
#include "drawer.h"
#include "media/colors.h"
#include "common/log_wrapper.h"

namespace aicode {

// 像素小人尺寸：16x32 像素
constexpr int PIXEL_W = 16;
constexpr int PIXEL_H = 32;

// 渲染倍率：每个像素放大 2 倍显示（32x64 像素）
constexpr float RENDER_SCALE = 2.0f;

PixelCharacterRenderer& PixelCharacterRenderer::Instance() {
    static PixelCharacterRenderer instance;
    return instance;
}

void PixelCharacterRenderer::Initialize() {
    LOG_INFO("PixelCharacterRenderer initialized");
}

// 获取角色颜色配置
static Color GetBodyColor(int palette_index) {
    // 不同角色使用不同颜色的衣服
    const Color colors[] = {
        Color{0, 100, 200, 255},    // palette 0: 蓝色衣服 (coder)
        Color{0, 150, 100, 255},    // palette 1: 绿色衣服 (reviewer)
        Color{200, 100, 0, 255},    // palette 2: 橙色衣服
        Color{150, 0, 150, 255},    // palette 3: 紫色衣服
        Color{200, 50, 50, 255},    // palette 4: 红色衣服
        Color{100, 100, 200, 255},  // palette 5: 淡紫衣服 (architect)
    };
    return colors[palette_index % 6];
}

static Color GetSkinColor() {
    return Color{255, 220, 180, 255};  // 皮肤颜色
}

static Color GetHairColor(int palette_index) {
    // 不同角色使用不同颜色的头发
    const Color colors[] = {
        Color{101, 67, 33, 255},   // palette 0: 棕色头发
        Color{200, 180, 100, 255}, // palette 1: 金色头发
        Color{50, 50, 50, 255},    // palette 2: 黑色头发
        Color{150, 100, 50, 255},  // palette 3: 浅棕头发
        Color{200, 100, 50, 255},  // palette 4: 红棕色头发
        Color{180, 100, 200, 255}, // palette 5: 紫色头发
    };
    return colors[palette_index % 6];
}

// 绘制单个像素（放大后的像素块）
void PixelCharacterRenderer::DrawPixel(float base_x, float base_y, int px, int py, const Color& color) const {
    if (px < 0 || px >= PIXEL_W || py < 0 || py >= PIXEL_H) {
        return;
    }
    float x = base_x + px * RENDER_SCALE;
    float y = base_y + py * RENDER_SCALE;
    ::Drawer::Instance().DrawFillRect(x, y, RENDER_SCALE, RENDER_SCALE, color);
}

// 绘制像素小人（正面视角）
void PixelCharacterRenderer::RenderCharacter(float x, float y, int palette_index) const {
    Color body_color = GetBodyColor(palette_index);
    Color skin_color = GetSkinColor();
    Color hair_color = GetHairColor(palette_index);

    // 坐标系：原点 (0,0) 在左上角
    // y=0~7: 头部，y=8~15: 上半身，y=16~23: 下半身，y=24~31: 腿部

    // ===== 头部 (y=0~7) =====
    // 头发顶部 (y=0~2)
    for (int px = 4; px <= 11; px++) {
        DrawPixel(x, y, px, 0, hair_color);
        DrawPixel(x, y, px, 1, hair_color);
        DrawPixel(x, y, px, 2, hair_color);
    }
    // 头发两侧 (y=1~4)
    for (int py = 1; py <= 4; py++) {
        DrawPixel(x, y, 3, py, hair_color);  // 左侧
        DrawPixel(x, y, 12, py, hair_color); // 右侧
    }
    // 脸部 (y=2~7, px=4~11)
    for (int py = 2; py <= 7; py++) {
        for (int px = 4; px <= 11; px++) {
            // 眼睛行 (y=4)
            if (py == 4 && (px == 6 || px == 9)) {
                DrawPixel(x, y, px, py, Color{0, 0, 0, 255});  // 眼睛
                continue;
            }
            // 嘴巴 (y=6)
            if (py == 6 && px >= 6 && px <= 9) {
                DrawPixel(x, y, px, py, Color{200, 100, 100, 255});  // 嘴巴（红色）
                continue;
            }
            DrawPixel(x, y, px, py, skin_color);
        }
    }

    // ===== 上半身 (y=8~15) =====
    // 身体主体 (px=4~11)
    for (int py = 8; py <= 15; py++) {
        for (int px = 4; px <= 11; px++) {
            DrawPixel(x, y, px, py, body_color);
        }
    }
    // 手臂 - 左 (px=2~3) 和 右 (px=12~13)
    for (int py = 8; py <= 13; py++) {
        // 左手
        DrawPixel(x, y, 2, py, body_color);
        DrawPixel(x, y, 3, py, py < 10 ? hair_color : body_color);  // 袖口
        // 右手
        DrawPixel(x, y, 12, py, py < 10 ? hair_color : body_color);  // 袖口
        DrawPixel(x, y, 13, py, body_color);
    }

    // ===== 下半身 (y=16~23) =====
    // 腿部分开绘制（中间有缝隙）
    for (int py = 16; py <= 23; py++) {
        // 左腿 (px=4~7)
        for (int px = 4; px <= 7; px++) {
            DrawPixel(x, y, px, py, body_color);
        }
        // 右腿 (px=8~11)
        for (int px = 8; px <= 11; px++) {
            DrawPixel(x, y, px, py, body_color);
        }
    }

    // ===== 腿部/鞋子 (y=24~31) =====
    for (int py = 24; py <= 31; py++) {
        // 左腿 (px=4~7)
        for (int px = 4; px <= 7; px++) {
            Color color = (py >= 28) ? Color{50, 50, 50, 255} : body_color;  // 鞋子（黑色）
            DrawPixel(x, y, px, py, color);
        }
        // 右腿 (px=8~11)
        for (int px = 8; px <= 11; px++) {
            Color color = (py >= 28) ? Color{50, 50, 50, 255} : body_color;  // 鞋子（黑色）
            DrawPixel(x, y, px, py, color);
        }
    }
}

// 根据方向渲染角色
void PixelCharacterRenderer::Render(const CharacterInstance& ch, float x, float y, float /*scale*/) {
    float render_x = x;
    float render_y = y;

    // 根据方向调整渲染（目前只支持正面和背面）
    if (ch.direction == CharDirection::UP) {
        // 背面视角：头发在后面
        RenderCharacterBack(render_x, render_y, ch.def.palette_index);
    } else if (ch.direction == CharDirection::LEFT || ch.direction == CharDirection::RIGHT) {
        // 侧面视角
        RenderCharacterSide(render_x, render_y, ch.def.palette_index, ch.direction);
    } else {
        // DOWN - 正面视角
        RenderCharacter(render_x, render_y, ch.def.palette_index);
    }
}

// 绘制背面视角
void PixelCharacterRenderer::RenderCharacterBack(float x, float y, int palette_index) const {
    Color body_color = GetBodyColor(palette_index);
    Color hair_color = GetHairColor(palette_index);

    // 背面主要看到头发和身体背面

    // ===== 头部背面 (y=0~7) =====
    // 头发覆盖整个头部背面
    for (int py = 0; py <= 7; py++) {
        for (int px = 3; px <= 12; px++) {
            DrawPixel(x, y, px, py, hair_color);
        }
    }

    // ===== 上半身 (y=8~15) =====
    for (int py = 8; py <= 15; py++) {
        for (int px = 4; px <= 11; px++) {
            DrawPixel(x, y, px, py, body_color);
        }
    }
    // 手臂
    for (int py = 8; py <= 13; py++) {
        DrawPixel(x, y, 2, py, body_color);
        DrawPixel(x, y, 3, py, body_color);
        DrawPixel(x, y, 12, py, body_color);
        DrawPixel(x, y, 13, py, body_color);
    }

    // ===== 下半身 (y=16~23) =====
    for (int py = 16; py <= 23; py++) {
        for (int px = 4; px <= 7; px++) {
            DrawPixel(x, y, px, py, body_color);
        }
        for (int px = 8; px <= 11; px++) {
            DrawPixel(x, y, px, py, body_color);
        }
    }

    // ===== 腿部/鞋子 (y=24~31) =====
    for (int py = 24; py <= 31; py++) {
        for (int px = 4; px <= 7; px++) {
            Color color = (py >= 28) ? Color{50, 50, 50, 255} : body_color;
            DrawPixel(x, y, px, py, color);
        }
        for (int px = 8; px <= 11; px++) {
            Color color = (py >= 28) ? Color{50, 50, 50, 255} : body_color;
            DrawPixel(x, y, px, py, color);
        }
    }
}

// 绘制侧面视角
void PixelCharacterRenderer::RenderCharacterSide(float x, float y, int palette_index, CharDirection dir) const {
    Color body_color = GetBodyColor(palette_index);
    Color skin_color = GetSkinColor();
    Color hair_color = GetHairColor(palette_index);

    // 侧面视角：只绘制一半宽度，居中显示
    int offset_x = (dir == CharDirection::LEFT) ? 4 : 0;

    // ===== 头部 (y=0~7) =====
    for (int py = 0; py <= 2; py++) {
        for (int px = 4; px <= 8; px++) {
            DrawPixel(x, y, px + offset_x, py, hair_color);
        }
    }
    // 脸部
    for (int py = 2; py <= 7; py++) {
        for (int px = 5; px <= 9; px++) {
            if (py == 4 && px == 7) {
                DrawPixel(x, y, px + offset_x, py, Color{0, 0, 0, 255});  // 眼睛
                continue;
            }
            DrawPixel(x, y, px + offset_x, py, skin_color);
        }
    }

    // ===== 上半身 (y=8~15) =====
    for (int py = 8; py <= 15; py++) {
        for (int px = 5; px <= 9; px++) {
            DrawPixel(x, y, px + offset_x, py, body_color);
        }
    }
    // 手臂（侧面看到一只手臂）
    for (int py = 8; py <= 13; py++) {
        DrawPixel(x, y, 3 + offset_x, py, body_color);
        DrawPixel(x, y, 10 + offset_x, py, body_color);
    }

    // ===== 下半身 (y=16~23) =====
    for (int py = 16; py <= 23; py++) {
        for (int px = 5; px <= 9; px++) {
            DrawPixel(x, y, px + offset_x, py, body_color);
        }
    }

    // ===== 腿部/鞋子 (y=24~31) =====
    for (int py = 24; py <= 31; py++) {
        for (int px = 5; px <= 9; px++) {
            Color color = (py >= 28) ? Color{50, 50, 50, 255} : body_color;
            DrawPixel(x, y, px + offset_x, py, color);
        }
    }
}

}  // namespace aicode

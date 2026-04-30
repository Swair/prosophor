// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "scene/anime_character.h"
#include "media_engine/media_engine.h"

namespace prosophor {

AnimeCharacterRenderer& AnimeCharacterRenderer::Instance() {
    static AnimeCharacterRenderer instance;
    return instance;
}

void AnimeCharacterRenderer::Initialize() {}

void AnimeCharacterRenderer::Render(AnimeCharacterType type, float cx, float cy,
                                     AgentRuntimeState state, const Color& scarf_color,
                                     float scale, float alpha, bool is_blinking) {
    uint8_t a = static_cast<uint8_t>(alpha * 255);

    DrawLegs(type, cx, cy - 70 * scale, scale, a);
    DrawBody(type, cx, cy - 15 * scale, scale, state, scarf_color, a);
    DrawArms(type, cx, cy - 10 * scale, scale, state, a);
    DrawFace(type, cx, cy - 35 * scale, scale, a);
    DrawHair(type, cx, cy - 40 * scale, scale, a);
    DrawEyes(type, cx, cy - 2 * scale, scale, state, a, is_blinking);
    DrawAccessories(type, cx, cy - 2 * scale, scale, a);
    DrawMouth(type, cx, cy + 14 * scale, scale, state, a);
    DrawBlush(type, cx, cy + 8 * scale, scale, state, a);
}

// ============================================================================
// Hair - 各角色发型
// ============================================================================
void AnimeCharacterRenderer::DrawHair(AnimeCharacterType type, float cx, float cy,
                                       float s, uint8_t a) {
    switch (type) {

    // ---- 老师：紫色长发 + 发箍 + 呆毛 ----
    case AnimeCharacterType::TEACHER: {
        Color hair(90, 40, 120, a);
        ::Drawer::Instance().DrawFillEllipse(cx, cy - 10 * s, 30 * s, 28 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx - 10 * s, cy + 4 * s, 12 * s, 8 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx, cy + 2 * s, 10 * s, 8 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx + 10 * s, cy + 4 * s, 12 * s, 8 * s, hair);

        ::Drawer::Instance().DrawFillTriangle(
            cx - 8 * s, cy + 8 * s, cx - 5 * s, cy + 20 * s, cx - 2 * s, cy + 8 * s, hair);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 2 * s, cy + 8 * s, cx + 2 * s, cy + 22 * s, cx + 5 * s, cy + 8 * s, hair);
        ::Drawer::Instance().DrawFillTriangle(
            cx + 2 * s, cy + 8 * s, cx + 8 * s, cy + 20 * s, cx + 12 * s, cy + 8 * s, hair);

        ::Drawer::Instance().DrawFillEllipse(cx - 28 * s, cy + 30 * s, 12 * s, 40 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx + 28 * s, cy + 30 * s, 12 * s, 40 * s, hair);

        Color ribbon(200, 50, 50, a);
        ::Drawer::Instance().DrawFillEllipse(cx, cy - 22 * s, 28 * s, 4 * s, ribbon);
        ::Drawer::Instance().DrawFillTriangle(
            cx, cy - 22 * s, cx - 12 * s, cy - 30 * s, cx - 12 * s, cy - 14 * s, ribbon);
        ::Drawer::Instance().DrawFillTriangle(
            cx, cy - 22 * s, cx + 12 * s, cy - 30 * s, cx + 12 * s, cy - 14 * s, ribbon);
        ::Drawer::Instance().DrawFillCircle(cx, cy - 22 * s, 3 * s, Color(180, 40, 40, a));

        ::Drawer::Instance().DrawFillTriangle(
            cx - 3 * s, cy - 34 * s, cx + 1 * s, cy - 48 * s, cx + 5 * s, cy - 34 * s, hair);
        break;
    }

    // ---- 学生：金色双马尾 + 短发刘海 ----
    case AnimeCharacterType::STUDENT: {
        Color hair(220, 190, 60, a);
        Color hair_bright(255, 220, 80, a);
        ::Drawer::Instance().DrawFillEllipse(cx, cy - 10 * s, 28 * s, 26 * s, hair);

        // 刘海
        ::Drawer::Instance().DrawFillEllipse(cx - 8 * s, cy + 4 * s, 10 * s, 7 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx, cy + 2 * s, 8 * s, 7 * s, hair_bright);
        ::Drawer::Instance().DrawFillEllipse(cx + 8 * s, cy + 4 * s, 10 * s, 7 * s, hair);

        // 左侧双马尾
        ::Drawer::Instance().DrawFillEllipse(cx - 32 * s, cy + 25 * s, 14 * s, 45 * s, hair);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 38 * s, cy + 65 * s, cx - 32 * s, cy + 80 * s, cx - 26 * s, cy + 65 * s, hair);

        // 右侧双马尾
        ::Drawer::Instance().DrawFillEllipse(cx + 32 * s, cy + 25 * s, 14 * s, 45 * s, hair);
        ::Drawer::Instance().DrawFillTriangle(
            cx + 26 * s, cy + 65 * s, cx + 32 * s, cy + 80 * s, cx + 38 * s, cy + 65 * s, hair);

        // 发圈（红色）
        Color hair_tie(220, 60, 60, a);
        ::Drawer::Instance().DrawFillCircle(cx - 30 * s, cy + 10 * s, 4 * s, hair_tie);
        ::Drawer::Instance().DrawFillCircle(cx + 30 * s, cy + 10 * s, 4 * s, hair_tie);
        break;
    }

    // ---- AI助手：银短发 + 几何感 ----
    case AnimeCharacterType::AI_ASSISTANT: {
        Color hair(200, 210, 230, a);
        Color hair_glow(160, 180, 255, a);
        ::Drawer::Instance().DrawFillEllipse(cx, cy - 10 * s, 26 * s, 24 * s, hair);

        // 齐刘海（平整切线）
        ::Drawer::Instance().DrawFillEllipse(cx - 8 * s, cy + 2 * s, 10 * s, 6 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx, cy, 8 * s, 6 * s, hair_glow);
        ::Drawer::Instance().DrawFillEllipse(cx + 8 * s, cy + 2 * s, 10 * s, 6 * s, hair);

        // 侧发（短发齐耳）
        ::Drawer::Instance().DrawFillEllipse(cx - 22 * s, cy + 20 * s, 8 * s, 22 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx + 22 * s, cy + 20 * s, 8 * s, 22 * s, hair);

        // 头顶几何发光纹
        Color glow(100, 150, 255, static_cast<uint8_t>(a * 0.6f));
        ::Drawer::Instance().DrawFillTriangle(
            cx - 6 * s, cy - 30 * s, cx, cy - 42 * s, cx + 6 * s, cy - 30 * s, glow);
        ::Drawer::Instance().DrawFillCircle(cx, cy - 36 * s, 2 * s, Color(200, 220, 255, a));
        break;
    }

    // ---- 魔法少女：粉色双马尾 + 星星发饰 ----
    case AnimeCharacterType::MAGICAL_GIRL: {
        Color hair(255, 150, 200, a);
        Color hair_light(255, 180, 220, a);
        ::Drawer::Instance().DrawFillEllipse(cx, cy - 10 * s, 30 * s, 28 * s, hair);

        // 弧形刘海
        ::Drawer::Instance().DrawFillEllipse(cx - 10 * s, cy + 4 * s, 12 * s, 8 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx, cy + 2 * s, 10 * s, 8 * s, hair_light);
        ::Drawer::Instance().DrawFillEllipse(cx + 10 * s, cy + 4 * s, 12 * s, 8 * s, hair);

        // 超长双马尾
        ::Drawer::Instance().DrawFillEllipse(cx - 34 * s, cy + 20 * s, 16 * s, 55 * s, hair);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 42 * s, cy + 70 * s, cx - 34 * s, cy + 90 * s, cx - 26 * s, cy + 70 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx + 34 * s, cy + 20 * s, 16 * s, 55 * s, hair);
        ::Drawer::Instance().DrawFillTriangle(
            cx + 26 * s, cy + 70 * s, cx + 34 * s, cy + 90 * s, cx + 42 * s, cy + 70 * s, hair);

        // 星星发饰
        Color star(255, 220, 50, a);
        // 左星
        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 30 * s, cx - 14 * s, cy - 24 * s, cx - 22 * s, cy - 24 * s, star);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 16 * s, cy - 28 * s, cx - 12 * s, cy - 26 * s, cx - 20 * s, cy - 26 * s, star);
        // 右星
        ::Drawer::Instance().DrawFillTriangle(
            cx + 18 * s, cy - 30 * s, cx + 14 * s, cy - 24 * s, cx + 22 * s, cy - 24 * s, star);
        ::Drawer::Instance().DrawFillTriangle(
            cx + 16 * s, cy - 28 * s, cx + 12 * s, cy - 26 * s, cx + 20 * s, cy - 26 * s, star);

        // 呆毛（爱心形）
        ::Drawer::Instance().DrawFillTriangle(
            cx - 4 * s, cy - 34 * s, cx, cy - 46 * s, cx + 4 * s, cy - 34 * s, hair_light);
        break;
    }

    // ---- 冷酷学姐：蓝色短发 + 知性 ----
    case AnimeCharacterType::COOL_SEMPAI: {
        Color hair(50, 80, 180, a);
        Color hair_dark(30, 60, 150, a);
        ::Drawer::Instance().DrawFillEllipse(cx, cy - 10 * s, 26 * s, 24 * s, hair);

        // 斜分刘海
        ::Drawer::Instance().DrawFillEllipse(cx - 6 * s, cy + 4 * s, 12 * s, 7 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx + 4 * s, cy + 2 * s, 10 * s, 6 * s, hair_dark);

        // 侧发（短发到耳下）
        ::Drawer::Instance().DrawFillEllipse(cx - 22 * s, cy + 22 * s, 8 * s, 24 * s, hair);
        ::Drawer::Instance().DrawFillEllipse(cx + 22 * s, cy + 22 * s, 8 * s, 24 * s, hair_dark);

        // 脑后短发
        ::Drawer::Instance().DrawFillEllipse(cx, cy + 10 * s, 20 * s, 14 * s, hair);
        break;
    }
    }
}

// ============================================================================
// Face - 各角色脸型
// ============================================================================
void AnimeCharacterRenderer::DrawFace(AnimeCharacterType type, float cx, float cy,
                                       float s, uint8_t a) {
    (void)type;  // 所有角色使用相同脸型
    Color skin(255, 224, 192, a);

    ::Drawer::Instance().DrawFillEllipse(cx, cy - 2 * s, 22 * s, 20 * s, skin);
    ::Drawer::Instance().DrawFillTriangle(
        cx - 16 * s, cy + 10 * s, cx, cy + 22 * s, cx + 16 * s, cy + 10 * s, skin);
}

// ============================================================================
// Eyes - 各角色瞳孔颜色和风格
// ============================================================================
void AnimeCharacterRenderer::DrawEyes(AnimeCharacterType type, float cx, float cy, float s,
                                       AgentRuntimeState state, uint8_t a, bool is_blinking) {
    Color eye_white(255, 255, 255, a);
    Color eye_highlight(255, 255, 255, a);
    float eye_y = cy;
    float eye_spacing = 11 * s;

    // 各角色瞳孔颜色
    Color eye_iris, eye_pupil;
    switch (type) {
        case AnimeCharacterType::TEACHER:
            eye_iris = Color(80, 30, 130, a);   // 紫色
            eye_pupil = Color(20, 10, 40, a);
            break;
        case AnimeCharacterType::STUDENT:
            eye_iris = Color(50, 180, 255, a);   // 亮蓝色
            eye_pupil = Color(10, 40, 80, a);
            break;
        case AnimeCharacterType::AI_ASSISTANT:
            eye_iris = Color(0, 200, 255, a);    // 荧光蓝
            eye_pupil = Color(0, 100, 150, a);
            break;
        case AnimeCharacterType::MAGICAL_GIRL:
            eye_iris = Color(255, 80, 120, a);   // 玫红色
            eye_pupil = Color(80, 20, 40, a);
            break;
        case AnimeCharacterType::COOL_SEMPAI:
            eye_iris = Color(60, 100, 180, a);   // 冰蓝色
            eye_pupil = Color(20, 30, 60, a);
            break;
    }

    if (is_blinking) {
        for (float ex : {cx - eye_spacing, cx + eye_spacing}) {
            ::Drawer::Instance().DrawFillRect(ex - 6 * s, eye_y - 1 * s, 12 * s, 2 * s,
                                              Color(40, 20, 60, a));
        }
        return;
    }

    // AI助手眼睛额外发光效果
    if (type == AnimeCharacterType::AI_ASSISTANT) {
        Color glow(0, 150, 255, static_cast<uint8_t>(a * 0.25f));
        for (float ex : {cx - eye_spacing, cx + eye_spacing}) {
            ::Drawer::Instance().DrawFillEllipse(ex, eye_y, 12 * s, 14 * s, glow);
        }
    }

    if (state == AgentRuntimeState::COMPLETE) {
        for (float ex : {cx - eye_spacing, cx + eye_spacing}) {
            ::Drawer::Instance().DrawFillEllipse(ex, eye_y, 7 * s, 4 * s, eye_iris);
            ::Drawer::Instance().DrawFillCircle(ex - 2 * s, eye_y - 1 * s, 2 * s, eye_highlight);
        }
    } else if (state == AgentRuntimeState::STATE_ERROR) {
        for (float ex : {cx - eye_spacing, cx + eye_spacing}) {
            ::Drawer::Instance().DrawFillEllipse(ex, eye_y, 9 * s, 10 * s, eye_white);
            ::Drawer::Instance().DrawFillEllipse(ex, eye_y, 5 * s, 6 * s, eye_iris);
            ::Drawer::Instance().DrawFillCircle(ex, eye_y, 2.5 * s, eye_pupil);
            ::Drawer::Instance().DrawFillCircle(ex - 2 * s, eye_y - 3 * s, 1.5 * s, eye_highlight);
        }
    } else if (state == AgentRuntimeState::THINKING) {
        for (float ex : {cx - eye_spacing, cx + eye_spacing}) {
            ::Drawer::Instance().DrawFillEllipse(ex, eye_y, 7 * s, 9 * s, eye_white);
            ::Drawer::Instance().DrawFillEllipse(ex + 2 * s, eye_y - 2 * s, 5 * s, 6 * s, eye_iris);
            ::Drawer::Instance().DrawFillCircle(ex + 2 * s, eye_y - 2 * s, 2.5 * s, eye_pupil);
            ::Drawer::Instance().DrawFillCircle(ex + 1 * s, eye_y - 4 * s, 1.5 * s, eye_highlight);
        }
    } else {
        for (float ex : {cx - eye_spacing, cx + eye_spacing}) {
            ::Drawer::Instance().DrawFillEllipse(ex, eye_y, 7 * s, 9 * s, eye_white);
            ::Drawer::Instance().DrawFillEllipse(ex, eye_y, 5 * s, 7 * s, eye_iris);
            ::Drawer::Instance().DrawFillCircle(ex, eye_y, 2.5 * s, eye_pupil);
            ::Drawer::Instance().DrawFillCircle(ex - 2 * s, eye_y - 3 * s, 1.5 * s, eye_highlight);
            ::Drawer::Instance().DrawFillCircle(ex + 2 * s, eye_y + 1 * s, 1 * s, eye_highlight);
        }
    }

    // 眉毛
    Color brow;
    switch (type) {
        case AnimeCharacterType::TEACHER: brow = Color(100, 50, 130, a); break;
        case AnimeCharacterType::STUDENT: brow = Color(200, 170, 50, a); break;
        case AnimeCharacterType::AI_ASSISTANT: brow = Color(180, 190, 210, a); break;
        case AnimeCharacterType::MAGICAL_GIRL: brow = Color(255, 130, 180, a); break;
        case AnimeCharacterType::COOL_SEMPAI: brow = Color(40, 70, 160, a); break;
    }
    float brow_y = cy - 13 * s;

    if (state == AgentRuntimeState::STATE_ERROR) {
        ::Drawer::Instance().DrawFillTriangle(
            cx - eye_spacing - 8 * s, brow_y - 2 * s,
            cx - eye_spacing + 6 * s, brow_y + 2 * s,
            cx - eye_spacing + 6 * s, brow_y + 4 * s, brow);
        ::Drawer::Instance().DrawFillTriangle(
            cx + eye_spacing - 6 * s, brow_y + 2 * s,
            cx + eye_spacing + 8 * s, brow_y - 2 * s,
            cx + eye_spacing - 6 * s, brow_y + 4 * s, brow);
    } else {
        ::Drawer::Instance().DrawFillEllipse(cx - eye_spacing, brow_y, 7 * s, 2 * s, brow);
        ::Drawer::Instance().DrawFillEllipse(cx + eye_spacing, brow_y, 7 * s, 2 * s, brow);
    }
}

// ============================================================================
// Accessories - 各角色配饰（眼镜/发饰/耳麦）
// ============================================================================
void AnimeCharacterRenderer::DrawAccessories(AnimeCharacterType type, float cx, float cy,
                                              float s, uint8_t a) {
    switch (type) {
        case AnimeCharacterType::TEACHER: {
            // 圆框眼镜
            Color frame(60, 60, 80, static_cast<uint8_t>(a * 0.8f));
            float eye_spacing = 11 * s;
            ::Drawer::Instance().DrawCircle(cx - eye_spacing, cy, 10 * s, frame);
            ::Drawer::Instance().DrawCircle(cx + eye_spacing, cy, 10 * s, frame);
            ::Drawer::Instance().DrawLine(cx - eye_spacing + 8 * s, cy - 2 * s,
                                           cx + eye_spacing - 8 * s, cy - 2 * s, frame);
            ::Drawer::Instance().DrawLine(cx - eye_spacing - 10 * s, cy,
                                           cx - eye_spacing - 20 * s, cy - 3 * s, frame);
            ::Drawer::Instance().DrawLine(cx + eye_spacing + 10 * s, cy,
                                           cx + eye_spacing + 20 * s, cy - 3 * s, frame);
            Color shine(200, 220, 255, static_cast<uint8_t>(a * 0.15f));
            ::Drawer::Instance().DrawFillEllipse(cx - eye_spacing - 3 * s, cy - 4 * s,
                                                  4 * s, 2 * s, shine);
            ::Drawer::Instance().DrawFillEllipse(cx + eye_spacing - 3 * s, cy - 4 * s,
                                                  4 * s, 2 * s, shine);
            break;
        }

        case AnimeCharacterType::STUDENT:
            // 无额外配饰
            break;

        case AnimeCharacterType::AI_ASSISTANT: {
            // 耳麦 / 数据接口
            Color tech(0, 180, 255, static_cast<uint8_t>(a * 0.7f));
            // 左耳接口
            ::Drawer::Instance().DrawFillCircle(cx - 24 * s, cy, 4 * s, tech);
            ::Drawer::Instance().DrawCircle(cx - 24 * s, cy, 4 * s, Color(100, 200, 255, a));
            // 右耳接口
            ::Drawer::Instance().DrawFillCircle(cx + 24 * s, cy, 4 * s, tech);
            ::Drawer::Instance().DrawCircle(cx + 24 * s, cy, 4 * s, Color(100, 200, 255, a));
            // 连接线
            ::Drawer::Instance().DrawLine(cx - 20 * s, cy - 2 * s, cx + 20 * s, cy - 2 * s, tech);
            break;
        }

        case AnimeCharacterType::MAGICAL_GIRL:
            // 无额外面部配饰（发饰在hair中已绘制）
            break;

        case AnimeCharacterType::COOL_SEMPAI: {
            // 细框眼镜（无镜片）- 用四条线模拟矩形
            Color frame(80, 80, 100, static_cast<uint8_t>(a * 0.6f));
            float eye_spacing = 11 * s;
            float lx = cx - eye_spacing - 8 * s, ly = cy - 8 * s, lw = 16 * s, lh = 16 * s;
            ::Drawer::Instance().DrawLine(lx, ly, lx + lw, ly, frame);
            ::Drawer::Instance().DrawLine(lx, ly + lh, lx + lw, ly + lh, frame);
            ::Drawer::Instance().DrawLine(lx, ly, lx, ly + lh, frame);
            ::Drawer::Instance().DrawLine(lx + lw, ly, lx + lw, ly + lh, frame);
            float rx = cx + eye_spacing - 8 * s;
            ::Drawer::Instance().DrawLine(rx, ly, rx + lw, ly, frame);
            ::Drawer::Instance().DrawLine(rx, ly + lh, rx + lw, ly + lh, frame);
            ::Drawer::Instance().DrawLine(rx, ly, rx, ly + lh, frame);
            ::Drawer::Instance().DrawLine(rx + lw, ly, rx + lw, ly + lh, frame);
            ::Drawer::Instance().DrawLine(cx - eye_spacing + 8 * s, cy,
                                           cx + eye_spacing - 8 * s, cy, frame);
            break;
        }
    }
}

// ============================================================================
// Mouth - 根据状态变化
// ============================================================================
void AnimeCharacterRenderer::DrawMouth(AnimeCharacterType type, float cx, float cy, float s,
                                        AgentRuntimeState state, uint8_t a) {
    (void)type;
    Color mouth(200, 100, 100, a);

    if (state == AgentRuntimeState::COMPLETE) {
        ::Drawer::Instance().DrawFillEllipse(cx, cy, 7 * s, 4 * s, mouth);
    } else if (state == AgentRuntimeState::STREAM_CONTENT_TYPING) {
        ::Drawer::Instance().DrawFillEllipse(cx, cy, 3 * s, 5 * s, mouth);
    } else if (state == AgentRuntimeState::STATE_ERROR) {
        ::Drawer::Instance().DrawFillTriangle(
            cx - 5 * s, cy, cx, cy + 3 * s, cx + 5 * s, cy, mouth);
    } else if (state == AgentRuntimeState::THINKING) {
        ::Drawer::Instance().DrawFillRect(cx - 4 * s, cy - 1 * s, 8 * s, 2 * s, mouth);
    } else {
        ::Drawer::Instance().DrawFillTriangle(
            cx - 3 * s, cy, cx, cy + 2 * s, cx + 3 * s, cy, mouth);
    }
}

// ============================================================================
// Blush - 红晕
// ============================================================================
void AnimeCharacterRenderer::DrawBlush(AnimeCharacterType type, float cx, float cy, float s,
                                        AgentRuntimeState state, uint8_t a) {
    (void)type;
    if (state == AgentRuntimeState::COMPLETE || state == AgentRuntimeState::THINKING) {
        Color blush(255, 150, 150, static_cast<uint8_t>(a * 0.3f));
        ::Drawer::Instance().DrawFillEllipse(cx - 16 * s, cy, 6 * s, 3 * s, blush);
        ::Drawer::Instance().DrawFillEllipse(cx + 16 * s, cy, 6 * s, 3 * s, blush);
    }
}

// ============================================================================
// Body - 各角色服装
// ============================================================================
void AnimeCharacterRenderer::DrawBody(AnimeCharacterType type, float cx, float cy, float s,
                                       AgentRuntimeState /*state*/,
                                       const Color& scarf_color, uint8_t a) {
    switch (type) {

    // ---- 老师：深紫色西装 + 百褶裙 ----
    case AnimeCharacterType::TEACHER: {
        Color suit(50, 30, 70, a);
        Color shirt(250, 250, 255, a);

        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 18 * s, cx + 18 * s, cy - 18 * s, cx + 24 * s, cy + 22 * s, suit);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 18 * s, cx - 24 * s, cy + 22 * s, cx + 24 * s, cy + 22 * s, suit);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 7 * s, cy - 20 * s, cx + 7 * s, cy - 20 * s, cx, cy + 4 * s, shirt);

        ::Drawer::Instance().DrawFillTriangle(
            cx, cy - 16 * s, cx - 10 * s, cy - 20 * s, cx - 10 * s, cy - 12 * s, scarf_color);
        ::Drawer::Instance().DrawFillTriangle(
            cx, cy - 16 * s, cx + 10 * s, cy - 20 * s, cx + 10 * s, cy - 12 * s, scarf_color);
        ::Drawer::Instance().DrawFillCircle(cx, cy - 16 * s, 3 * s, scarf_color);

        Color suit_line(70, 50, 100, a);
        ::Drawer::Instance().DrawLine(cx - 5 * s, cy - 20 * s, cx - 2 * s, cy + 4 * s, suit_line);
        ::Drawer::Instance().DrawLine(cx + 5 * s, cy - 20 * s, cx + 2 * s, cy + 4 * s, suit_line);

        Color skirt(40, 25, 60, a);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 24 * s, cy + 22 * s, cx + 24 * s, cy + 22 * s, cx + 28 * s, cy + 48 * s, skirt);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 24 * s, cy + 22 * s, cx - 28 * s, cy + 48 * s, cx + 28 * s, cy + 48 * s, skirt);

        Color skirt_fold(30, 20, 50, a);
        for (int i = -2; i <= 2; i++) {
            ::Drawer::Instance().DrawFillTriangle(
                cx + i * 8 * s, cy + 22 * s, cx + i * 10 * s, cy + 48 * s,
                cx + (i + 1) * 10 * s, cy + 48 * s, skirt_fold);
        }
        break;
    }

    // ---- 学生：水手服 ----
    case AnimeCharacterType::STUDENT: {
        Color uniform(250, 250, 255, a);
        Color navy(30, 50, 120, a);

        // 上衣（白色）
        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 18 * s, cx + 18 * s, cy - 18 * s, cx + 22 * s, cy + 22 * s, uniform);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 18 * s, cx - 22 * s, cy + 22 * s, cx + 22 * s, cy + 22 * s, uniform);

        // 水手领（蓝色）
        ::Drawer::Instance().DrawFillTriangle(
            cx - 16 * s, cy - 20 * s, cx, cy + 2 * s, cx - 8 * s, cy - 20 * s, navy);
        ::Drawer::Instance().DrawFillTriangle(
            cx + 16 * s, cy - 20 * s, cx, cy + 2 * s, cx + 8 * s, cy - 20 * s, navy);

        // 领巾（红色）
        ::Drawer::Instance().DrawFillTriangle(
            cx, cy - 14 * s, cx - 6 * s, cy - 18 * s, cx - 6 * s, cy - 8 * s, Color(200, 50, 50, a));
        ::Drawer::Instance().DrawFillTriangle(
            cx, cy - 14 * s, cx + 6 * s, cy - 18 * s, cx + 6 * s, cy - 8 * s, Color(200, 50, 50, a));

        // 格子裙
        Color plaid_a(40, 60, 140, a);
        Color plaid_b(60, 80, 160, a);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 22 * s, cy + 22 * s, cx + 22 * s, cy + 22 * s, cx + 26 * s, cy + 48 * s, plaid_a);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 22 * s, cy + 22 * s, cx - 26 * s, cy + 48 * s, cx + 26 * s, cy + 48 * s, plaid_a);

        // 格子纹理
        for (int i = -1; i <= 1; i++) {
            ::Drawer::Instance().DrawFillTriangle(
                cx + i * 6 * s, cy + 22 * s, cx + (i + 1) * 6 * s, cy + 22 * s,
                cx + (i + 1) * 7 * s, cy + 48 * s, plaid_b);
        }
        break;
    }

    // ---- AI助手：科技装 ----
    case AnimeCharacterType::AI_ASSISTANT: {
        Color tech_suit(30, 30, 50, a);
        Color tech_accent(0, 150, 255, a);

        // 贴身科技服
        ::Drawer::Instance().DrawFillTriangle(
            cx - 16 * s, cy - 18 * s, cx + 16 * s, cy - 18 * s, cx + 20 * s, cy + 22 * s, tech_suit);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 16 * s, cy - 18 * s, cx - 20 * s, cy + 22 * s, cx + 20 * s, cy + 22 * s, tech_suit);

        // LED 发光线
        ::Drawer::Instance().DrawLine(cx, cy - 18 * s, cx, cy + 22 * s, tech_accent);
        ::Drawer::Instance().DrawLine(cx - 10 * s, cy - 16 * s, cx - 12 * s, cy + 10 * s, tech_accent);
        ::Drawer::Instance().DrawLine(cx + 10 * s, cy - 16 * s, cx + 12 * s, cy + 10 * s, tech_accent);

        // 胸口发光核心
        Color core(0, 200, 255, static_cast<uint8_t>(a * 0.7f));
        ::Drawer::Instance().DrawFillCircle(cx, cy - 10 * s, 5 * s, core);
        ::Drawer::Instance().DrawCircle(cx, cy - 10 * s, 5 * s, Color(100, 220, 255, a));
        break;
    }

    // ---- 魔法少女：魔法裙 ----
    case AnimeCharacterType::MAGICAL_GIRL: {
        Color dress(255, 180, 200, a);
        Color dress_light(255, 210, 220, a);
        Color dress_trim(255, 100, 150, a);

        // 上身（紧身）
        ::Drawer::Instance().DrawFillTriangle(
            cx - 16 * s, cy - 18 * s, cx + 16 * s, cy - 18 * s, cx + 20 * s, cy + 22 * s, dress);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 16 * s, cy - 18 * s, cx - 20 * s, cy + 22 * s, cx + 20 * s, cy + 22 * s, dress);

        // V 领装饰
        ::Drawer::Instance().DrawFillTriangle(
            cx - 6 * s, cy - 20 * s, cx + 6 * s, cy - 20 * s, cx, cy + 2 * s, dress_light);

        // 领口宝石
        Color gem(255, 50, 100, a);
        ::Drawer::Instance().DrawFillCircle(cx, cy - 16 * s, 4 * s, gem);

        // 大裙摆（蓬松）
        ::Drawer::Instance().DrawFillTriangle(
            cx - 28 * s, cy + 22 * s, cx + 28 * s, cy + 22 * s, cx + 36 * s, cy + 55 * s, dress);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 28 * s, cy + 22 * s, cx - 36 * s, cy + 55 * s, cx + 36 * s, cy + 55 * s, dress);

        // 裙摆花边
        ::Drawer::Instance().DrawFillTriangle(
            cx - 36 * s, cy + 50 * s, cx + 36 * s, cy + 50 * s,
            cx + 38 * s, cy + 58 * s, dress_trim);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 36 * s, cy + 50 * s, cx - 38 * s, cy + 58 * s, cx + 38 * s, cy + 58 * s, dress_trim);

        // 蝴蝶结腰带
        ::Drawer::Instance().DrawFillTriangle(
            cx, cy + 16 * s, cx - 12 * s, cy + 12 * s, cx - 12 * s, cy + 20 * s, Color(255, 120, 180, a));
        ::Drawer::Instance().DrawFillTriangle(
            cx, cy + 16 * s, cx + 12 * s, cy + 12 * s, cx + 12 * s, cy + 20 * s, Color(255, 120, 180, a));
        ::Drawer::Instance().DrawFillCircle(cx, cy + 16 * s, 3 * s, Color(255, 80, 140, a));
        break;
    }

    // ---- 冷酷学姐：针织开衫 ----
    case AnimeCharacterType::COOL_SEMPAI: {
        Color cardigan(100, 130, 180, a);
        Color shirt(240, 240, 250, a);
        Color skirt(40, 50, 80, a);

        // 开衫外套（打开状态）
        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 18 * s, cx - 6 * s, cy - 18 * s, cx - 24 * s, cy + 22 * s, cardigan);
        ::Drawer::Instance().DrawFillTriangle(
            cx + 18 * s, cy - 18 * s, cx + 6 * s, cy - 18 * s, cx + 24 * s, cy + 22 * s, cardigan);

        // 白衬衫（内搭）
        ::Drawer::Instance().DrawFillRect(cx - 6 * s, cy - 20 * s, 12 * s, 42 * s, shirt);

        // 领结
        ::Drawer::Instance().DrawFillTriangle(
            cx, cy - 16 * s, cx - 8 * s, cy - 20 * s, cx - 8 * s, cy - 12 * s, scarf_color);
        ::Drawer::Instance().DrawFillTriangle(
            cx, cy - 16 * s, cx + 8 * s, cy - 20 * s, cx + 8 * s, cy - 12 * s, scarf_color);
        ::Drawer::Instance().DrawFillCircle(cx, cy - 16 * s, 2.5 * s, scarf_color);

        // 长裤/长裙
        ::Drawer::Instance().DrawFillTriangle(
            cx - 22 * s, cy + 22 * s, cx + 22 * s, cy + 22 * s, cx + 24 * s, cy + 48 * s, skirt);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 22 * s, cy + 22 * s, cx - 24 * s, cy + 48 * s, cx + 24 * s, cy + 48 * s, skirt);
        break;
    }
    }
}

// ============================================================================
// Arms - 根据状态变化（通用）
// ============================================================================
void AnimeCharacterRenderer::DrawArms(AnimeCharacterType type, float cx, float cy, float s,
                                       AgentRuntimeState state, uint8_t a) {
    // 各角色手臂颜色
    Color arm_color;
    Color skin(255, 224, 192, a);
    switch (type) {
        case AnimeCharacterType::TEACHER: arm_color = Color(50, 30, 70, a); break;
        case AnimeCharacterType::STUDENT: arm_color = Color(250, 250, 255, a); break;
        case AnimeCharacterType::AI_ASSISTANT: arm_color = Color(30, 30, 50, a); break;
        case AnimeCharacterType::MAGICAL_GIRL: arm_color = Color(255, 180, 200, a); break;
        case AnimeCharacterType::COOL_SEMPAI: arm_color = Color(100, 130, 180, a); break;
    }

    if (state == AgentRuntimeState::THINKING) {
        ::Drawer::Instance().DrawFillTriangle(
            cx + 18 * s, cy - 6 * s, cx + 28 * s, cy - 8 * s, cx + 28 * s, cy + 2 * s, arm_color);
        ::Drawer::Instance().DrawFillEllipse(cx + 28 * s, cy - 4 * s, 5 * s, 5 * s, skin);
        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 6 * s, cx - 24 * s, cy + 18 * s, cx - 18 * s, cy + 20 * s, arm_color);
        ::Drawer::Instance().DrawFillEllipse(cx - 21 * s, cy + 20 * s, 4 * s, 4 * s, skin);
    } else if (state == AgentRuntimeState::COMPLETE) {
        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 6 * s, cx - 26 * s, cy - 8 * s, cx - 26 * s, cy + 2 * s, arm_color);
        ::Drawer::Instance().DrawFillCircle(cx - 26 * s, cy - 2 * s, 4 * s, skin);
        ::Drawer::Instance().DrawFillTriangle(
            cx + 18 * s, cy - 6 * s, cx + 26 * s, cy - 8 * s, cx + 26 * s, cy + 2 * s, arm_color);
        ::Drawer::Instance().DrawFillCircle(cx + 26 * s, cy - 2 * s, 4 * s, skin);
    } else if (state == AgentRuntimeState::EXECUTING_TOOL) {
        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 6 * s, cx - 24 * s, cy + 8 * s, cx - 18 * s, cy + 12 * s, arm_color);
        ::Drawer::Instance().DrawFillEllipse(cx - 21 * s, cy + 12 * s, 4 * s, 4 * s, skin);
        ::Drawer::Instance().DrawFillTriangle(
            cx + 18 * s, cy - 6 * s, cx + 24 * s, cy + 8 * s, cx + 18 * s, cy + 12 * s, arm_color);
        ::Drawer::Instance().DrawFillEllipse(cx + 21 * s, cy + 12 * s, 4 * s, 4 * s, skin);
        if (type == AnimeCharacterType::TEACHER) {
            ::Drawer::Instance().DrawFillRect(cx + 21 * s - 1 * s, cy + 8 * s,
                                               2 * s, 6 * s, Color(255, 255, 255, a));
        }
    } else {
        ::Drawer::Instance().DrawFillTriangle(
            cx - 18 * s, cy - 6 * s, cx - 24 * s, cy + 22 * s, cx - 18 * s, cy + 24 * s, arm_color);
        ::Drawer::Instance().DrawFillEllipse(cx - 21 * s, cy + 24 * s, 4 * s, 4 * s, skin);
        ::Drawer::Instance().DrawFillTriangle(
            cx + 18 * s, cy - 6 * s, cx + 24 * s, cy + 22 * s, cx + 18 * s, cy + 24 * s, arm_color);
        ::Drawer::Instance().DrawFillEllipse(cx + 21 * s, cy + 24 * s, 4 * s, 4 * s, skin);
    }
}

// ============================================================================
// Legs - 各角色腿部
// ============================================================================
void AnimeCharacterRenderer::DrawLegs(AnimeCharacterType type, float cx, float cy, float s, uint8_t a) {
    Color skin(255, 224, 192, a);
    Color shoe(40, 30, 50, a);

    switch (type) {
        case AnimeCharacterType::TEACHER: {
            Color sock(30, 30, 40, a);
            ::Drawer::Instance().DrawFillRect(cx - 13 * s, cy, 7 * s, 22 * s, skin);
            ::Drawer::Instance().DrawFillRect(cx - 13 * s, cy + 20 * s, 7 * s, 8 * s, sock);
            ::Drawer::Instance().DrawFillEllipse(cx - 10 * s, cy + 30 * s, 6 * s, 4 * s, shoe);
            ::Drawer::Instance().DrawFillRect(cx + 6 * s, cy, 7 * s, 22 * s, skin);
            ::Drawer::Instance().DrawFillRect(cx + 6 * s, cy + 20 * s, 7 * s, 8 * s, sock);
            ::Drawer::Instance().DrawFillEllipse(cx + 10 * s, cy + 30 * s, 6 * s, 4 * s, shoe);
            break;
        }
        case AnimeCharacterType::STUDENT: {
            // 白色过膝袜
            Color sock(240, 240, 250, a);
            ::Drawer::Instance().DrawFillRect(cx - 13 * s, cy, 7 * s, 22 * s, skin);
            ::Drawer::Instance().DrawFillRect(cx - 13 * s, cy, 7 * s, 28 * s, sock);
            ::Drawer::Instance().DrawFillEllipse(cx - 10 * s, cy + 30 * s, 6 * s, 4 * s, shoe);
            ::Drawer::Instance().DrawFillRect(cx + 6 * s, cy, 7 * s, 22 * s, skin);
            ::Drawer::Instance().DrawFillRect(cx + 6 * s, cy, 7 * s, 28 * s, sock);
            ::Drawer::Instance().DrawFillEllipse(cx + 10 * s, cy + 30 * s, 6 * s, 4 * s, shoe);
            break;
        }
        case AnimeCharacterType::AI_ASSISTANT: {
            // 机械腿（银色+蓝色发光）
            Color mech(150, 150, 170, a);
            Color glow(0, 180, 255, static_cast<uint8_t>(a * 0.5f));
            ::Drawer::Instance().DrawFillRect(cx - 13 * s, cy, 7 * s, 22 * s, mech);
            ::Drawer::Instance().DrawFillRect(cx - 12 * s, cy + 5 * s, 5 * s, 2 * s, glow);
            ::Drawer::Instance().DrawFillEllipse(cx - 10 * s, cy + 30 * s, 6 * s, 4 * s, shoe);
            ::Drawer::Instance().DrawFillRect(cx + 6 * s, cy, 7 * s, 22 * s, mech);
            ::Drawer::Instance().DrawFillRect(cx + 7 * s, cy + 5 * s, 5 * s, 2 * s, glow);
            ::Drawer::Instance().DrawFillEllipse(cx + 10 * s, cy + 30 * s, 6 * s, 4 * s, shoe);
            break;
        }
        case AnimeCharacterType::MAGICAL_GIRL: {
            // 白色长筒袜 + 红色鞋
            Color sock(250, 250, 255, a);
            Color magic_shoe(200, 50, 80, a);
            ::Drawer::Instance().DrawFillRect(cx - 13 * s, cy, 7 * s, 22 * s, skin);
            ::Drawer::Instance().DrawFillRect(cx - 13 * s, cy, 7 * s, 28 * s, sock);
            ::Drawer::Instance().DrawFillEllipse(cx - 10 * s, cy + 30 * s, 6 * s, 4 * s, magic_shoe);
            ::Drawer::Instance().DrawFillRect(cx + 6 * s, cy, 7 * s, 22 * s, skin);
            ::Drawer::Instance().DrawFillRect(cx + 6 * s, cy, 7 * s, 28 * s, sock);
            ::Drawer::Instance().DrawFillEllipse(cx + 10 * s, cy + 30 * s, 6 * s, 4 * s, magic_shoe);
            break;
        }
        case AnimeCharacterType::COOL_SEMPAI: {
            // 长裤（不开腿）
            Color pants(40, 50, 80, a);
            ::Drawer::Instance().DrawFillRect(cx - 13 * s, cy, 7 * s, 22 * s, skin);
            ::Drawer::Instance().DrawFillRect(cx - 13 * s, cy, 7 * s, 28 * s, pants);
            ::Drawer::Instance().DrawFillEllipse(cx - 10 * s, cy + 30 * s, 6 * s, 4 * s, shoe);
            ::Drawer::Instance().DrawFillRect(cx + 6 * s, cy, 7 * s, 22 * s, skin);
            ::Drawer::Instance().DrawFillRect(cx + 6 * s, cy, 7 * s, 28 * s, pants);
            ::Drawer::Instance().DrawFillEllipse(cx + 10 * s, cy + 30 * s, 6 * s, 4 * s, shoe);
            break;
        }
    }
}

}  // namespace prosophor

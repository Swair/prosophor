// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "media_engine/media_engine.h"
#include "components/ui_types.h"

namespace prosophor {

/// 角色类型
enum class AnimeCharacterType {
    TEACHER,       // 紫色长发老师，眼镜，西装
    STUDENT,       // 金色双马尾学生，水手服
    AI_ASSISTANT,  // 银色短发AI助手，发光眼，科技装
    MAGICAL_GIRL,  // 粉色双马尾魔法少女，魔法裙，星形饰
    COOL_SEMPAI,   // 蓝色短发冷酷学姐，针织开衫
};

/// 二次元角色渲染器 - 使用 Drawer 绘制虚拟人风格角色
class AnimeCharacterRenderer {
public:
    static AnimeCharacterRenderer& Instance();

    void Initialize();

    /// 渲染角色
    /// type: 角色类型
    /// cx, cy: 角色中心坐标（底部脚部位置）
    /// state: AgentRuntimeState，决定表情/装饰
    /// scarf_color: 领结/围巾颜色（随状态变化）
    /// scale: 缩放比例
    /// alpha: 整体透明度 (0-1)
    /// is_blinking: 是否正在眨眼
    void Render(AnimeCharacterType type, float cx, float cy, AgentRuntimeState state,
                const Color& scarf_color, float scale, float alpha, bool is_blinking);

private:
    AnimeCharacterRenderer() = default;

    // 通用绘制方法（type 控制各角色特征）
    void DrawHair(AnimeCharacterType type, float cx, float cy, float s, uint8_t a);
    void DrawFace(AnimeCharacterType type, float cx, float cy, float s, uint8_t a);
    void DrawEyes(AnimeCharacterType type, float cx, float cy, float s,
                  AgentRuntimeState state, uint8_t a, bool is_blinking);
    void DrawAccessories(AnimeCharacterType type, float cx, float cy, float s, uint8_t a);
    void DrawMouth(AnimeCharacterType type, float cx, float cy, float s,
                   AgentRuntimeState state, uint8_t a);
    void DrawBlush(AnimeCharacterType type, float cx, float cy, float s,
                   AgentRuntimeState state, uint8_t a);
    void DrawBody(AnimeCharacterType type, float cx, float cy, float s,
                  AgentRuntimeState state, const Color& scarf_color, uint8_t a);
    void DrawArms(AnimeCharacterType type, float cx, float cy, float s,
                  AgentRuntimeState state, uint8_t a);
    void DrawLegs(AnimeCharacterType type, float cx, float cy, float s, uint8_t a);
};

}  // namespace prosophor

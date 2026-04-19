#pragma once

#include <stdint.h>

// ============================================================================
// 颜色定义
// ============================================================================
struct Color {
    uint8_t r, g, b, a;

    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}
};

// 常用颜色枚举
namespace Colors {
    // 基础色
    constexpr Color Transparent{0, 0, 0, 0};
    constexpr Color Black{0, 0, 0, 255};
    constexpr Color White{255, 255, 255, 255};
    constexpr Color Red{255, 0, 0, 255};
    constexpr Color Green{0, 255, 0, 255};
    constexpr Color Blue{0, 0, 255, 255};
    constexpr Color Yellow{255, 255, 0, 255};
    constexpr Color Cyan{0, 255, 255, 255};
    constexpr Color Magenta{255, 0, 255, 255};
    constexpr Color Gray{128, 128, 128, 255};

    // 灰色系
    constexpr Color DarkGray{64, 64, 64, 255};
    constexpr Color LightGray{192, 192, 192, 255};
    constexpr Color Silver{192, 192, 192, 255};

    // 红色系
    constexpr Color DarkRed{139, 0, 0, 255};
    constexpr Color LightRed{255, 100, 100, 255};
    constexpr Color Crimson{220, 20, 60, 255};

    // 绿色系
    constexpr Color DarkGreen{0, 100, 0, 255};
    constexpr Color LightGreen{144, 238, 144, 255};
    constexpr Color Lime{0, 255, 0, 255};
    constexpr Color Olive{128, 128, 0, 255};

    // 蓝色系
    constexpr Color DarkBlue{0, 0, 139, 255};
    constexpr Color LightBlue{173, 216, 230, 255};
    constexpr Color Navy{0, 0, 128, 255};
    constexpr Color Teal{0, 128, 128, 255};

    // 黄色系
    constexpr Color Orange{255, 165, 0, 255};
    constexpr Color Gold{255, 215, 0, 255};
    constexpr Color Beige{245, 245, 220, 255};

    // 紫色系
    constexpr Color Purple{128, 0, 128, 255};
    constexpr Color Violet{238, 130, 238, 255};
    constexpr Color Indigo{75, 0, 130, 255};

    // 棕色系
    constexpr Color Brown{165, 42, 42, 255};
    constexpr Color Tan{210, 180, 140, 255};

    // 游戏 UI 常用色
    constexpr Color HealthBarBg{50, 50, 50, 255};      // 血条背景
    constexpr Color HealthBar{200, 50, 50, 255};       // 血条红色
    constexpr Color HealthBarBorder{255, 255, 255, 255}; // 血条边框

    constexpr Color MenuHighlight{100, 255, 100, 255}; // 菜单高亮绿色
    constexpr Color MenuNormal{200, 200, 200, 255};   // 菜单普通白色
    constexpr Color MenuTitle{255, 215, 0, 255};      // 菜单标题金色
    constexpr Color MenuSubtitle{180, 180, 200, 255}; // 菜单副标题浅灰色

    constexpr Color TextWhite{255, 255, 255, 255};
    constexpr Color TextGray{200, 200, 200, 255};
    constexpr Color TextDark{100, 100, 100, 255};

    // 背景色
    constexpr Color Background{20, 20, 40, 255};       // 深蓝灰背景
    constexpr Color Overlay{0, 0, 0, 180};             // 半透明遮罩

    // Agent UI 面板颜色
    constexpr Color UiPanelBg{20, 20, 20, 200};        // UI 面板背景（半透明深色）
    constexpr Color UiPanelBorder{80, 80, 80, 255};    // UI 面板边框
    constexpr Color UiStatusBarBg{10, 10, 10, 240};    // 状态栏背景
    constexpr Color UiStatusBarBorder{60, 60, 60, 255}; // 状态栏边框
    constexpr Color UiMessageUserBg{50, 80, 150, 180}; // 用户消息气泡背景
    constexpr Color UiMessageUserBorder{80, 120, 200, 255}; // 用户消息气泡边框
    constexpr Color UiMessageAgentBg{60, 60, 60, 180}; // Agent 消息气泡背景
    constexpr Color UiMessageAgentBorder{100, 100, 100, 255}; // Agent 消息气泡边框

    // 办公室场景颜色
    constexpr Color OfficeFloor{245, 245, 240, 255};  // 办公室地板
    constexpr Color OfficeFloorGrid{220, 220, 215, 80};  // 地板网格线
    constexpr Color OfficeWall{180, 170, 160, 255};   // 办公室墙壁
    constexpr Color OfficeWallBorder{150, 140, 130, 255}; // 办公室墙壁边框
    constexpr Color OfficeDoor{255, 255, 255, 255};   // 门
    constexpr Color OfficeDoorBorder{139, 90, 43, 255}; // 门边框（棕色）
    constexpr Color OfficeDoorKnob{200, 180, 100, 255}; // 门把手
    constexpr Color OfficeWindow{180, 220, 255, 255}; // 窗户玻璃
    constexpr Color OfficeDesk{210, 180, 140, 255};   // 书桌桌面
    constexpr Color OfficeDeskBorder{160, 120, 80, 255}; // 书桌边框
    constexpr Color OfficeComputer{60, 60, 60, 255};  // 电脑显示器
    constexpr Color OfficeComputerBorder{100, 100, 100, 255}; // 电脑边框
    constexpr Color OfficeComputerScreen{80, 120, 200, 255}; // 电脑屏幕
    constexpr Color OfficeKeyboard{230, 230, 230, 255}; // 键盘
    constexpr Color OfficeKeyboardBorder{180, 180, 180, 255}; // 键盘边框
    constexpr Color OfficeMouse{200, 200, 200, 255};  // 鼠标
    constexpr Color OfficeChair{70, 90, 120, 255};    // 椅子
    constexpr Color OfficeChairBorder{50, 70, 90, 255}; // 椅子边框
    constexpr Color OfficeChairLeg{140, 140, 140, 255}; // 椅子腿

    // 人物颜色
    constexpr Color PersonSkin{255, 220, 180, 255};   // 肤色
    constexpr Color PersonHair{60, 40, 20, 255};      // 棕色头发
    constexpr Color PersonClothes{60, 100, 180, 255}; // 蓝色衣服

    // 植物颜色
    constexpr Color PlantPot{180, 130, 80, 255};      // 花盆
    constexpr Color PlantPotBorder{150, 110, 60, 255}; // 花盆边框
    constexpr Color PlantLeaf{60, 160, 60, 255};      // 绿叶
    constexpr Color PlantLeafDark{50, 150, 50, 255};  // 深绿叶
    constexpr Color PlantLeafLight{70, 170, 70, 255}; // 浅绿叶
}

// ============================================================================
// 颜色工具函数
// ============================================================================

// 将 32 位颜色 (ARGB) 转换为 Color
inline Color ColorFromUInt32(unsigned int color) {
    uint8_t a = (color >> 24) & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t r = (color >> 0) & 0xFF;
    return Color(r, g, b, a);
}

// 将 Color 转换为 32 位颜色 (ARGB)
inline unsigned int ColorToUInt32(const Color& color) {
    return (static_cast<unsigned int>(color.a) << 24) |
           (static_cast<unsigned int>(color.b) << 16) |
           (static_cast<unsigned int>(color.g) << 8) |
           (static_cast<unsigned int>(color.r) << 0);
}

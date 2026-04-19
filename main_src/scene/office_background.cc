// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "scene/office_background.h"
#include "scene/layout_config.h"
#include "media_core.h"
#include "drawer.h"
#include "colors.h"
#include "common/log_wrapper.h"

namespace aicode {

OfficeBackground& OfficeBackground::GetInstance() {
    static OfficeBackground instance;
    return instance;
}

void OfficeBackground::Initialize() {
    LOG_INFO("OfficeBackground initializing...");
    // Initialize character manager with tile grid
    tile_cols_ = static_cast<int>(LayoutConfig::GetOfficeWidth(1280) / 100);
    tile_rows_ = static_cast<int>(LayoutConfig::GetOfficeHeight(720) / 100);
    OfficeCharacterManager::Instance().Initialize(tile_cols_, tile_rows_);
    LOG_INFO("OfficeBackground initialized.");
}

void OfficeBackground::Render() {
    // 获取办公室区域（使用 LayoutConfig）
    office_x_ = LayoutConfig::GetOfficeX();
    office_y_ = LayoutConfig::GetOfficeY();
    office_width_ = LayoutConfig::GetOfficeWidth(MediaCore::Instance().GetWindowWidth());
    office_height_ = LayoutConfig::GetOfficeHeight(MediaCore::Instance().GetWindowHeight());

    // 2D 平面俯视办公室
    DrawFloor();
    DrawWalls();
    DrawDoor();
    DrawWindow();
    DrawDesk();
    DrawComputer();
    DrawChair();
    // DrawPerson();  // 已弃用，使用 OfficeCharacterManager
    DrawPlant();

    // 渲染 3 个角色小人
    OfficeCharacterManager::Instance().Render();
}

// 地板 - 浅色方格
void OfficeBackground::DrawFloor() {
    ::Drawer::Instance().DrawFillRect(office_x_, office_y_, office_width_, office_height_, Colors::OfficeFloor);

    // 网格线
    for (int x = 0; x <= office_width_; x += 100) {
        ::Drawer::Instance().DrawLine(office_x_ + x, office_y_, office_x_ + x, office_y_ + office_height_, Colors::OfficeFloorGrid);
    }
    for (int y = 0; y <= office_height_; y += 100) {
        ::Drawer::Instance().DrawLine(office_x_, office_y_ + y, office_x_ + office_width_, office_y_ + y, Colors::OfficeFloorGrid);
    }
}

// 墙壁 - 四周边框
void OfficeBackground::DrawWalls() {
    float wall_thickness = 50.0f;
    // 上墙
    ::Drawer::Instance().DrawFillRect(office_x_, office_y_, office_width_, wall_thickness, Colors::OfficeWall);
    // 下墙
    ::Drawer::Instance().DrawFillRect(office_x_, office_y_ + office_height_ - wall_thickness, office_width_, wall_thickness, Colors::OfficeWall);
    // 左墙
    ::Drawer::Instance().DrawFillRect(office_x_, office_y_, wall_thickness, office_height_, Colors::OfficeWall);
    // 右墙
    ::Drawer::Instance().DrawFillRect(office_x_ + office_width_ - wall_thickness, office_y_, wall_thickness, office_height_, Colors::OfficeWall);
}

// 门
void OfficeBackground::DrawDoor() {
    // 门在下方居中
    float door_width = 100.0f;
    float door_height = 50.0f;
    float door_x = office_x_ + (office_width_ - door_width) / 2;
    float door_y = office_y_ + office_height_ - door_height;

    ::Drawer::Instance().DrawFilledRectWithBorder(door_x, door_y, door_width, door_height, Colors::OfficeDoor, Colors::OfficeDoorBorder);

    // 门把手
    ::Drawer::Instance().DrawFillCircle(door_x + door_width - 20, door_y + door_height / 2, 5, Colors::OfficeDoorKnob);
}

// 窗户
void OfficeBackground::DrawWindow() {
    // 上墙窗户（使用 LayoutConfig 比例）
    float window1_width = office_width_ * 0.14f;
    float window1_x = office_x_ + office_width_ * 0.15f;
    ::Drawer::Instance().DrawFilledRectWithBorder(window1_x, office_y_, window1_width, 50, Colors::OfficeWindow, Colors::OfficeWallBorder);

    // 右墙窗户
    float window2_height = office_height_ * 0.28f;
    float window2_y = office_y_ + office_height_ * 0.2f;
    ::Drawer::Instance().DrawFilledRectWithBorder(office_x_ + office_width_ - 50, window2_y, 50, window2_height, Colors::OfficeWindow, Colors::OfficeWallBorder);
}

// 桌子 - 俯视
void OfficeBackground::DrawDesk() {
    float desk_w = office_width_ * 0.23f;
    float desk_h = office_height_ * 0.22f;
    float desk_x = office_x_ + office_width_ * 0.43f;
    float desk_y = office_y_ + office_height_ * 0.39f;

    // 桌面 - 浅木色
    ::Drawer::Instance().DrawFilledRectWithBorder(desk_x, desk_y, desk_w, desk_h, Colors::OfficeDesk, Colors::OfficeDeskBorder);

    // 桌角装饰
    ::Drawer::Instance().DrawFillRect(desk_x + 15, desk_y + 15, 25, 25, Colors::OfficeDeskBorder);
    ::Drawer::Instance().DrawFillRect(desk_x + desk_w - 40, desk_y + 15, 25, 25, Colors::OfficeDeskBorder);
    ::Drawer::Instance().DrawFillRect(desk_x + 15, desk_y + desk_h - 40, 25, 25, Colors::OfficeDeskBorder);
    ::Drawer::Instance().DrawFillRect(desk_x + desk_w - 40, desk_y + desk_h - 40, 25, 25, Colors::OfficeDeskBorder);
}

// 电脑 - 俯视简化
void OfficeBackground::DrawComputer() {
    float computer_w = office_width_ * 0.08f;
    float computer_h = office_height_ * 0.1f;
    float computer_x = office_x_ + office_width_ * 0.5f;
    float computer_y = office_y_ + office_height_ * 0.42f;

    // 显示器
    ::Drawer::Instance().DrawFilledRectWithBorder(computer_x, computer_y, computer_w, computer_h, Colors::OfficeComputer, Colors::OfficeComputerBorder);
    // 屏幕亮色
    ::Drawer::Instance().DrawFillRect(computer_x + 10, computer_y + 10, computer_w - 20, computer_h - 20, Colors::OfficeComputerScreen);

    // 键盘
    float keyboard_w = office_width_ * 0.09f;
    float keyboard_h = office_height_ * 0.05f;
    ::Drawer::Instance().DrawFilledRectWithBorder(computer_x - 10, computer_y + computer_h + 10, keyboard_w, keyboard_h, Colors::OfficeKeyboard, Colors::OfficeKeyboardBorder);

    // 鼠标
    ::Drawer::Instance().DrawFillCircle(computer_x + computer_w + 20, computer_y + computer_h / 2, 10, Colors::OfficeMouse);
}

// 椅子 - 俯视圆形
void OfficeBackground::DrawChair() {
    float chair_x = office_x_ + office_width_ * 0.45f;
    float chair_y = office_y_ + office_height_ * 0.65f;

    // 椅座
    ::Drawer::Instance().DrawFillCircle(chair_x + 25, chair_y + 25, 28, Colors::OfficeChair);
    ::Drawer::Instance().DrawCircle(chair_x + 25, chair_y + 25, 28, Colors::OfficeChairBorder);

    // 椅腿
    ::Drawer::Instance().DrawFillRect(chair_x + 10, chair_y + 10, 6, 20, Colors::OfficeChairLeg);
    ::Drawer::Instance().DrawFillRect(chair_x + 34, chair_y + 10, 6, 20, Colors::OfficeChairLeg);
    ::Drawer::Instance().DrawFillRect(chair_x + 10, chair_y + 35, 6, 20, Colors::OfficeChairLeg);
    ::Drawer::Instance().DrawFillRect(chair_x + 34, chair_y + 35, 6, 20, Colors::OfficeChairLeg);
}

// 小人 - 简化 2D 俯视
void OfficeBackground::DrawPerson() {
    float person_x = 580;
    float person_y = 470;

    // 头部 (圆形)
    ::Drawer::Instance().DrawFillCircle(person_x + 25, person_y + 25, 18, Colors::PersonSkin);

    // 身体/肩膀
    ::Drawer::Instance().DrawFillCircle(person_x + 25, person_y + 50, 22, Colors::PersonClothes);

    // 头发
    ::Drawer::Instance().DrawFillCircle(person_x + 25, person_y + 20, 16, Colors::PersonHair);
}

// 绿植
void OfficeBackground::DrawPlant() {
    // 左上角盆栽（相对于办公室区域）
    float plant_x = office_x_ + office_width_ * 0.1f;
    float plant_y = office_y_ + office_height_ * 0.1f;

    // 花盆
    ::Drawer::Instance().DrawFillCircle(plant_x, plant_y, 30, Colors::PlantPot);
    ::Drawer::Instance().DrawCircle(plant_x, plant_y, 30, Colors::PlantPotBorder);

    // 植物 (多个叶子)
    ::Drawer::Instance().DrawFillCircle(plant_x, plant_y - 15, 20, Colors::PlantLeaf);
    ::Drawer::Instance().DrawFillCircle(plant_x - 18, plant_y + 8, 18, Colors::PlantLeafDark);
    ::Drawer::Instance().DrawFillCircle(plant_x + 18, plant_y + 8, 18, Colors::PlantLeafDark);
    ::Drawer::Instance().DrawFillCircle(plant_x, plant_y + 20, 15, Colors::PlantLeafLight);
}

}  // namespace aicode

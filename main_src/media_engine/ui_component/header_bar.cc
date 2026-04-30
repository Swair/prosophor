// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ui_component/header_bar.h"
#include "drawer.h"

namespace prosophor {

HeaderBar::HeaderBar(float x, float y, float width, float height)
    : x_(x), y_(y), width_(width), height_(height) {
}

void HeaderBar::Render(const std::string& title, Color bg_color) const {
    ::Drawer::Instance().DrawFilledRectWithBorder(
        x_, y_, width_, height_,
        bg_color, Colors::Transparent);

    for (size_t i = 0; i < title.size() && i < 50; i++) {
        char c = title[i];
        if (c >= 32 && c < 127) {
            ::Drawer::Instance().DrawFillRect(x_ + 10 + i * 12, y_ + 7, 8, 14,
                                              Colors::TextGray);
        }
    }
}

}  // namespace prosophor

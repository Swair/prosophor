#include "font.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "sdl_common.h"

// FontImpl 定义
struct Font::FontImpl {
    std::string file_path_{};
    TTF_Font* font_ = nullptr;
};


Font::Font(const std::string &file_path, int font_size) : impl_(std::make_unique<FontImpl>()) {
    impl_->file_path_ = file_path;
    impl_->font_ = TTF_OpenFont(file_path.c_str(), font_size);
    if (impl_->font_ == nullptr) {
        LOG_ERROR("[Font] Failed to load font: {}", file_path.c_str());
    }
}

Font::~Font() {
    if(impl_->font_ != nullptr) {
        TTF_CloseFont(impl_->font_);
    }
}

bool Font::RenderText(const std::string& text, float x, float y, float r, float g, float b, float a) {
    TTF_Text* temp_text_object = TTF_CreateText(SdlResource::Instance().GetTtfEngine(), impl_->font_, text.data(), 0);
    if (!temp_text_object) {
        LOG_ERROR("[Font] drawUIText 创建临时 TTF_Text 失败：{}", SDL_GetError());
        return false;
    }

    // 先渲染一次黑色文字模拟阴影
    TTF_SetTextColorFloat(temp_text_object, 0.0f, 0.0f, 0.0f, 1.0f);
    if (!TTF_DrawRendererText(temp_text_object, x + 2, y + 2)) {
        LOG_ERROR("[Font] drawUIText 绘制临时 TTF_Text 失败：{}", SDL_GetError());
    }

    // 然后正确绘制
    TTF_SetTextColorFloat(temp_text_object, r, g, b, a);
    if (!TTF_DrawRendererText(temp_text_object, x, y)) {
        LOG_ERROR("[Font] drawUIText 绘制临时 TTF_Text 失败：{}", SDL_GetError());
    }

    TTF_DestroyText(temp_text_object);

    return true;
}

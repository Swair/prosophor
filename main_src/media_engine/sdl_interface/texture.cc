#include "texture.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "sdl_common.h"

// TextureImpl 定义
struct Texture::TextureImpl {
    std::string file_path_{};
    float origin_width_;
    float origin_height_;
    SDL_Texture* texture_{nullptr};
};

Texture::Texture(const std::string& file_path) : impl_(std::make_unique<TextureImpl>()) {
    impl_->file_path_ = file_path;
    impl_->texture_ = IMG_LoadTexture(SdlResource::Instance().GetRender(), file_path.c_str());
    if (impl_->texture_ == nullptr) {
        LOG_ERROR("[Texture] Failed to load image: {}", file_path.c_str());
        impl_->origin_width_ = 0;
        impl_->origin_height_ = 0;
    } else {
        SDL_GetTextureSize(impl_->texture_, &impl_->origin_width_, &impl_->origin_height_);
    }
}


Texture::~Texture() {
    if(impl_->texture_ != nullptr) {
        SDL_DestroyTexture(impl_->texture_);
    }
}

float Texture::GetOriginWidth() const {
    return impl_->origin_width_;
}

float Texture::GetOriginHeight() const {
    return impl_->origin_height_;
}

// 渲染时按照指定的宽高进行拉伸缩放
bool Texture::RenderTexture(float x, float y, float w, float h) const {
    SDL_FRect rect = {x, y, w, h};
    if (SDL_RenderTexture(SdlResource::Instance().GetRender(), impl_->texture_, nullptr, &rect) < 0) {
        LOG_ERROR("[Texture] SDL_RenderTexture 渲染视差纹理失败 {}", SDL_GetError());
        return false;
    }
    return true;
}

// src_rect 为原始素材的裁剪区域，dst_rect 为渲染到屏幕上的矩形区域
bool Texture::RenderTexture(float src_x, float src_y, float src_w, float src_h, float dst_x, float dst_y, float dst_w, float dst_h) const {
    SDL_FRect src_rect = {src_x, src_y, src_w, src_h};
    SDL_FRect dst_rect = {dst_x, dst_y, dst_w, dst_h};
    if (SDL_RenderTexture(SdlResource::Instance().GetRender(), impl_->texture_, &src_rect, &dst_rect) < 0) {
        LOG_ERROR("[Texture] SDL_RenderTexture 渲染视差纹理失败 {}", SDL_GetError());
        return false;
    }
    return true;
}

bool Texture::RenderTexture(float x, float y, float w, float h, float angle) const {
    SDL_FRect rect = {x, y, w, h};
    if (SDL_RenderTextureRotated(SdlResource::Instance().GetRender(), impl_->texture_, nullptr, &rect, angle, NULL, SDL_FLIP_NONE) < 0) {
        LOG_ERROR("[Texture] SDL_RenderTextureRotated 渲染视差纹理失败 {}", SDL_GetError());
        return false;
    }
    return true;
}

// 平铺渲染，起点 (absolute_x_, absolute_y_)，把尺寸为 (width_, height_) 平铺满 (tiled_width, tiled_height)
void Texture::TiledRender(float x, float y, float w, float h, int tiled_width, int tiled_height) const {
    for (int pos_y = y; pos_y < tiled_height; pos_y += h) {
        for (int pos_x = x; pos_x < tiled_width; pos_x += w) {
            RenderTexture(pos_x, pos_y, w, h);
        }
    }
}

// 支持水平/垂直翻转的渲染
bool Texture::RenderTexture(float src_x, float src_y, float src_w, float src_h,
                            float dst_x, float dst_y, float dst_w, float dst_h,
                            bool flip_h, bool flip_v) const {
    SDL_FRect src_rect = {src_x, src_y, src_w, src_h};
    SDL_FRect dst_rect = {dst_x, dst_y, dst_w, dst_h};

    SDL_FlipMode flip = SDL_FLIP_NONE;
    if (flip_h) flip = (SDL_FlipMode)(flip | SDL_FLIP_HORIZONTAL);
    if (flip_v) flip = (SDL_FlipMode)(flip | SDL_FLIP_VERTICAL);

    if (SDL_RenderTextureRotated(SdlResource::Instance().GetRender(), impl_->texture_, &src_rect, &dst_rect, 0.0, NULL, flip) < 0) {
        LOG_ERROR("[Texture] SDL_RenderTextureRotated 渲染翻转纹理失败 {}", SDL_GetError());
        return false;
    }
    return true;
}

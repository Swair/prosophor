#pragma once

#include <string>
#include <memory>

class Texture {
    public:
        explicit Texture(const std::string& file_path);

        ~Texture();
        
        // 后去素材的原始像素大小
        float GetOriginWidth() const;

        float GetOriginHeight() const;

        // 渲染时按照指定的宽高进行拉伸缩放
        bool RenderTexture(float x, float y, float w, float h) const;

        // src_rect为原始素材的裁剪区域，dst_rect为渲染到屏幕上的矩形区域
        bool RenderTexture(float src_x, float src_y, float src_w, float src_h, float dst_x, float dst_y, float dst_w, float dst_h) const;

        // 以指定的角度进行旋转渲染
        bool RenderTexture(float x, float y, float w, float h, float angle) const;

        // 支持水平/垂直翻转的渲染
        bool RenderTexture(float src_x, float src_y, float src_w, float src_h,
                          float dst_x, float dst_y, float dst_w, float dst_h,
                          bool flip_h = false, bool flip_v = false) const;

        // 平铺渲染，起点(absolute_x_, absolute_y_)，把尺寸为(width_, height_)平铺满(tiled_width, tiled_height)
        void TiledRender(float x, float y, float w, float h, int tiled_width, int tiled_height) const;

    private:
        struct TextureImpl;
        std::unique_ptr<TextureImpl> impl_{nullptr};
};


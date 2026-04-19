#pragma once

// #include <SDL3/SDL_main.h> 不要加，加了就会重复 main，报错
#include <SDL3/SDL.h>
#include <string>
#include <memory>
#include "log_wrapper.h"
#include <SDL3/SDL_init.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_image/SDL_image.h>
#include <imgui.h>

// ImGui 前向声明
struct ImGuiContext;
struct ImGuiIO;

class SdlResource {
public:
    static SdlResource& Instance() {
        static SdlResource resource;
        return resource;
    }

    SDL_Renderer* GetRender();
    MIX_Mixer* GetMixer();
    TTF_TextEngine* GetTtfEngine();
    ImGuiContext* GetImGuiContext();
    ImGuiIO* GetImGuiIO();

    void Init(int window_width, int window_height);
    void ImGuiInit();
    void ImGuiShutdown();
    void ImGuiNewFrame();
    void ImGuiRender();
    bool IsImGuiInitialized() const { return imgui_initialized_; }

private:
    SdlResource();
    ~SdlResource();
    void SDLInit();
    void SDLImage();
    void TTFInit();
    void MixInit();

    SDL_Window* window_ = nullptr;
    int window_width_ = 800;
    int window_height_ = 600;
    SDL_Renderer* sdl_renderer_ = nullptr;
    TTF_TextEngine* ttf_engine_ = nullptr;
    MIX_Mixer* mixer_ = nullptr;

    // ImGui 成员
    ImGuiContext* imgui_context_ = nullptr;
    bool imgui_initialized_ = false;
};

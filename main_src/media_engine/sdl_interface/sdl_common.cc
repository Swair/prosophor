#include "sdl_common.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"


ImGuiContext* SdlResource::GetImGuiContext() {
    return imgui_context_;
}

ImGuiIO* SdlResource::GetImGuiIO() {
    if (!imgui_initialized_) return nullptr;
    return &ImGui::GetIO();
}

SDL_Renderer* SdlResource::GetRender() {
    return sdl_renderer_;
}
MIX_Mixer* SdlResource::GetMixer() {
    return mixer_;
}
TTF_TextEngine* SdlResource::GetTtfEngine() {
    return ttf_engine_;
}

void SdlResource::Init(int window_width, int window_height) {
    LOG_INFO("[SdlResource] SdlResource Initializing...\n");
    window_width_ = window_width;
    window_height_ = window_height;
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    SDLInit();
    SDLImage();
    TTFInit();
    MixInit();
    ImGuiInit();
}

SdlResource::SdlResource() {}

SdlResource::~SdlResource() {
    // ImGui 清理
    ImGuiShutdown();

    if(ttf_engine_ != nullptr) {
        TTF_DestroyRendererTextEngine(ttf_engine_);
        ttf_engine_ = nullptr;
    }

    // 退出 TTF
    TTF_Quit();

    if(mixer_ != nullptr) {
        mixer_ = nullptr;
    }

    MIX_Quit();
    if(sdl_renderer_ != nullptr) {
        SDL_DestroyRenderer(sdl_renderer_);
        sdl_renderer_ = nullptr;
    }
    if(window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    // 退出 SDL
    SDL_Quit();
}


void SdlResource::SDLInit() {
    // SDL3 初始化
    if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO)){
        LOG_ERROR("[SdlResource] SDL 初始化失败：{}", SDL_GetError());
    }
}

void SdlResource::SDLImage() {
    // 创建窗口与渲染器
    SDL_CreateWindowAndRenderer("AiCode Assistant", window_width_, window_height_, SDL_WINDOW_RESIZABLE, &window_, &sdl_renderer_);
    if (!window_ || !sdl_renderer_){
        LOG_ERROR("[SdlResource] 创建窗口或渲染器失败：{}", SDL_GetError());
    }

    // 设置窗口图标（如果有图标文件）
    // SDL_SetWindowIcon(window_, SDL_LoadBMP("assets/icon.bmp"));

    // 设置窗口逻辑分辨率
    SDL_SetRenderLogicalPresentation(sdl_renderer_, window_width_, window_height_, SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

void SdlResource::TTFInit() {
    // SDL3_TTF 初始化
    if (!TTF_Init()){
        LOG_ERROR("[SdlResource] SDL_TTF 初始化失败：{}", SDL_GetError());
    }
    ttf_engine_ = TTF_CreateRendererTextEngine(sdl_renderer_);
}

void SdlResource::MixInit() {
    // SDL3_Mixer 初始化
    if (MIX_Init()){
        LOG_ERROR("[SdlResource] MIX_Init 初始化失败：{}", SDL_GetError());
    }

    // init SDL Mixer
    mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (mixer_ == nullptr) {
        LOG_ERROR("[SdlResource] MIX_CreateMixerDevice 初始化失败：{}", SDL_GetError());
    }
}

// ============================================================================
// ImGui 方法实现
// ============================================================================

void SdlResource::ImGuiInit() {
    if (imgui_initialized_) {
        LOG_INFO("[SdlResource] ImGui already initialized");
        return;
    }

    // 创建 ImGui 上下文
    imgui_context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_context_);

    // 初始化 ImGui 样式
    ImGui::StyleColorsDark();

    // 让 UI 背景透明，但保留输入框可见
    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 1.0f;
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;

    // 窗口背景透明
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // 输入框背景可见（用于显示光标）
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.2f, 0.2f, 0.9f);  // 输入框背景
    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.3f, 0.5f, 0.8f, 0.5f);

    // 其他 UI 元素颜色
    style.Colors[ImGuiCol_Border] = ImVec4(0.5f, 0.5f, 0.5f, 0.5f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.5f, 0.8f, 0.8f);  // 按钮背景
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.4f, 0.6f, 0.9f, 0.9f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.2f, 0.4f, 0.7f, 1.0f);

    // 加载中文字体
    ImGuiIO& io = ImGui::GetIO();

    const char* font_paths[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/simhei.ttf",
    };

    bool font_loaded = false;
    for (const auto& path : font_paths) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        if (font) {
            font_loaded = true;
            LOG_INFO("[SdlResource] Loaded Chinese font: {}", path);
            break;
        }
    }

    if (!font_loaded) {
        io.Fonts->AddFontDefault();
        LOG_INFO("[SdlResource] Using default ImGui font");
    }

    // 初始化 ImGui 后端
    bool init_success = true;

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, sdl_renderer_)) {
        LOG_ERROR("[SdlResource] Failed to initialize ImGui SDL3 backend");
        init_success = false;
    }

    if (!ImGui_ImplSDLRenderer3_Init(sdl_renderer_)) {
        LOG_ERROR("[SdlResource] Failed to initialize ImGui SDLRenderer3 backend");
        init_success = false;
    }

    if (init_success) {
        imgui_initialized_ = true;
        LOG_INFO("[SdlResource] ImGui initialized successfully");
    } else {
        ImGuiShutdown();
    }
}

void SdlResource::ImGuiShutdown() {
    if (!imgui_initialized_) return;

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(imgui_context_);

    imgui_context_ = nullptr;
    imgui_initialized_ = false;

    LOG_INFO("[SdlResource] ImGui shutdown");
}

void SdlResource::ImGuiNewFrame() {
    if (!imgui_initialized_) return;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void SdlResource::ImGuiRender() {
    if (!imgui_initialized_) return;

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sdl_renderer_);
}


#include "media_core.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "sdl_common.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"


static EventType EventConvert(const SDL_Event& sdl_event) {
    EventType event_type = EventType::NONE;
    switch (sdl_event.type) {
        case SDL_EVENT_KEY_DOWN:
            // 只有在按键被按下的瞬间才会执行相应代码，一直按着，0.05 才会触发一次 event
            switch (sdl_event.key.scancode) {
                // 字母键
                case SDL_SCANCODE_A: event_type = EventType::A; break;
                case SDL_SCANCODE_B: event_type = EventType::B; break;
                case SDL_SCANCODE_C: event_type = EventType::C; break;
                case SDL_SCANCODE_D: event_type = EventType::D; break;
                case SDL_SCANCODE_E: event_type = EventType::E; break;
                case SDL_SCANCODE_F: event_type = EventType::F; break;
                case SDL_SCANCODE_G: event_type = EventType::G; break;
                case SDL_SCANCODE_H: event_type = EventType::H; break;
                case SDL_SCANCODE_I: event_type = EventType::I; break;
                case SDL_SCANCODE_J: event_type = EventType::J; break;
                case SDL_SCANCODE_K: event_type = EventType::K; break;
                case SDL_SCANCODE_L: event_type = EventType::L; break;
                case SDL_SCANCODE_M: event_type = EventType::M; break;
                case SDL_SCANCODE_N: event_type = EventType::N; break;
                case SDL_SCANCODE_O: event_type = EventType::O; break;
                case SDL_SCANCODE_P: event_type = EventType::P; break;
                case SDL_SCANCODE_Q: event_type = EventType::Q; break;
                case SDL_SCANCODE_R: event_type = EventType::R; break;
                case SDL_SCANCODE_S: event_type = EventType::S; break;
                case SDL_SCANCODE_T: event_type = EventType::T; break;
                case SDL_SCANCODE_U: event_type = EventType::U; break;
                case SDL_SCANCODE_V: event_type = EventType::V; break;
                case SDL_SCANCODE_W: event_type = EventType::W; break;
                case SDL_SCANCODE_X: event_type = EventType::X; break;
                case SDL_SCANCODE_Y: event_type = EventType::Y; break;
                case SDL_SCANCODE_Z: event_type = EventType::Z; break;

                // 数字键
                case SDL_SCANCODE_0: event_type = EventType::NUM_0; break;
                case SDL_SCANCODE_1: event_type = EventType::NUM_1; break;
                case SDL_SCANCODE_2: event_type = EventType::NUM_2; break;
                case SDL_SCANCODE_3: event_type = EventType::NUM_3; break;
                case SDL_SCANCODE_4: event_type = EventType::NUM_4; break;
                case SDL_SCANCODE_5: event_type = EventType::NUM_5; break;
                case SDL_SCANCODE_6: event_type = EventType::NUM_6; break;
                case SDL_SCANCODE_7: event_type = EventType::NUM_7; break;
                case SDL_SCANCODE_8: event_type = EventType::NUM_8; break;
                case SDL_SCANCODE_9: event_type = EventType::NUM_9; break;

                // 功能键
                case SDL_SCANCODE_F1: event_type = EventType::F1; break;
                case SDL_SCANCODE_F2: event_type = EventType::F2; break;
                case SDL_SCANCODE_F3: event_type = EventType::F3; break;
                case SDL_SCANCODE_F4: event_type = EventType::F4; break;
                case SDL_SCANCODE_F5: event_type = EventType::F5; break;
                case SDL_SCANCODE_F6: event_type = EventType::F6; break;
                case SDL_SCANCODE_F7: event_type = EventType::F7; break;
                case SDL_SCANCODE_F8: event_type = EventType::F8; break;
                case SDL_SCANCODE_F9: event_type = EventType::F9; break;
                case SDL_SCANCODE_F10: event_type = EventType::F10; break;
                case SDL_SCANCODE_F11: event_type = EventType::F11; break;
                case SDL_SCANCODE_F12: event_type = EventType::F12; break;

                // 控制键
                case SDL_SCANCODE_ESCAPE: event_type = EventType::ESCAPE; break;
                case SDL_SCANCODE_TAB: event_type = EventType::TAB; break;
                case SDL_SCANCODE_CAPSLOCK: event_type = EventType::CAPSLOCK; break;
                case SDL_SCANCODE_LSHIFT: event_type = EventType::LSHIFT; break;
                case SDL_SCANCODE_RSHIFT: event_type = EventType::RSHIFT; break;
                case SDL_SCANCODE_LCTRL: event_type = EventType::LCTRL; break;
                case SDL_SCANCODE_RCTRL: event_type = EventType::RCTRL; break;
                case SDL_SCANCODE_LALT: event_type = EventType::LALT; break;
                case SDL_SCANCODE_RALT: event_type = EventType::RALT; break;

                // 导航键
                case SDL_SCANCODE_UP: event_type = EventType::UP; break;
                case SDL_SCANCODE_DOWN: event_type = EventType::DOWN; break;
                case SDL_SCANCODE_LEFT: event_type = EventType::LEFT; break;
                case SDL_SCANCODE_RIGHT: event_type = EventType::RIGHT; break;
                case SDL_SCANCODE_HOME: event_type = EventType::HOME; break;
                case SDL_SCANCODE_END: event_type = EventType::END; break;
                case SDL_SCANCODE_PAGEUP: event_type = EventType::PAGEUP; break;
                case SDL_SCANCODE_PAGEDOWN: event_type = EventType::PAGEDOWN; break;
                case SDL_SCANCODE_INSERT: event_type = EventType::INSERT; break;
                case SDL_SCANCODE_DELETE: event_type = EventType::DEL; break;

                // 常用键
                case SDL_SCANCODE_RETURN: event_type = EventType::ENTER; break;
                case SDL_SCANCODE_SPACE: event_type = EventType::SPACE; break;
                case SDL_SCANCODE_BACKSPACE: event_type = EventType::BACKSPACE; break;

                // 符号键
                case SDL_SCANCODE_MINUS: event_type = EventType::MINUS; break;
                case SDL_SCANCODE_EQUALS: event_type = EventType::EQUALS; break;
                case SDL_SCANCODE_LEFTBRACKET: event_type = EventType::LEFTBRACKET; break;
                case SDL_SCANCODE_RIGHTBRACKET: event_type = EventType::RIGHTBRACKET; break;
                case SDL_SCANCODE_BACKSLASH: event_type = EventType::BACKSLASH; break;
                case SDL_SCANCODE_SEMICOLON: event_type = EventType::SEMICOLON; break;
                case SDL_SCANCODE_APOSTROPHE: event_type = EventType::APOSTROPHE; break;
                case SDL_SCANCODE_GRAVE: event_type = EventType::GRAVE; break;
                case SDL_SCANCODE_COMMA: event_type = EventType::COMMA; break;
                case SDL_SCANCODE_PERIOD: event_type = EventType::PERIOD; break;
                case SDL_SCANCODE_SLASH: event_type = EventType::SLASH; break;

                // 小键盘
                case SDL_SCANCODE_KP_0: event_type = EventType::KP_0; break;
                case SDL_SCANCODE_KP_1: event_type = EventType::KP_1; break;
                case SDL_SCANCODE_KP_2: event_type = EventType::KP_2; break;
                case SDL_SCANCODE_KP_3: event_type = EventType::KP_3; break;
                case SDL_SCANCODE_KP_4: event_type = EventType::KP_4; break;
                case SDL_SCANCODE_KP_5: event_type = EventType::KP_5; break;
                case SDL_SCANCODE_KP_6: event_type = EventType::KP_6; break;
                case SDL_SCANCODE_KP_7: event_type = EventType::KP_7; break;
                case SDL_SCANCODE_KP_8: event_type = EventType::KP_8; break;
                case SDL_SCANCODE_KP_9: event_type = EventType::KP_9; break;
                case SDL_SCANCODE_KP_ENTER: event_type = EventType::KP_ENTER; break;
                case SDL_SCANCODE_KP_PLUS: event_type = EventType::KP_PLUS; break;
                case SDL_SCANCODE_KP_MINUS: event_type = EventType::KP_MINUS; break;
                case SDL_SCANCODE_KP_MULTIPLY: event_type = EventType::KP_MULTIPLY; break;
                case SDL_SCANCODE_KP_DIVIDE: event_type = EventType::KP_DIVIDE; break;
                case SDL_SCANCODE_KP_PERIOD: event_type = EventType::KP_PERIOD; break;
            }
    }
    return event_type;
}

void MediaCore::GetKeyboardState(std::vector<EventType>& event_list) {
    auto keyboardState = SDL_GetKeyboardState(NULL);

    // 字母键
    if (keyboardState[SDL_SCANCODE_A]) event_list.push_back(EventType::A);
    if (keyboardState[SDL_SCANCODE_B]) event_list.push_back(EventType::B);
    if (keyboardState[SDL_SCANCODE_C]) event_list.push_back(EventType::C);
    if (keyboardState[SDL_SCANCODE_D]) event_list.push_back(EventType::D);
    if (keyboardState[SDL_SCANCODE_E]) event_list.push_back(EventType::E);
    if (keyboardState[SDL_SCANCODE_F]) event_list.push_back(EventType::F);
    if (keyboardState[SDL_SCANCODE_G]) event_list.push_back(EventType::G);
    if (keyboardState[SDL_SCANCODE_H]) event_list.push_back(EventType::H);
    if (keyboardState[SDL_SCANCODE_I]) event_list.push_back(EventType::I);
    if (keyboardState[SDL_SCANCODE_J]) event_list.push_back(EventType::J);
    if (keyboardState[SDL_SCANCODE_K]) event_list.push_back(EventType::K);
    if (keyboardState[SDL_SCANCODE_L]) event_list.push_back(EventType::L);
    if (keyboardState[SDL_SCANCODE_M]) event_list.push_back(EventType::M);
    if (keyboardState[SDL_SCANCODE_N]) event_list.push_back(EventType::N);
    if (keyboardState[SDL_SCANCODE_O]) event_list.push_back(EventType::O);
    if (keyboardState[SDL_SCANCODE_P]) event_list.push_back(EventType::P);
    if (keyboardState[SDL_SCANCODE_Q]) event_list.push_back(EventType::Q);
    if (keyboardState[SDL_SCANCODE_R]) event_list.push_back(EventType::R);
    if (keyboardState[SDL_SCANCODE_S]) event_list.push_back(EventType::S);
    if (keyboardState[SDL_SCANCODE_T]) event_list.push_back(EventType::T);
    if (keyboardState[SDL_SCANCODE_U]) event_list.push_back(EventType::U);
    if (keyboardState[SDL_SCANCODE_V]) event_list.push_back(EventType::V);
    if (keyboardState[SDL_SCANCODE_W]) event_list.push_back(EventType::W);
    if (keyboardState[SDL_SCANCODE_X]) event_list.push_back(EventType::X);
    if (keyboardState[SDL_SCANCODE_Y]) event_list.push_back(EventType::Y);
    if (keyboardState[SDL_SCANCODE_Z]) event_list.push_back(EventType::Z);

    // 数字键
    if (keyboardState[SDL_SCANCODE_0]) event_list.push_back(EventType::NUM_0);
    if (keyboardState[SDL_SCANCODE_1]) event_list.push_back(EventType::NUM_1);
    if (keyboardState[SDL_SCANCODE_2]) event_list.push_back(EventType::NUM_2);
    if (keyboardState[SDL_SCANCODE_3]) event_list.push_back(EventType::NUM_3);
    if (keyboardState[SDL_SCANCODE_4]) event_list.push_back(EventType::NUM_4);
    if (keyboardState[SDL_SCANCODE_5]) event_list.push_back(EventType::NUM_5);
    if (keyboardState[SDL_SCANCODE_6]) event_list.push_back(EventType::NUM_6);
    if (keyboardState[SDL_SCANCODE_7]) event_list.push_back(EventType::NUM_7);
    if (keyboardState[SDL_SCANCODE_8]) event_list.push_back(EventType::NUM_8);
    if (keyboardState[SDL_SCANCODE_9]) event_list.push_back(EventType::NUM_9);

    // 功能键
    if (keyboardState[SDL_SCANCODE_F1]) event_list.push_back(EventType::F1);
    if (keyboardState[SDL_SCANCODE_F2]) event_list.push_back(EventType::F2);
    if (keyboardState[SDL_SCANCODE_F3]) event_list.push_back(EventType::F3);
    if (keyboardState[SDL_SCANCODE_F4]) event_list.push_back(EventType::F4);
    if (keyboardState[SDL_SCANCODE_F5]) event_list.push_back(EventType::F5);
    if (keyboardState[SDL_SCANCODE_F6]) event_list.push_back(EventType::F6);
    if (keyboardState[SDL_SCANCODE_F7]) event_list.push_back(EventType::F7);
    if (keyboardState[SDL_SCANCODE_F8]) event_list.push_back(EventType::F8);
    if (keyboardState[SDL_SCANCODE_F9]) event_list.push_back(EventType::F9);
    if (keyboardState[SDL_SCANCODE_F10]) event_list.push_back(EventType::F10);
    if (keyboardState[SDL_SCANCODE_F11]) event_list.push_back(EventType::F11);
    if (keyboardState[SDL_SCANCODE_F12]) event_list.push_back(EventType::F12);

    // 控制键
    if (keyboardState[SDL_SCANCODE_ESCAPE]) event_list.push_back(EventType::ESCAPE);
    if (keyboardState[SDL_SCANCODE_TAB]) event_list.push_back(EventType::TAB);
    if (keyboardState[SDL_SCANCODE_CAPSLOCK]) event_list.push_back(EventType::CAPSLOCK);
    if (keyboardState[SDL_SCANCODE_LSHIFT]) event_list.push_back(EventType::LSHIFT);
    if (keyboardState[SDL_SCANCODE_RSHIFT]) event_list.push_back(EventType::RSHIFT);
    if (keyboardState[SDL_SCANCODE_LCTRL]) event_list.push_back(EventType::LCTRL);
    if (keyboardState[SDL_SCANCODE_RCTRL]) event_list.push_back(EventType::RCTRL);
    if (keyboardState[SDL_SCANCODE_LALT]) event_list.push_back(EventType::LALT);
    if (keyboardState[SDL_SCANCODE_RALT]) event_list.push_back(EventType::RALT);

    // 导航键
    if (keyboardState[SDL_SCANCODE_UP]) event_list.push_back(EventType::UP);
    if (keyboardState[SDL_SCANCODE_DOWN]) event_list.push_back(EventType::DOWN);
    if (keyboardState[SDL_SCANCODE_LEFT]) event_list.push_back(EventType::LEFT);
    if (keyboardState[SDL_SCANCODE_RIGHT]) event_list.push_back(EventType::RIGHT);
    if (keyboardState[SDL_SCANCODE_HOME]) event_list.push_back(EventType::HOME);
    if (keyboardState[SDL_SCANCODE_END]) event_list.push_back(EventType::END);
    if (keyboardState[SDL_SCANCODE_PAGEUP]) event_list.push_back(EventType::PAGEUP);
    if (keyboardState[SDL_SCANCODE_PAGEDOWN]) event_list.push_back(EventType::PAGEDOWN);
    if (keyboardState[SDL_SCANCODE_INSERT]) event_list.push_back(EventType::INSERT);
    if (keyboardState[SDL_SCANCODE_DELETE]) event_list.push_back(EventType::DEL);

    // 常用键
    if (keyboardState[SDL_SCANCODE_RETURN]) event_list.push_back(EventType::ENTER);
    if (keyboardState[SDL_SCANCODE_SPACE]) event_list.push_back(EventType::SPACE);
    if (keyboardState[SDL_SCANCODE_BACKSPACE]) event_list.push_back(EventType::BACKSPACE);

    // 符号键
    if (keyboardState[SDL_SCANCODE_MINUS]) event_list.push_back(EventType::MINUS);
    if (keyboardState[SDL_SCANCODE_EQUALS]) event_list.push_back(EventType::EQUALS);
    if (keyboardState[SDL_SCANCODE_LEFTBRACKET]) event_list.push_back(EventType::LEFTBRACKET);
    if (keyboardState[SDL_SCANCODE_RIGHTBRACKET]) event_list.push_back(EventType::RIGHTBRACKET);
    if (keyboardState[SDL_SCANCODE_BACKSLASH]) event_list.push_back(EventType::BACKSLASH);
    if (keyboardState[SDL_SCANCODE_SEMICOLON]) event_list.push_back(EventType::SEMICOLON);
    if (keyboardState[SDL_SCANCODE_APOSTROPHE]) event_list.push_back(EventType::APOSTROPHE);
    if (keyboardState[SDL_SCANCODE_GRAVE]) event_list.push_back(EventType::GRAVE);
    if (keyboardState[SDL_SCANCODE_COMMA]) event_list.push_back(EventType::COMMA);
    if (keyboardState[SDL_SCANCODE_PERIOD]) event_list.push_back(EventType::PERIOD);
    if (keyboardState[SDL_SCANCODE_SLASH]) event_list.push_back(EventType::SLASH);

    // 小键盘
    if (keyboardState[SDL_SCANCODE_KP_0]) event_list.push_back(EventType::KP_0);
    if (keyboardState[SDL_SCANCODE_KP_1]) event_list.push_back(EventType::KP_1);
    if (keyboardState[SDL_SCANCODE_KP_2]) event_list.push_back(EventType::KP_2);
    if (keyboardState[SDL_SCANCODE_KP_3]) event_list.push_back(EventType::KP_3);
    if (keyboardState[SDL_SCANCODE_KP_4]) event_list.push_back(EventType::KP_4);
    if (keyboardState[SDL_SCANCODE_KP_5]) event_list.push_back(EventType::KP_5);
    if (keyboardState[SDL_SCANCODE_KP_6]) event_list.push_back(EventType::KP_6);
    if (keyboardState[SDL_SCANCODE_KP_7]) event_list.push_back(EventType::KP_7);
    if (keyboardState[SDL_SCANCODE_KP_8]) event_list.push_back(EventType::KP_8);
    if (keyboardState[SDL_SCANCODE_KP_9]) event_list.push_back(EventType::KP_9);
    if (keyboardState[SDL_SCANCODE_KP_ENTER]) event_list.push_back(EventType::KP_ENTER);
    if (keyboardState[SDL_SCANCODE_KP_PLUS]) event_list.push_back(EventType::KP_PLUS);
    if (keyboardState[SDL_SCANCODE_KP_MINUS]) event_list.push_back(EventType::KP_MINUS);
    if (keyboardState[SDL_SCANCODE_KP_MULTIPLY]) event_list.push_back(EventType::KP_MULTIPLY);
    if (keyboardState[SDL_SCANCODE_KP_DIVIDE]) event_list.push_back(EventType::KP_DIVIDE);
    if (keyboardState[SDL_SCANCODE_KP_PERIOD]) event_list.push_back(EventType::KP_PERIOD);
}


void MediaCore::MediaInit(int window_width, int window_height) {
    window_width_ = window_width;
    window_height_ = window_height;
    SdlResource::Instance().Init(window_width, window_height);

    last_timestamp_ns_ = SDL_GetTicksNS();
    frame_duration_ns_ = 1e9 / FPS_; // 纳秒转换
}

void MediaCore::SetFPS(uint64_t FPS) {
    FPS_ = FPS;
    frame_duration_ns_ = 1e9 / FPS_; // 纳秒转换
}

void MediaCore::FPSControl() {
    // 帧率控制
    uint64_t elapsed = SDL_GetTicksNS() - last_timestamp_ns_;
    if (elapsed < frame_duration_ns_) {
        uint64_t left_sleep = frame_duration_ns_ - elapsed;
        SDL_DelayNS(left_sleep);
        delta_s_ = frame_duration_ns_ / 1.0e9;
        // LOG_INFO("TaskManager", "{}, {}", frame_duration_ns_, delta_s_);
    }
    else {
        delta_s_ = elapsed / 1.0e9;
    }

    // RuntimeFPS_ = 1e9 / (SDL_GetTicksNS() - last_timestamp_ns_);
    // LOG_INFO(SDL_LOG_CATEGORY_APPLICATION, "{}, {}, {}, {}, {}", SDL_GetTicksNS(), last_timestamp_ns_, elapsed, frame_duration_ns_, RuntimeFPS_);

    last_timestamp_ns_ = SDL_GetTicksNS();
}


float MediaCore::GetDeltaTimeS() { return delta_s_; }

void MediaCore::MainRun() {
#ifndef EMSCRIPTEN
    while(!game_exit_) {
#endif
        // Run();
        /**************************** Event Processing ************************/
        EventProcess(); //所有对象的 Update 方法
        /**************************** ImGui NewFrame ************************/
        ImGuiNewFrame();
        /**************************** Update ****************************/
        Update();   //所有对象的 Update 方法
        /**************************** Render ****************************/
        SDL_SetRenderDrawColor(SdlResource::Instance().GetRender(), 21, 21, 21, 255);
        SDL_RenderClear(SdlResource::Instance().GetRender());
        Render();   //所有对象的 Render 方法
        ImGuiRender();   // ImGui 渲染
        SDL_RenderPresent(SdlResource::Instance().GetRender());
        /**************************** FrameRate ****************************/
        FPSControl();
#ifndef EMSCRIPTEN
    }
#endif
}

void MediaCore::RegEventHandler(EventHandler handler) {
    event_handler_list_.push_back(handler);
}

void MediaCore::Quit() {
    game_exit_ = true;
    LOG_INFO("MediaCore, Quit game");
}

void MediaCore::RegUpdateHandler(UpdateHandler handler) {
    update_handlers_list_.push_back(handler);
}

void MediaCore::RegRenderHandler(RenderHandler handler) {
    render_handlers_list_.push_back(handler);
}

/**
 * @brief 处理媒体事件的核心函数
 * @param event_list 事件列表的引用，包含需要处理的事件集合
 * @note 该函数用于处理媒体相关的各类事件，是媒体事件处理的核心入口
 */
void MediaCore::EventProcess() {
    std::vector<EventType> event_list{};
    SDL_Event event;

    while (SDL_PollEvent(&event) != 0) {
        // Pass event to ImGui first
        bool wants_capture_event = false;
        if (MediaCore::Instance().IsImGuiInitialized()) {
            wants_capture_event = ImGui_ImplSDL3_ProcessEvent(&event);
        }

        // Skip other handlers if ImGui captured the event
        if (wants_capture_event) {
            continue;
        }

        switch (event.type) {
            case SDL_EVENT_QUIT:
                Quit();
                break;
        }

        EventType event_type = EventConvert(event);
        event_list.push_back(event_type);
    }

    for (const auto& handler : event_handler_list_) {
        handler(event_list);
    }
}

void MediaCore::Update() {
    for (const auto& handler : update_handlers_list_) {
        handler();
    }
}

void MediaCore::Render() {
    for (const auto& handler : render_handlers_list_) {
        handler();
    }
}



// ============================================================================
// MediaUtil 实现
// ============================================================================

bool MediaUtil::RectHasIntersection(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    SDL_Rect a = {x1, y1, w1, h1};
    SDL_Rect b = {x2, y2, w2, h2};
    return SDL_HasRectIntersection(&a, &b);
}

bool MediaUtil::DrawTextRect(const std::string& text, float x, float y, float w, float h,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char* font_path) {
    int font_size = static_cast<int>(h);
    Font font(font_path, font_size);
    return font.RenderText(text, x, y, r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

bool MediaUtil::DrawTextRect(const std::string& text, float x, float y, float w, float h,
                             const Color& color, const char* font_path) {
    return DrawTextRect(text, x, y, w, h, color.r, color.g, color.b, color.a, font_path);
}



// ============================================================================
// MediaCore ImGui 实现
// ============================================================================

void MediaCore::ImGuiInit() {
    SdlResource::Instance().ImGuiInit();
}

void MediaCore::ImGuiShutdown() {
    SdlResource::Instance().ImGuiShutdown();
}

void MediaCore::ImGuiNewFrame() {
    SdlResource::Instance().ImGuiNewFrame();
}

void MediaCore::ImGuiRender() {
    SdlResource::Instance().ImGuiRender();
}

bool MediaCore::IsImGuiInitialized() {
    auto& io = ImGui::GetIO();
    return io.Fonts->IsBuilt();
}

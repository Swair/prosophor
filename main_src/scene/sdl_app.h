// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "common/noncopyable.h"
#include "common/input_event.h"
#include "scene/home_screen.h"
#include <functional>

namespace prosophor {

/// SdlApp: SDL-based graphical interface entry point
/// SDL dependencies are hidden behind media_engine interfaces
class SdlApp : public Noncopyable {
 public:
    static SdlApp& GetInstance();

    int Run();
    void Stop();

    /// Handle text input
    void HandleTextInput(const char* text);

    /// Handle key event
    void HandleKeyDown(int key_code);

    /// Handle mouse event
    void HandleMouseButtonDown(int x, int y);

    /// Set input event callback
    using InputCallback = std::function<void(const InputEvent&)>;
    void SetInputCallback(InputCallback callback);

    /// Get current UI mode
    UIMode GetCurrentMode() const { return current_scene_; }

 private:
    SdlApp();
    ~SdlApp();

    void Initialize();
    void Shutdown();

    /// Switch to a different UI mode
    void SwitchMode(UIMode mode);

    /// Mode-specific render handlers
    void RenderHome();
    void RenderVirtualHuman();
    void RenderGalgame();
    void RenderTerminal();

    InputCallback input_callback_;
    UIMode current_scene_ = UIMode::HOME;

    InputCallback saved_callback_;
};

}  // namespace prosophor

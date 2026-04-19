// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "common/noncopyable.h"
#include "common/input_event.h"
#include <functional>

namespace aicode {

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

 private:
    SdlApp();
    ~SdlApp();

    void Initialize();
    void Shutdown();

    InputCallback input_callback_;
};

}  // namespace aicode

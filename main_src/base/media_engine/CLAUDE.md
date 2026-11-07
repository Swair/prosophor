# 模块: media_engine — 媒体与 UI 引擎层

## 用途
完整的媒体和 UI 引擎层，将 SDL3 + SDL_image + SDL_ttf + ImGui 封装在一个外观之后。提供窗口管理、2D 绘图原语、纹理、字体、ImGui 控件包装器和面板容器。

## 关键文件

| 文件 | 职责 | 实例化模式 |
|------|------|-----------|
| `media_engine.h` | 统一包含入口点（外部模块用这个，不要用单个文件） | — |
| `media/media_core.h` | `MediaCore` 单例（游戏循环、FPS、多窗口、事件） | **单例** (`GetInstance()`)，有状态 |
| `media/texture/window.h` | `Window` 类（SDL_Window + SDL_Renderer + ImGui 上下文，PIMPL） | 常规类，按需构造，有状态 |
| `media/texture/drawer.h` | SDL 2D 图元（矩形、圆形、多边形、圆角矩形、三角形） | 自由函数（无状态） |
| `media/texture/imgui_widget.h` | ImGui 控件 PIMPL 包装（Button, InputText, ScrollWindow 等） | 自由函数 + 工具类 |
| `media/texture/colors.h` | RGBA `Color` 结构体 + 80+ 命名常量 | 数据结构（无状态） |
| `media/texture/texture.h` | 纹理加载/渲染 | 常规类，按需构造 |
| `media/font/font.h` | 字体管理 | 常规类，按需构造 |
| `media/audio/audior.h` | 音频输出 | `AudioPlayer` 单例 |
| `media/audio/audio_capture.h` | `AudioCapture` 麦克风输入（PIMPL） | 常规类，按需构造 |
| `media/audio/audio_streamer.h` | `AudioStreamer` 音频播放（PIMPL） | 常规类，按需构造 |
| `media/audio/voice_channel.h` | `VoiceChannel` 全局音频 I/O 线程（麦克风→回调，回调→扬声器） | **单例** (`GetInstance()`)，有状态 |
| `ui_component/` | 高级复合控件（UIPanel, InputPanel, HeaderBar） | 常规类，按需构造 |

## 模块组织（接口/实现两层，每层内按领域子目录：texture/font/audio）

```
media_engine/
│
├── media_engine.h                    → 统一入口（外部仅引用此文件）
│
├── media/（公开接口，不暴露 SDL）
│   ├── media_core.h                 → MediaCore 单例（跨领域根组装，留接口层根）
│   ├── texture/                      → 渲染领域
│   │   ├── window.h                 → Window PIMPL
│   │   ├── drawer.h                 → 2D 绘图（MediaUtil 静态方法）
│   │   ├── colors.h                 → Color + Colors:: 命名常量（font/imgui 共用，基础）
│   │   ├── texture.h                → Texture 加载/渲染
│   │   └── imgui_widget.h           → ImGui 控件包装
│   ├── font/                         → 字体领域
│   │   ├── font.h                   → Font 管理
│   │   └── droid_sans_font.h        → 构建期生成的嵌入字体头
│   └── audio/                        → 音频领域
│       ├── audior.h                 → AudioPlayer 单例
│       ├── audio_capture.h          → AudioCapture 麦克风（PIMPL）
│       ├── audio_streamer.h         → AudioStreamer 播放（PIMPL）
│       └── voice_channel.h          → VoiceChannel 全局音频线程
│
├── sdl_interface/（PIMPL 实现，隐藏 SDL/ImGui 头文件）
│   ├── media_core.cc                → 主循环、多窗口事件分发（留根）
│   ├── sdl_common.*                 → SdlResource 单例（audio/font/texture 共享，留根）
│   ├── texture/                      → 渲染实现
│   │   ├── window.cc                → SDL 窗口 + ImGui 上下文
│   │   ├── drawer.cc                → SDL_Renderer 绘图
│   │   ├── texture.cc               → SDL_Texture 加载/渲染
│   │   └── imgui_widget.cc          → ImGui API 调用（另编 imgui_widget_lib）
│   ├── font/                         → 字体实现
│   │   └── font.cc                  → SDL_ttf 渲染
│   └── audio/                        → 音频实现
│       ├── audio_capture.cc         → AudioCapture PIMPL 实现
│       ├── audio_streamer.cc        → AudioStreamer PIMPL 实现
│       ├── audior.cc                → AudioPlayer 实现
│       └── voice_channel.cc         → VoiceChannel 音频线程实现
│
└── ui_component/（复合控件）
    ├── ui_panel.*                   → UIPanel（面板容器）
    ├── input_panel.*                → InputPanel（文本输入 + 发送按钮）
    └── header_bar.*                 → HeaderBar（标题栏）
```

领域依赖方向：`font → texture`（colors）、`audio` 独立；`ui_component` → 各领域；`media_core` 为跨领域装配根。平台替换（SDL → 其他）只需换 `sdl_interface/` 层。

## 代码组织关系

### 内部依赖
```
┌──────────────────────────────────────────────────────────┐
│                     MediaCore 单例                         │
│  主循环 │ 事件分发 │ 窗口管理 │ 渲染调度                     │
└────────┬─────────┬──────────┬─────────────────────────────┘
         │         │          │
    ┌────┴──┐ ┌───┴────┐ ┌───┴────────┐
    ▼       ▼ ▼        ▼ ▼            ▼
┌────────┐ ┌────────┐ ┌──────────────────┐ ┌────────────────┐
│ Window  │ │ Window  │ │  ...  │ Window    │ │ mouse_window_   │
│ #0     │ │ #1     │ │       │ #N       │ │ handlers_[]     │
│(主窗口) │ │(sprite)│ │       │(sprite)  │ │ (事件路由)      │
└───┬────┘ └───┬────┘ └────────┴──────────┘ └────────────────┘
    │          │
    ▼          ▼
┌──────────────────────┐
│  ImGui Context (每个  │
│  Window 独立上下文)    │
│  → imgui_widget.cc   │
└──────────────────────┘

┌──────────────────────────────────────────────────┐
│                 MediaUtil（静态方法）                │
│  DrawTextRect → Font.RenderText                   │
│  RectHasIntersection                              │
│  各种绘图方法 → Drawer 2D 图元                     │
└──────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────┐
│                 SdlResource 单例                   │
│  持有 primary_window_ 的 SDL_Window* / Renderer*  │
└──────────────────────────────────────────────────┘

### 音频管线
```
VoiceChannel 单例（音频 I/O 线程）
│
├── 输入方向:
│   AudioCapture::Read() 每 30ms 读 480 samples
│   → on_capture_(int16_t*, size_t) 回调
│   → VoiceEngine::FeedPCM() → VAD → ASR
│
└── 输出方向:
│   on_playback_(int16_t*, max) → size_t 回调
│   → AudioStreamer::PushChunk() → SDL 播放
│
VoiceEngine::Speak() → TTS 合成线程
  → playback_buf_ (mutex 保护)
  → VoiceChannel 音频线程 PollPlaybackCb() 拉取
```

## 依赖
- `common/`（日志）
- 外部: SDL3, SDL_image, SDL_ttf, SDL_mixer, ImGui 1.92.8

## 责任约束
- **不**引用 `prosophor_core/`、`scene/`、`virtual_sprite/`
- **不**包含业务逻辑——不含 LLM、Agent 或会话概念
- **不**了解应用级 UI 布局
- 独立的图形层。所有 SDL 和 ImGui 头文件必须隐藏在 PIMPL 后面

## 实例化模式
- `MediaCore` — 单例（静态局部变量），有状态
- `VoiceChannel` — 单例（静态局部变量），有状态
- `AudioPlayer` — 单例，有状态
- `SdlResource` — 单例，有状态
- `AudioCapture` — 常规类，按需构造
- `AudioStreamer` — 常规类，按需构造
- `Window` — 常规类，`CreateMediaWindow` 创建，`MediaCore` 持有 `unique_ptr`
- `Texture` / `Font` — 常规类，按需构造
- `UIPanel` / `InputPanel` / `HeaderBar` — 常规类，按需构造
- 绘图/工具方法 — 自由函数（无状态）

## 关键约定
- `media_engine` 命名空间（不是 `prosophor`）
- 所有 SDL/ImGui 头文件在 PIMPL 的 `Impl` 结构体后面
- Window 用不透明指针守卫（头文件中前向声明 `struct SDL_Window;`）
- 外部模块通过 `#include "media_engine/media_engine.h"` 引用
- 颜色在 `colors.h` 中用抽象名称定义，业务代码中不使用内联 RGBA

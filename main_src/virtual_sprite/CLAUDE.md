# 模块: virtual_sprite — 桌面宠物应用

## 用途
完整的 SDL 窗口应用层——桌面宠物模式入口。拥有 sprite 窗口（动画像素宠物 + 语音气泡叠加）和共享的中央聊天窗口。

## 关键文件

| 文件 | 职责 | 实例化模式 |
|------|------|-----------|
| `virtual_sprite.h` / `virtual_sprite.cc` | `VirtualSprite` 单例（顶层应用，拥有中央窗口 + 注册全局处理器） | **单例** (`GetInstance()`)，有状态 |
| `sprite.h` / `sprite.cc` | `Sprite` 类（每个 sprite 的窗口，含宠物动画、语音气泡、拖拽、导航按钮、自有会话） | 常规类，`SpriteManager` 创建 |
| `sprite_manager.h` / `sprite_manager.cc` | `SpriteManager` 单例（管理多个 Sprite 实例，会话 ID 查询，"+New" 回调） | **单例** (`GetInstance()`)，有状态 |
| `chat_window.h` / `chat_window.cc` | 中央聊天窗口（消息历史 + 输入面板，跟随焦点 sprite） | 常规类，`VirtualSprite` 持有 |
| `voice_engine.h` / `voice_engine.cc` | `VoiceEngine` 单例（SDL 语音门面：`GetInstance/PollAsrLatest/StartCapture/StopCapture/Speak`，其余委托 `base/voice_interaction`） | **单例** (`GetInstance()`)，有状态 |
| `media_engine_audio_output.h/.cc` | `AudioOutput` 适配器：TTS PCM → 重采样 16k → `VoiceChannel::PlayAudio` | 常规类，`VoiceEngine` 持有 |

## 模块组织

```
virtual_sprite/                 ← 应用层
│
├── virtual_sprite.h/.cc       → VirtualSprite 单例
│   ├── central_window_        → ChatWindow
│   ├── SpriteManager          → Sprite 容器
│   └── 全局处理器注册
│
├── sprite_manager.h/.cc       → SpriteManager 单例
├── sprite.h/.cc               → Sprite 类（每个 sprite 一个窗口）
│
├── chat_window.h/.cc          → ChatWindow（主窗口 + 视图路由）
│   ├── RenderCurrentView()    → 根据 NavItem 路由到对应 *_view
│   └── View Router:           → 20+ 个视图
│
├── voice_engine.h/.cc         → VoiceEngine 单例（SDL 语音门面）
│   ├── 注册 VoiceChannel 回调 → interaction->FeedFrame
│   ├── VAD = vad::WebRtcVad；状态机在 voice_interaction/voice_pipeline
│   └── 播放 = MediaEngineAudioOutput；ASR/TTS provider 在 voice_interaction/providers
│
├── components/                ← 可复用控件层（原 components/ 模块，已并入）
│   ├── bordered_container.h/.cc ← 带边框子容器
│   ├── selectable_item.h/.cc  ← 三态列表项
│   ├── item_list.h/.cc        ← 可选中列表容器
│   ├── panel_kit.h/.cc        ← Card、SplitPanel、ActionBar、FocusCard 等
│   ├── sidebar.h/.cc          ← 左侧导航栏
│   ├── chat_panel.h/.cc       ← 聊天消息历史
│   ├── speech_bubble.h/.cc    ← 语音气泡
│   ├── spritesheet.h/.cc      ← 精灵表动画
│   └── status_bar.h/.cc       ← 底部状态栏
│
└── panels/                    ← 业务视图层（仅编排，不手绘）
    ├── model_card.h/.cc       ← BorderedContainer + 4 字段
    ├── provider_entry_card.h/.cc ← BorderedContainer + 字段 + ModelCard × N
    ├── panel_helpers.h        ← 统一 include 控件头（兼容旧引用）
    └── *_view.cc ×19          ← 每个 sidebar 项一个视图

media_engine/                   ← 引擎层（外部模块）
├── PanelContainer             ← 页面壳（白底圆角 + 标题 + 内容区）
├── DrawList                   ← 2D 绘图原语（RoundRect, Text, Channels 等）
├── Child / ScopedChild        ← 子窗口原语
├── Style / Colors             ← 样式/颜色系统
└── Layout / Text / ImGuiWidget ← ImGui 控件包装
```

### 视图编排示例

```
PanelContainer (页面壳)
  └── SplitPanel (左右分栏)
        ├── ItemList (左侧列表)
        │     └── SelectableItem × N
        └── Card / ModelCard / ProviderEntryCard (右侧内容)
              ├── Card::Field()
              └── Advance() / SetY()
```

### 控件体系（components/）

底层原语来自 media_engine；本模块的 components/ 在其上封装可复用控件。所有组件都是独立的封装单元——不因"简单"而内联，不因"复杂"而特殊。

```
容器类：
  BorderedContainer     ← ScopedChild + Borders + 可选底色 + 圆角
  ItemList              ← 可选中列表容器（Y 追踪 + 列表项）
  SelectableItem        ← 列表项（InvisibleButton + 三态视觉）

卡片体系（Card 为核心，参数化配色）：
  Card                  ← 背景 + 边框 + 标题（上方/内部）+ Field() + 自适应/固定高度
                          ChannelsSplit 实现正确图层顺序
  WhiteCard             ← White + CreamBorder（简化封装，title_above=false）
  FocusCard             ← OrangeLightest + OrangeWarm（三态交互，固定高度）
  StatCard              ← White + CreamBorder + 左侧色条

布局/操作辅助：
  SplitPanel            ← 左右分栏坐标计算
  ActionBar             ← 底部 Save/Cancel 按钮栏
  PanelHelper           ← 存量 helper（LabelRow / SectionCard / Spacing 等）

业务卡片（panels/ 层，非 components/）：
  ModelCard             ← BorderedContainer + model/temperature/max_tokens/context_window
  ProviderEntryCard     ← BorderedContainer + 字段 + ModelCard × N
```

#### Card 参数详解

```cpp
Card(x, y, w, title, sm = 1.0f,
     bg_color = Beige,
     border_color = Gray63,
     title_color = OrangeDeep,
     radius = 6.0f,
     fixed_h = 0,          // 0=自适应，>0=固定高度
     title_above = true);   // true=标题在边框上方，false=在内部
```

- 构造时：ChannelsSplit(2)，内容渲染在通道 1（前景）
- 析构时：通道 0 绘制背景 + 边框（背景层）
- 图层顺序：背景 → 标题 → 内容字段 → 边框

#### 关键约定

- 所有控件在 `prosophor` 命名空间
- 构造即绘制（ImGui 立即模式），析构执行收尾操作
- 不因"简单"而内联——有名字的概念就应封装

## 代码组织关系

### 内部依赖链
```
┌───────────────────────────────────────────────────────┐
│               VirtualSprite 单例                        │
│  顶层应用 │ 中央窗口 │ 全局处理器注册                     │
└────────┬────────────────────┬──────────────────────────┘
         │                    │
    ┌────┴──────┐       ┌────┴──────────┐
    ▼           ▼       ▼               ▼
┌──────────────┐  ┌────────────────────────────────────┐
│ ChatWindow    │  │       SpriteManager 单例            │
│ 中央聊天窗口   │  │  sprites_[] 容器 │ 焦点追踪          │
│ (主窗口)      │  └────────────────┬───────────────────┘
│ InputPanel   │                   │
│ ChatPanel    │                   ▼
└──────────────┘  ┌────────────────────────────────────┐
                  │           Sprite 类（N 个实例）       │
                  │  ┌──────────────────────────────┐   │
                  │  │ sprite_window_（透明、置顶）    │   │
                  │  │ session_id_ → AgentEngine     │   │
                  │  │ pet_sprite_ → Spritesheet     │   │
                  │  │ speech_bubble_ → SpeechBubble │   │
                  │  │ 鼠标处理器: 单击/双击/拖拽     │   │
                  │  │ 导航栏: Prev/Next/+New        │   │
                  │  └──────────────────────────────┘   │
                  └────────────────────────────────────┘
```

### 创建与生命周期
```
VirtualSprite::GlobalInit()
  │
  ├── 1. central_window_.Create()    → SDL 主窗口（第一个创建）
  │
  ├── 2. create_sprite("Prosophor Assistant")
  │       └→ SpriteManager::CreateSprite()
  │             ├→ new Sprite → Sprite::Create()
  │             │    ├→ MC::CreateMediaWindow()  → 次级窗口
  │             │    ├→ RegMouseHandler          → 鼠标事件
  │             │    └→ RegRenderHandler         → 每帧渲染
  │             └→ sprites_.push_back(unique_ptr)
  │
  ├── 3. create_sprite("Mascot")     → 同上，位置偏移 (80,120)
  │
  ├── 4. RegUpdateHandler(OnUpdate)  → 全局动画更新
  ├── 5. RegEventHandler            → 键盘事件
  └── 6. SetOnNewSprite             → "+New" 按钮创建更多 sprite
```

### 事件流
```
用户输入
  │
  ├── 键盘事件 → RegEventHandler → VirtualSprite::HandleKeyDown
  │                                → input_callback_ → InputEvent
  │
  ├── sprite 鼠标事件 → RegMouseHandler(sprite_window_)
  │   ├── 左键单击 → SetFocusedSession + show_nav_popup_
  │   ├── 双击 → on_toggle_central_（切换中央窗口）
  │   └── 右键 → 全局上下文菜单
  │
  └── 中央窗口输入 → InputPanel::OnSubmit
                      → AgentEngine::SendUserMessage(focused_session_)
                               │
                               └→ SetOutputCallback → sprite::SetAgentState
```

## 依赖
- `media_engine/`（渲染、窗口、控件、颜色、纹理；`voice_channel.h` 音频 I/O 线程）
- `prosophor_core/`（AgentEngine, AgentRuntimeState, RenderSnapshot；ASR + TTS Provider 路由）
- `voice_interaction/`（统一语音闭环、WebRTC VAD、ASR/TTS provider；`vad/webrtc_vad` 内部用 `rtc_wrapper/rtc` 算法）
- `common/`（日志、工具）
- `platform/`（字体路径）

（`components/` 控件层已并入本模块 —— `virtual_sprite/components/`，不再单独依赖。控件共享类型 `ui_types.h`（ChatMessage/StateVisualProps）也在本模块内。）

## 责任约束
- **不**配置提供商或注册工具
- **不**包含核心 LLM 循环逻辑——委托给 AgentEngine
- **不**渲染场景角色（那是 scene/ 的事）
- 仅 UI 编排层——组合控件，不处理业务

## 实例化模式
- `VirtualSprite` — 单例（`Noncopyable` + 静态局部变量），有状态
- `SpriteManager` — 单例，有状态
- `Sprite` — 常规类，`SpriteManager::CreateSprite()` 创建，`unique_ptr` 持有
- `ChatWindow` — 常规类，`VirtualSprite` 直接持有

## 关键约定
- 每个 sprite 拥有自有会话（`session_id_`）
- 中央窗口第一个创建（成为 SDL 主窗口），所有 sprite 是次级窗口
- Sprite 窗口无边框 + 透明 + 跳过任务栏
- 状态到动作的映射通过 `StateToAction()` 用于宠物动画
- 跨层回调通过 `std::function`（`SetOnToggleCentralWindow`, `SetOnNewSprite`）

## 组件命名约定

### Container vs Card

| 后缀 | 含义 | 可复用性 | 示例 |
|------|------|---------|------|
| `*Container` | **通用容器**——带边框/底色的通用外壳，不绑定业务 | 高，任何视图可用 | `PanelContainer`, `BorderedContainer`, `SectionContainer` |
| `*Card` | **定制卡片**——绑定具体业务字段/数据的组件 | 低，仅特定视图使用 | `ModelCard`（专用于 model 配置） |

**区分原则：**
- 如果其他视图也能直接拿过来用 → `Container`
- 如果包含特定业务标签/字段，别处用不上 → `Card`

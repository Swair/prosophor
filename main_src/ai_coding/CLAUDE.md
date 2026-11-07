# 模块: ai_coding — TUI 前端

## 用途
终端 UI 前端。持有终端事件循环，所有业务逻辑委托给 `AgentEngine`。

## 关键文件

| 文件 | 职责 | 实例化模式 |
|------|------|-----------|
| `ai_coding.h` / `ai_coding.cc` | `AiCoding` 单例（Run/Stop/HandleInputEvent） | **单例** (`GetInstance()`)，有状态 |
| `banner.h` / `banner.cc` | ASCII 艺术启动图标 | 自由函数（无状态） |

## 模块组织

```
┌──────────────────────────────────────┐
│            AiCoding 单例               │
│                                      │
│  Run() → 终端事件循环                  │
│  HandleInputEvent() → AgentEngine     │
└────────────────┬─────────────────────┘
                 │
    ┌────────────┼────────────┐
    ▼            ▼            ▼
┌──────────┐ ┌──────────┐ ┌──────────────┐
│AgentEngin│ │banner.h  │ │InputHandler  │
│e         │ │ASCII 图标│ │（platform/） │
│业务委托   │ │          │ │终端原始输入   │
└──────────┘ └──────────┘ └──────────────┘
```

## 代码组织关系

### 内部依赖
```
ai_coding.cc
  ├── ai_coding.h       → AiCoding 类声明
  ├── banner.h          → ASCII 启动图标
  ├── platform/         → InputHandler（终端输入）
  └── prosophor_core/   → AgentEngine（所有业务逻辑）

banner.h / .cc          → 自包含，无内部依赖
```

### 文件引用链
```
main() → VirtualSprite::Run() / AiCoding::Run()
                                  │
                                  └→ AgentEngine（会话、工具、提供商全部委托）
```

## 依赖
- `common/`（日志、工具）
- `prosophor_core/`（AgentEngine 所有业务逻辑）
- `platform/`（终端输入处理）

## 责任约束
- **不**引用 SDL/ImGui/media_engine
- **不**包含业务逻辑——纯事件循环 + 显示
- **不**管理窗口——仅终端

## 实例化模式
- `AiCoding` — 单例（`Noncopyable` 基类 + 静态局部变量）

## 关键约定
- `prosophor` 命名空间
- `#pragma once` + SPDX 许可头

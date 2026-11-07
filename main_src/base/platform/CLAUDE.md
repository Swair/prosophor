# 模块: platform — 跨平台 OS 抽象

## 用途
跨平台操作系统抽象（`#ifdef`）——控制台编码、文件系统路径、管道/进程创建（LSP/MCP 通信）、原始终端输入、子进程管理、编码转换。

## 关键文件

| 文件 | 职责 | 实例化模式 |
|------|------|-----------|
| `platform.h` | `Platform` 静态类（ReadLine, HomeDir, SetConsoleUtf8, RunCommandWithOutput 等）+ 数据结构 PipePair/ForkedProcess/CommandOutput | 静态类（全部 static 方法） |
| `input_handler.h` | `InputHandler` 类（原始模式、tab 补全、历史、UTF-8 输入） | 按需构造 |
| `pipe_handler.h` | 兼容头文件（仅 `#include "platform/platform.h"`，管道操作声明在 `Platform` 类中） | 兼容转发 |

## 模块组织

```
platform/
│
├── platform.h / .cc              → 统一跨平台 API
│   ├── #ifdef _WIN32 分支        → platform_win32 实现
│   └── #ifdef __linux__ 分支     → platform_posix 实现
│
├── input_handler.h / .cc         → InputHandler 类（PIMPL）
│   ├── input_handler_win32.cc    → Win32 终端输入实现
│   └── input_handler_posix.cc    → POSIX 终端输入实现
│
├── pipe_handler.h                 → 兼容转发头文件（管道操作声明在 Platform 类中）
│   ├── pipe_handler_win32.cc     → Win32 管道实现
│   └── pipe_handler_posix.cc     → POSIX 管道实现
│
└── 导出符号:
    kIsWindows / kIsLinux / kIsMacOS  → 编译时平台常量
    kDefaultFontPath                  → 系统字体路径
```

## 代码组织关系

### 内部依赖
```
platform.h
  ├── platform.cc          → 平台无关实现
  ├── input_handler.h      → InputHandler 类（PIMPL）
  │   ├── input_handler_win32.cc  → Win32 具体实现
  │   └── input_handler_posix.cc  → POSIX 具体实现
  └── pipe_handler.h       → 兼容转发（所有管道声明在 Platform 类中）
      ├── pipe_handler_win32.cc   → Win32 具体实现
      └── pipe_handler_posix.cc   → POSIX 具体实现
```

### 文件引用链
```
调用方（任何模块）
  └→ #include "platform/platform.h"
       ├→ ReadLine, HomeDir, SetConsoleUtf8 等（平台无关）
       ├→ InputHandler（平台相关，.cc 通过 #ifdef 选择实现）
       └→ PipePair, ForkedProcess（平台相关）
```

## 依赖
- `common/`（time_wrapper.h 间接）
- 标准库 + Win32/POSIX API

## 责任约束
- **不**引用 `media_engine/`、`scene/`、`virtual_sprite/`、`ai_coding/`、`prosophor_core/`
- **不**包含 UI 或图形概念
- 纯操作系统抽象层

## 关键约定
- 编译时检测通过 `kIsWindows`、`kIsLinux`、`kIsMacOS` 常量
- 平台相关代码在专用 `.cc` 文件中，共享接口在 `.h` 中
- `class Platform`（全部 static 方法，`Platform() = delete`）
- `InputHandler` 采用 PIMPL 模式做平台特定实现

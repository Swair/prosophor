# 模块: common — 共享基础工具

## 用途
所有模块共用的基础工具——线程池、缓冲/队列、文件 I/O、日志、时间包装、输入事件类型、字符串编码。

## 关键文件

| 文件 | 职责 | 实例化模式 |
|------|------|-----------|
| `noncopyable.h` | `Noncopyable` 基类（删拷贝、允许移动） | 基类（无状态） |
| `thread_pool.h` | 通用线程池，task 提交、future 支持、并发限制 | 模板类，按需构造 |
| `buffers.h` | 缓冲/队列：`BlockingQueue`（有界阻塞交接）、`ListBuffer`（增长累积）、`FixedBuffer`（最近 N 条） | 模板类，按需构造 |
| `file_utils.h` | 跨平台文件读写、JSON 序列化、路径展开 | 自由函数（无状态） |
| `log_wrapper.h` | spdlog 初始化包装 `LOG_INFO`/`LOG_DEBUG` 等 | 自由函数（无状态） |
| `time_wrapper.h` | `SystemClock`（挂钟）和 `SteadyClock`（单调） | 自由函数（无状态） |
| `input_event.h` | 统一 `InputEvent` 变体和 `InputSource` 枚举 | 数据结构（无状态） |
| `string_utils.h` | 字符串处理（大小写 / UTF-8 校验） | 自由函数（无状态） |
| `constants.h` | 默认 LLM/工具/MCP/HTTP 设置 | 常量（无状态） |

## 模块组织

```
common/ 基本平级
─────────────────────────────────

noncopyable.h    ─→ 所有单例的基类
thread_pool.h    ─→ 线程池（标准库）
buffers.h        ─→ 阻塞队列 + 积累/环形缓冲（标准库）
file_utils.h     ─→ 文件 I/O + JSON（nlohmann_json）
log_wrapper.h    ─→ spdlog 包装
time_wrapper.h   ─→ <chrono> 包装
input_event.h    ─→ InputEvent 数据结构
string_utils.h   ─→ 字符串处理（标准库）
constants.h      ─→ 默认配置常量
```

## 代码组织关系

### 内部依赖
```
common/ 内全部平级，零内部依赖

每个 .h 独立自包含：
  noncopyable.h  ← 无依赖
  thread_pool.h  ← 标准库
  buffers.h     ← 标准库
  file_utils.h   ← 标准库 + nlohmann_json
  log_wrapper.h  ← spdlog
  time_wrapper.h ← 标准库 <chrono>
  input_event.h  ← 标准库 <string>
  string_utils.h ← 标准库 <string>
  constants.h    ← 标准库 <string>
```

### 被依赖热度（被其他模块引用的次数）
```
noncopyable.h  → 几乎所有单例继承
file_utils.h   → prosophor_core, virtual_sprite
log_wrapper.h  → 所有模块
input_event.h  → virtual_sprite（InputEvent）
constants.h    → prosophor_core
thread_pool.h  → prosophor_core
```

## 依赖
- **无**——仅标准库 + spdlog + nlohmann_json（均为外部库）

## 责任约束
- **不**引用任何其他 prosophor 模块
- **不**包含 UI、平台或业务逻辑概念
- 必须是纯可复用的独立代码

## 关键约定
- `prosophor` 命名空间
- 头文件同时包含声明和实现（inline 函数）
- 模板工具（`ThreadPool`）优于类型擦除包装

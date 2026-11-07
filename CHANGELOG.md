# Changelog

## [Unreleased] — 2026-11-07 本地语音（MeloTTS + SenseVoice）+ NutriBot 形态 + 自动更新 + 语音模块下沉

本期重点：语音子系统整体下沉为 `base/voice_interaction`（ASR/TTS/VAD/音频 I/O 统一管线，NutriBot 与 SDL 共用）；Windows SDL 新增 sherpa-onnx 本地语音（MeloTTS + SenseVoice，离线、可选 fp32）；新增 NutriBot（高通 QCS8550）形态；SDL 增加自动更新；构建系统统一目标名并改为能力探测选择 GPU 后端。

### 本地语音（Windows SDL，sherpa-onnx）
- **SherpaTtsProvider**（`base/voice_interaction/providers/tts/sherpa_tts_provider.*`）：MeloTTS VITS，44.1kHz 单声道，模型懒加载、进程内复用，输出 int16 PCM 交给既有缓存/播放链路
- **SherpaAsrProvider**（`providers/asr/sherpa_asr_provider.*`）：SenseVoice 一句话识别，优先 `model.int8.onnx`，可用 `sherpa_prefer_fp32` 切到 fp32
- **显式后端选择**：`TtsProviderFactory` / `AsrProviderFactory` 按配置原样选择；移除自动回退，后端不可用即报错返回 nullptr（NutriBot 板端需显式配置 `backend: aidvoice`）
- **移除 HTTP ASR 后端**（`http_asr_provider`）：ASR 仅保留 sherpa-onnx（Windows）与 aidvoice（NutriBot）
- 配置新增：`sherpa_model_dir` / `sherpa_speaker_id` / `sherpa_speed` / `sherpa_num_threads` / `sherpa_prefer_fp32`；设置面板增加后端、模型目录、语言、线程、fp32 开关（重启生效提示）
- 本地 ASR 自动关闭 interim（避免整句重复推理）；SDL 发送改为 `SendUserMessageAsync`，LLM 回合不再阻塞渲染（sprite 不卡）

### NutriBot（新增形态）
- `main_src/nutribot/`：高通 QCS8550 常驻语音营养助手（28 个文件）
  - 常驻回路：唤醒词、VAD 断句、ASR 并句、TTS 分句流式播报、用餐窗口、视觉感知、成员/菜品存储、机器人控制工具
  - LLM 走 aidgen NPU 本地推理；语音走 aidvoice（SenseVoice + MeloTTS）
- CMake：`PROSOPHOR_NUTRIBOT` 形态链接 aidgen/aidvoice/aidlite/opencv/ALSA，入口复用 `main.cc` 形态分支

### 语音模块下沉（base/voice_interaction）
- 从 SDL/core 抽取统一语音闭环：`VoiceInteraction`（provider 生命周期、预热、闭麦/两阶段退出）、`VoicePipeline`（VAD 状态机 + ASR/TTS worker + 有界队列）、`vad/`（WebRTC/能量）、`audio/`（ALSA、FrameQueue、重采样）、`rtc_wrapper/`（WebRTC VAD C 算法）
- ASR/TTS provider 迁入 `base/voice_interaction/providers/`；LLM provider 重组到 `prosophor_core/providers/llm/`；网络层下沉 `base/network/`
- `virtual_sprite/voice_engine.*` 缩为 SDL 门面，TTS 音频 LRU 缓存、分句器、播报净化由两个形态共用

### 自动更新（SDL）
- `virtual_sprite/update_handler.*`：启动异步检查 Gitee → GitHub Releases，弹窗展示版本与更新说明（SDL GUI 独有）

### 构建与配置
- Makefile：目标名统一（`build_tui` / `build_sdl` / `build_web` / `build_nutribot`），平台实现内部自适应；修复嵌套构建目录（`build/<variant>`）源码路径；恢复 `BUILD_DIR` 根变量派生变体目录
- GPU 后端改为能力探测：CUDA → Vulkan → CPU（`check_language(CUDA)` + `find_package(Vulkan)`），不再按平台写死
- 顶层 `project(... LANGUAGES C CXX)`：SDL 子工程首次 configure 需要 C
- 新增 `PROSOPHOR_BUILD_SHERPA_VOICE`（Windows SDL 默认 ON）：下载 sherpa-onnx v2.0.1 预编译运行时（shared-MT），安装 `sherpa-onnx-c-api.dll` / `onnxruntime*.dll`，NSIS 安装包同步收录
- `settings.json` 统一 UTF-8；`ProsophorConfig::ToJson` 完整序列化 `tts` / `asr` / `voice`（设置面板保存不再丢段）；仓库配置显式设置 `backend: sherpa-onnx` 与模型目录

### 其他
- 多用户 Web 服务端与前端仅做少量 include/接口适配（随目录重组）
- 文档：`docs/配置说明.md` 更新 TTS/ASR 后端、模型准备与 fp32 选项；README 中英文语音能力说明同步

## [Unreleased] — 2026-08-30 IM 多用户 Web 服务端 + React 前端 + 核心多用户改造 + base 目录重组

本期重点：新增独立多用户 Web 服务端 `prosophor_web`（HTTP + WebSocket，cpp-httplib），配套 Vite + React 聊天前端；核心层完成 IM 多用户改造——会话归属（owner/group/sender）、按用户/群分层记忆注入与提取、JSONL 按归属分目录；流式输出回调重构为增量 delta 传递 + 多前端广播；源码目录大重组——`common`/`media_engine`/`platform`/`voice` 归入 `base/`，`components/` 并入 `virtual_sprite/`。净变化 +5,349 行。

### Web 服务端（新增 prosophor_web 可执行文件）
- **web_server/ 模块**（~2,700 行）：独立于 SDL/UI 层、仅依赖 prosophor_core 的多用户服务端
  - **REST API**（`rest_api.cc`）：认证（register/login/logout/device 免注册）、`/api/me`、`/api/roles`、`/api/groups`（创建/加入/退出/成员/删除）、`/api/sessions`、`/api/sessions/{id}/messages`（`before_ts` 分页）、`/api/sessions/{id}/stop`、`/api/health`
  - **WebSocket**（`ws_handler.cc` + `web_channel.cc`）：帧协议双向收发（`web_protocol.cc`）
  - **用户/群组存储**（`user_store.cc` / `group_store.cc`）：token 签发（`token_ttl_hours`，默认 168h）
  - **会话历史读取**（`session_history.cc`）：JSONL 磁盘扫描，服务器重启后内存会话为空时兜底恢复会话列表/消息
  - **静态文件服务**（`web_server_app.cc`）：`web_root`（默认 exe 相邻 web-dist 目录）
- **设备免注册**：`/api/auth/device`（GET/POST）按 `device_id` 自动建号/复用并签发 token；认证同时支持 `Authorization: Bearer` 头与 `prosophor_token` cookie 双通道
- **`main_web.cc`**：入口支持 `--host` / `--port` 命令行覆盖（局域网访问 `--host 0.0.0.0`）；改用 `Platform::SetConsoleSignalHandler()` 优雅停机
- **配置**：settings.json 新增 `web` 段（enabled/host/port/web_root/role/data_dir/token_ttl_hours）；`WebConfig` 完整序列化往返；`host` 默认值改为 `0.0.0.0`

### IM 渠道层（im_gateway → web_server）
- **ImChannel 抽象**（`channel.h`）：`ImMessage` / `ImChatContext` 统一消息模型；新增渠道（微信/钉钉/Telegram…）= 实现 ImChannel 子类 + 注册，核心层不感知具体协议
- **ImGateway → WebGateway**（`web_gateway.cc`）：维护 chat → AgentSession 映射，`EnsureSessionForChat` 找/建会话（同 role + 同 owner + 同类型复用）、输出回调路由回渠道
- **会话/历史多用户化**：日志目录 `sessions/{role}/{owner}/{date}.jsonl` 按归属分目录（免行级过滤、免写锁竞争），JSONL 新增 `owner`/`group`/`session_type`/`sender` 字段

### Web 前端（web/，Vite + React）
- **`web/src`**（~1,600 行）：React 18 + TypeScript + Vite + zustand + react-router
  - `pages/ChatPage.tsx`：聊天页（RoleSidebar 角色栏 + MessageList 消息列表 + ChatInput 输入框）
  - `api/rest.ts` + `api/ws.ts`：REST 认证/会话 API + WebSocket 帧协议客户端
  - `stores/`：auth（设备免注册登录流）/ chat（会话状态）zustand store
- **构建集成**：CMake `PROSOPHOR_BUILD_WEB_FRONTEND=ON` 时自动 npm ci + build（默认 OFF，不阻塞无 Node 环境）；Makefile 新增 `build_web` / `run_web` / `clean_web`

### 核心多用户改造
- **会话归属**（`agent_session.cc/h`）：新增 `owner_id` / `group_id` / `SessionType`（direct/group）；消息携带 `sender_id`/`sender_name`（消费式 `SetCurrentSender`，读取后自动清空不泄漏到下一条）
- **身份净化**（`session_key.h` 新增）：`SanitizeIdentity`（白名单 `[a-z0-9_-]`、长度 ≤64、大写转小写）防路径注入；`IsPathWithin` 防 `../` 逃逸
- **记忆分层注入**（`agent_session_manager.cc` BuildSystemPrompt）：私聊注入 `memory/users/{owner}/rules → profile → preferences`（规则 > 事实 > 偏好）；群聊注入 `memory/groups/{gid}/rules → profile` + 活跃成员事实性 profile（截断 200 字符，不注入私人规则/偏好）
- **记忆提取重构**（`memory_manager.cc`）：分类从 design_decision/code_change/lesson_learned/unresolved_issue 改为 fact/rule/preference/experience（旧类型自动归一化）；提取 prompt 新增 SENDER/SCOPE 归属字段；`AppendDecisionsToMemory` 按归属路由写入用户/群/角色记忆目录，无法归属时降级写角色 experiences（隐私下限）
- **批量发送策略**（`StartChain`）：批内 sender 全相同 → 合并为一次 LLM 调用（原有行为）；群聊混合 sender → 逐条 Loop 保持每条消息的发言归属
- **崩溃兜底**：StartChain 捕获 Loop 异常并重置 `task_active_` 标志、向前端广播错误——线程池吞异常导致会话"只进缓冲、永不回复"的死锁不再可能
- **会话复用收紧**：`GetOrCreateSession` 必须同 role + 同 owner + 同会话类型且活跃才复用，多用户下不再跨用户复用

### 输出回调 delta 重构
- **流式增量**：`SessionOutputCallback` 新增 `delta` 参数——`STREAM_CONTENT_TYPING` / `STREAM_THINKING` 帧 reply 为空、delta 携带本帧增量，终结帧 reply 为全量消息（`agent_core.cc` 不再逐帧构造 MessageSchema，session 侧直接累积 delta）
- **多前端广播**：`AgentEngine::AddOutputCallback()` 支持注册多个回调（桌面 + Web 并存）；`SetOutputCallback` 保持覆盖式；Session 持有 fan-out 闭包转发
- **TUI 适配**（`ai_coding.cc`）：流式 thinking/正文输出改用 delta 参数，不再依赖 reply 回显

### 其他核心修复
- **PermissionManager 死锁修复**：确认回调拷贝后在锁外执行（回调内再进 PermissionManager 不再死锁）；`SetConfirmCallback` 加锁、`std::optional` 存储
- **HttpClient**：新增 `Put()`（`PerformBodyRequest` 统一封装）
- **WebSocketClient::Recv**：新增 `bytesleft` 出参（分片读取场景）
- **Platform 扩展**：`SetConsoleSignalHandler()`（Windows Ctrl+C/Break/关窗、POSIX SIGINT/SIGTERM 统一回调）、`GetLanAddresses()`（Windows GetAdaptersAddresses / POSIX getaddrinfo 枚举局域网 IPv4，链接 iphlpapi）
- **config.cc**：加载时统一走 `ExpandEnvInJson` 展开环境变量

### 目录重组（base/）
- **`main_src/common/` → `main_src/base/common/`**：工具库整体迁移
- **`main_src/media_engine/` → `main_src/base/media_engine/`**：media 与 sdl_interface 分别按 audio/font/texture 子目录组织
- **`main_src/platform/` → `main_src/base/platform/`**
- **`main_src/voice/` 拆分**：VAD/噪声抑制/resampler → `base/rtc_wrapper/`；voice_engine → `virtual_sprite/voice_engine`
- **`main_src/components/` → `main_src/virtual_sprite/components/`**：UI 组件层并入 SDL 前端（19 个文件 2,006 行纯移动）；`agent_engine.h` 移除对 `components/ui_types.h` 的依赖（核心层与 UI 层解耦）
- **README.md**：目录结构章节同步更新

### 配置与构建
- **CMake**（`main_src/CMakeLists.txt`）：新增 `prosophor_web_server` 静态库 + `prosophor_web` 可执行文件；cpp-httplib 走 SYSTEM include 规避 -Werror；`--allow-multiple-definition` 解决 llama-common vendor httplib 符号冲突；新增 iphlpapi 链接
- **角色配置**：sayu spritesheet `sayu.png` → `sayu.webp`；skills_creator spritesheet → `skirk-2.webp`
- 移除 `config/.prosophor/workspace/AGENTS.md`

### 文件统计
- 变更文件：210 个（含目录重组移动）
- 新增：+7,621 行（其中 ~1,600 行 Web 前端、~2,000 行 components 随迁）
- 删除：-2,272 行（其中 ~2,000 行为 components 目录迁移）
- 净变化：+5,349 行

---

## [Unreleased] — 2026-07-16 移除计算机整理面板 + 角色面板去保护 + 布局微调

本期重点：彻底移除整个"计算机整理"（Disk Cleanup）功能模块——删除 503 行 `computer_organize_view.cc`、清理所有关联 i18n 翻译键、移除侧边栏导航项和路由代码；角色面板不再设"保护角色"（`disk_cleaner`、`skills_creator` 不再强制启用/不可删除），所有角色一视同仁；聊天侧边栏角色列表间距微调。净变化 +12 / -574 行。

### 移除：计算机整理面板
- **`computer_organize_view.cc`**（`-503` 行）：删除整个缓存扫描/清理面板，包含：缓存扫描引擎（`DoScan`/`ComputeDirSize`/`CheckCacheDir`）、缓存分类展示（系统/浏览器/聊天/IDE/开发工具 5 大类）、扫描进度 UI、单条/分类/全选删除、AI 扫描报告自动发送、`FmtSize`/`BuildScanReport` 等辅助函数
- **侧边栏导航移除**（`sidebar.cc` / `sidebar.h`）：删除 `NavItem::ComputerOrganize` 枚举值和导航条目渲染
- **ChatWindow 路由移除**（`chat_window.cc` / `chat_window.h`）：删除 `RenderComputerOrganizeView` 声明、`switch` 路由分支、以及点击导航自动切换 `disk_cleaner` 角色的 `SetOnNav` 回调
- **国际化清理**（`en.json` / `zh-CN.json`）：各删除 18 条计算机整理相关翻译键（导航标签、视图标题、5 类缓存名、扫描/清理按钮、进度提示、Agent 提示等）

### 角色面板精简
- **去保护机制**（`roles_view.cc`）：移除 `disk_cleaner` 和 `skills_creator` 的"保护角色"逻辑——所有角色现在均可自由取消勾选和删除；移除"该角色受保护"（`role_protected`）的文字提示；`checked` 状态不再受 `is_protected` 强制覆盖
- **disk_cleaner 角色配置更新**（`disk_cleaner.json`）：LLM 从 `ollama/qwen3:8b` 切换为 `openai/deepseek-v4-flash`；TTS 语音设为 `"none"`（关闭语音）

### 布局微调
- **Right Panel 间距优化**（`chat_view.cc`）：角色面板顶部 Y 偏移从 `22.0f` 收窄至 `8.0f`；标题与分割线间距从 `6.0f` 缩至 `4.0f`；新增 `ItemSpacing(4.0f, 2.0f)` 样式变量；移除角色卡片间的垂直间距（`4.0f * sm` → `0`），整体更紧凑

### 文件统计
- 变更文件：10 个
- 新增：+12 行
- 删除：-574 行
- 净变化：-562 行

---

## [Unreleased] — 2026-07-05 Platform API 扩展 + 计算机整理跨平台重构 + 构建修复

本期重点：`Platform` 层新增 `ExpandEnv()` / `GetWellKnownCacheDirs()` API，将计算机整理面板的缓存目录定义和变量展开逻辑上提到平台层，移除 `#ifdef _WIN32` 条件编译实现跨平台支持；构建系统移除 `IMGUI_USE_WCHAR32` 编译定义并修复 `BUILD_SHARED_LIBS` 作用范围。净变化 +168 / -150 行。

### Platform API 扩展
- **`ExpandEnv()`**（`platform.cc` +85 行）：新增跨平台环境变量展开方法，Windows 支持 `%LOCALAPPDATA%` / `%APPDATA%` / `%USERPROFILE%` / `%TEMP%` / `%WINDIR%`，Unix 支持 `$HOME` / `$XDG_CACHE_HOME`
- **`GetWellKnownCacheDirs()`**（`platform.cc` +42 行）：返回已知的主流应用缓存目录列表（系统/浏览器/聊天/IDE/开发工具 5 大类），Windows 30+ 项，Unix 15 项

### 计算机整理面板重构
- **跨平台化**（`computer_organize_view.cc` -112 行）：移除面板内联的 `ExpandEnv()` 函数和 `kCacheDefs` 静态数组，改用 `Platform::ExpandEnv()` / `Platform::GetWellKnownCacheDirs()`；移除 `#ifdef _WIN32` / `#ifndef _WIN32` 条件编译和对应的 stub 实现

### 构建系统修复
- **`BUILD_SHARED_LIBS` 作用域修正**（`CMakeLists.txt`）：仅 Linux 下强制 OFF，Windows 下不再覆盖，防止干扰 SDL 共享库构建
- **移除 `IMGUI_USE_WCHAR32`**：从 `imgui_lib` / `imgui_widget_lib` / 主目标三个位置移除该编译定义
- **修复重复 `nlohmann_json` 链接**：`link_libraries` 中移除重复条目

### 其他
- **`settings_window.cc`**：精简 ProvidersTab 渲染代码，移除不必要的中间变量
- **`curl_client.h`**：`SseStreamHandler::Buffer()` 改为非虚方法（移除 `override` 关键字）
- **`audio_capture.h`**：修复注释编码

---

## [Unreleased] — 2026-07-05 记忆面板独立 + 计算机整理重写 + 日志面板重写 + 角色面板精简

本期重点：新增独立记忆浏览面板，计算机硬盘整理工具完全重写为主流程序缓存扫描，日志面板去掉缓存层改为实时 ring buffer 渲染，角色面板大幅精简移除嵌入的记忆子面板。导航结构重新组织（Roles/Memory 移至 Capabilities 组）。净变化 +1,259 / -626 行。

### 面板重构
- **记忆面板独立**（`memory_view.cc` +464 行）：新增 `NavItem::Memory`，侧边栏 Capabilities 组新增记忆导航项，记忆浏览从角色面板移至独立面板，支持按角色/日期/类型查询记忆，查看全文，直接打开文件
- **角色面板精简**（`roles_view.cc` -500 行）：移除嵌入的记忆管理子面板和大量静态变量，专注角色配置管理
- **计算机整理重写**（`computer_organize_view.cc` +703 / -465 行）：移除旧磁盘信息展示，改为主流程序缓存扫描——浏览器缓存（Chrome/Edge/Firefox/Opera）、聊天工具（WeChat/QQ）、IDE、开发工具、系统日志/DNS 缓存；新增分类图标和大小排序
- **日志面板重写**（`logs_view.cc`）：移除 `LogCache` 中间缓存层，直接基于 ring buffer 实时渲染；新增复制按钮（`log_copy`，按过滤器复制到剪贴板）；清除逻辑改为锚点偏移（`log_clear_anchor_`），不再销毁缓存
- **删除旧面板**：移除 `scheduler_view.cc` 中的日志跳转引用

### 导航与组件
- **侧边栏重构**：`NavItem::Roles` 和 `NavItem::Memory` 从 Settings 组移至 Capabilities 组（`sidebar.cc`）
- **ItemList 组件增强**：`Item()` 新增 `disabled` 参数，支持禁用 checkbox（灰色不可交互，`item_list.cc/h`）

### 配置系统
- **`enable_summary` 默认关闭**：`settings.json` 和 `config.cc` 默认值从 `true` 改为 `false`
- **角色切换同步**：`SwitchRoleForSession` 时以全局 `enable_summary` 覆盖角色配置（`agent_session_manager.cc`）
- **多模态模型支持**：llamacpp 配置新增 `mmproj_path` 字段（多模态投影文件路径，`config.cc/h`）

### 代理核心
- **AgentEngine 扩展**：新增 `GetMemoryManager()` 方法暴露内存管理器引用（`agent_engine.h`）
- **AgentRole 调整**：`enable_summary` 默认值从 `true` 改为 `false`（`agent_role.h`）

### 媒体引擎
- **键盘 API**：`ImGuiWidget` 新增 `IsKeyDown` / `IsKeyPressed` 静态方法；新增 `ImGuiKey_LeftCtrl` / `RightCtrl` / `C` / `A` 键盘常量（`imgui_widget.h`）
- **颜色系统**：`Color::Slot` 新增 `TextDisabled = 1` 槽位（`colors.h`）

### 国际化
- `en.json` / `zh-CN.json`：新增日志复制按钮、记忆面板导航等翻译条目

### 其他
- `.gitignore` / `README.md`：文档和忽略规则更新

---

## [Unreleased] — 2026-06-29 记忆系统重构 + 自动更新 + 面板全面增强 + Token 用量统计

本期重点：记忆系统从 managers 层迁移至 core 层统一管理；新增自动更新检查/UI 机制；Usage/Roles/Logs/Security 面板全面增强；Token 跟踪系统大幅升级；新增 Skill Creator 系统；精灵模块重构精简。净变化 +2,740 / -1,723 行。

### 记忆系统重构（核心变更）
- **移除旧实现**：删除 `managers/memory_manager` 和 `core/memory_consolidation_service`
- **新建统一记忆管理器**：`core/memory_manager`（+515 / +160 行），将记忆管理集中到 core 层
- **AgentSession 扩展**：新增相关记忆操作方法（`agent_session.cc/h`）

### 自动更新机制（新增）
- **`updater/update_checker.cc/h`**（+209 / +56 行）：实现自动更新检查逻辑
- **`update_handler.cc/h`**（+88 / +19 行）：处理更新 UI 交互
- **virtual_sprite.cc**：主入口接入更新功能

### 面板全面增强
- **UsageView**（`usage_view.cc`）：大幅重构，+510 行，Token 用量统计展示
- **RolesView**（`roles_view.cc`）：+365 行，角色管理面板大幅增强
- **LogsView**（`logs_view.cc`）：+131 行，日志面板改进
- **SecurityView**（`security_view.cc`）：+54 行，安全设置面板增强
- **删除旧面板**：移除 `memory_view.cc` 和 `scheduler_view.cc`
- **ConfigView**：精简移除冗余配置项

### Token 跟踪系统
- `token_tracker.cc/h`：Token 用量统计与跟踪能力大幅增强（+109 / +19 行）

### Skill Creator 系统（新增）
- `config/.prosophor/active/skill_creator/`：技能创建器，含 ACTIVE.md、trigger.py、trigger_mode.cfg
- 新增资源 `preview.png`

### 媒体引擎改进
- `media_core.cc`：+59 行关键媒体核心逻辑更新
- `sdl_common.cc`、`imgui_widget.cc/h`、`media_core.h`：相应适配

### 国际化
- `en.json` / `zh-CN.json`：各 +16 行，新增/修改翻译条目

### 工具类增强
- `file_utils.cc/h`：+29/+27 行，新增文件工具方法
- `log_wrapper.h`：+20 行，日志功能增强

### 组件与 UI
- `sidebar.cc`：+43 行，侧边栏功能增强
- `chat_panel.cc`：+31 行，聊天面板改进
- `item_list.cc`、`selectable_item.cc`：列表和可选择项改进
- `sprite.cc/h`：大幅精简（-120 行），精灵重构
- `asset_define.h`：+14 行，新增精灵资源定义

### 代理核心改进
- `agent_core.cc/h`：新增内省方法
- `agent_role.h`：角色定义扩展
- `role_config_manager.cc`、`agent_role_loader.cc`：角色配置加载优化
- `subagent_coordinator.cc`、`command_registry.cc`：命令注册与子代理协调改进

### 构建系统
- `CMakeLists.txt`（根目录和 main_src）：添加新模块编译支持
- `Makefile`：+12 行构建规则
- `tests/CMakeLists.txt`：+10 行测试配置

---

## [Unreleased] — 2026-06-21 llamacpp 重构 + Provider 体系增强 + README 文档完善

本期重点：llamacpp Provider 大规模重构——移除 PIMPL 模式、`PROSOPHOR_HAS_LOCAL_MODEL` 条件编译下沉、使用 `common_chat_templates_apply` 渲染 prompt；全 Provider 体系新增 `thinking_budget_tokens` / `reasoning_effort`；新增 Tool Call 流式事件；会话系统新增 `enable_tools` 控制；README 文档全面升级新增环形缓冲区图解与 SDL 演示图。净变化 ~1,120 行，4 张新图片。

### llamacpp Provider 重构（~+850/-500 行）
- **去 PIMPL 模式**：`Impl` 结构体展开为 `LlamacppProvider` 直接成员（`model_`/`ctx_`/`sampler_`/`vocab_`/`chat_tmpls_`），便于单元测试
- **`PROSOPHOR_HAS_LOCAL_MODEL` 下沉**：provider 源码中移除条件编译宏，CMake 层在 `NOT PROSOPHOR_BUILD_LLAMA` 时从源列表过滤 `llamacpp_provider.cc`，router 层 `#ifdef` 保护
- **Chat Template Prompt 渲染**：新增 `BuildChatParams()` 使用 `common_chat_templates_apply()` 渲染 prompt，支持 Jinja 模板、thinking 标记、tool choice
- **结构化 Prompt 构建**：拆分为 `BuildSystemPrompt()` / `BuildMessagePrompt()` / `BuildToolsPrompt()` / `BuildFallbackPrompt()` 四个方法
- **Thinking 标记自动检测**：从模型 chat template 自动解析 `reason_start`/`reason_end`，配置化 fallback（`thinking_start`/`thinking_end`）
- **Tool Call 标记**：新增 `tool_call_start`/`tool_call_end`/`end_of_turn`/`start_of_turn` 配置字段，`ProcessTokenFrame()` 检测标记并发出流事件，`ParseToolCalls()` 解析 `call:name{...}` 格式
- **方法拆分**：`TokenizePrompt()` / `PrefillPrompt()` / `GenerateReply()` / `Release()` 独立方法
- **`thinking` 布尔值 → 字符串标记**：`LlamacppModelConfig` 中 `thinking` 替换为 `thinking_start`/`thinking_end` 等字符串

### Provider 体系通用增强
- **`ChatRequest` 新字段**：`thinking_budget_tokens`（默认 4096）、`reasoning_effort`（默认 "medium"）
- **`ChatResponse` 重构**：移除 `has_thinking` 字段，使用 `!content_thinking.empty()` 判断；`AddThinking()`/`AddText()` 改为覆写赋值，新增 `AppendThinking()`/`AppendText()`
- **`StreamEvent` 新增**：`kToolStart` / `kToolDelta` / `kToolEnd` 三个流式工具调用事件
- **Anthropic Provider**：thinking budget 动态计算（`max_tokens / 2`），移除 hardcoded 4096
- **OpenAI Provider**：移除硬编码 `ThinkingToReasoningEffort()`，使用配置的 `reasoning_effort`
- **所有 Provider**：`PrintRequestLog()` 增加详细日志（max_tokens / temperature / messages / tools / thinking 等）；LLM 基类 `ExecuteStream()` / `Chat()` 新增 request body debug 日志
- **Stream Handler**：所有 handler 移除 `has_thinking = true` 赋值

### 配置文件升级
- **`ModelConfig`**：新增 `thinking_budget_tokens` / `reasoning_effort`
- **`LlamacppModelConfig`**：`thinking` 布尔替换为 `thinking_start` / `thinking_end` / `tool_call_start` / `tool_call_end` / `end_of_turn` / `start_of_turn` 字符串标记
- **`config.cc`**：序列化/反序列化支持所有新字段
- **`settings.json`**：llamacpp 模型配置新增六个标记字段

### 角色与会话系统
- **`AgentRole`**：新增 `thinking_budget_tokens` / `reasoning_effort` / `enable_tools` 字段
- **`AgentRoleLoader`**：解析新字段；`enable_tools=false` 时自动清空工具列表
- **`AgentSession`**：`use_tools_` 从角色 `enable_tools` 初始化；`ApplyProviderOverride` 不再覆盖 thinking 字段
- **`AgentCore::BuildRequest`**：传递 `thinking_budget_tokens` / `reasoning_effort`
- **`AgentCore::Loop`**：处理 `kToolStart` / `kToolDelta` / `kToolEnd` 流事件
- **`AgentSessionManager::BuildSystemPrompt`**：新增行为指令"执行完一步后立刻进入下一步，不要重复确认"
- **`RoleConfigManager::HotReload`**：支持 `enable_tools` / `thinking` / `enable_streaming` / `max_iterations` 等字段热重载

### 角色 JSON 批量更新
- 所有 13 个角色新增 `thinking_budget_tokens`（4096）/ `reasoning_effort`（"medium"）/ `enable_tools`（true）
- architect / coder / reviewer / teacher 等角色启用 `thinking: true`
- coder 角色 `soul` 重写为更详细的编码专家描述

### UI / 面板
- **`roles_view.cc`**：新增 thinking / thinking_budget_tokens / reasoning_effort / enable_tools 编辑字段
- **`sprite.cc`**：`STREAM_TOOL_START` / `STREAM_TOOL` / `STREAM_TOOL_END` 映射到 SPRINT 动作
- **`status_bar.cc`**：tool 流状态显示 ⚙ 图标
- **`ui_renderer.cc`**：精灵右键菜单新增"启用工具"/"禁用工具"切换（`session->SetUseTools()`）
- **语言文件**：en / zh-CN 新增 `ctx_enable_tools` / `ctx_disable_tools`

### README 文档升级
- **新增 FixedBuffer 环形缓冲区文档**：三种运行状态（填充中/已满/回绕）配示意图 `ringbuf_case_1/2/3.png`，同步展示核心源码与逐 case 行为说明
- **新增 SDL 桌面精灵展示图**：`show.png` 配图置于 SDL Desktop Pet UI 章节
- **清理过期图片引用**：移除已删除的 `demo.png`、`win_demo.png` 引用
- **中文版同步更新**：替换外部 GitHub 图片链接为本地图片，内容与英文版保持一致

### 平台 / 其他
- **`platform.cc`**：`setlocale(LC_ALL, "en_US.UTF-8")` 修复 msys2/mingw UTF-8 输出
- **`platform.h`**：移除未使用的 `HomeDir()` 声明
- **`websocket_client.cc`**：`ApplyProxyFromEnv()` 读取 HTTPS_PROXY / HTTP_PROXY / NO_PROXY 环境变量，解决 Windows libcurl 小写环境变量读取问题
- **`i18n.cc`**：语言文件路径改为从 `$HOME/.prosophor/lang/` 加载，支持用户自定义翻译
- **`CMakeLists.txt`**：OpenSSL 在 CURL 之前 `find_package`；llamacpp 条件编译过滤
- **`ai_coding.cc`**：CLI 终端 tool 流状态彩色指示（绿/灰/红点）

## [Unreleased] — 2026-06-14 组件体系独立 + Provider 配置重构 + 字体系统升级 + 面板全面完善

本期重点：从 `panel_helpers` 中提取出独立的 `components/` 通用 UI 组件层（BorderedContainer/ItemList/SelectableItem/PanelKit），新增组件架构文档；Provider 配置重构（`thinking` 字段从 entry 级别下放到 role 级别，`gpu_enable`→`n_gpu_layers` 标准化）；字体系统全面升级（DroidSans 英文内嵌 + Emoji 字体加载 + FreeType 颜色渲染）；新增 skills_creator 角色；Workspace 路径配置；多个面板大幅重写（memory/providers/roles/skills/tts/config）。净变化 +16,964 行。

### 组件体系独立 (components/)
- **新增 `components/` 模块**：从 `panel_helpers` 提取为独立可复用 UI 控件层，新增 `CLAUDE.md` 架构文档
- **BorderedContainer**（新增 `bordered_container.h/cc`）：通用带边框容器，支持圆角/直角、可选底色、自定义边框样式
- **ItemList / SelectableItem**（新增 `item_list.h/cc`、`selectable_item.h/cc`）：可选中列表组件，Y 轴追踪 + InvisibleButton + 三态视觉（悬停/选中/点击），带分类头部
- **PanelKit**（`panel_helpers.cc` → `panel_kit.cc` + 新增 `panel_kit.h`）：
  - Card 核心体系：`Card(x,y,w,title,...)` 参数化配色，`ChannelsSplit` 正确图层顺序
  - `WhiteCard`（白底+奶油边）、`FocusCard`（淡橙+暖橙，三态交互）、`StatCard`（左侧色条）
  - `SplitPanel`：左右分栏坐标计算
  - `ActionBar`：底部 Save/Cancel 按钮栏
  - `PlaceholderPage`、`SectionScroll` 等辅助组件
- **Sidebar 适配**：改用 `SelectableItem` 实现导航项渲染；`IsNavItemActive()` 函数化；侧边栏分组可折叠

### Provider 配置重构
- **Thinking 下放到 Role 级别**：所有 provider entry 移除 `thinking` 字段，改为角色 JSON 中 `llm.thinking` 控制（`true`/`false`）
- **移除 entry→model thinking 传播**：`config.cc` 删除 `entry_thinking` → `model_config.thinking` 传播逻辑
- **`gpu_enable` → `n_gpu_layers` 标准化**：配置文件和序列化统一使用 `n_gpu_layers`（`-1`=全部GPU, `0`=CPU），向后兼容 `gpu_enable` 布尔值
- **llamacpp 新增参数**：`cpu_moe`（MoE 专家卸载到 CPU）、`no_mmap`（禁用内存映射加载）
- **`auto_start` 提升到 entry 级别**：entry 级别控制 `auto_start`，首个模型继承 entry 值，后续模型默认为 false
- **settings.json**：所有 provider 移除 `thinking` 字段；llamacpp 配置扁平化，模型名去掉 `.gguf` 后缀

### 字体系统全面升级
- **DroidSans 英文内嵌**（新增 `droid_sans_font.h`，15,843 行 TTF 数据）：作为基础英文字体，干净美观
- **Emoji 字体加载**（`LoadSharedEmojiFont()`）：跨平台搜索 Segoe UI Emoji / Noto Color Emoji / Apple Color Emoji
- **FreeType 颜色渲染**：引入 `imgui_freetype.h`，`ImGuiFreeTypeLoaderFlags_LoadColor | Bitmap` 加载彩色 emoji
- **字体层级**：`DroidSans(英文基础) → CJK(中文合并) → Emoji(表情合并)`，三层堆叠正确渲染
- **`SetWindowFontScale()`**：新增按窗口独立字体缩放 API
- **移除 `AddFontDefault()` 回退**：不再 fallback 到 ImGui 默认字体

### 新增 skills_creator 角色
- **`roles/skills_creator.json`**：专门创建和编辑 SKILL.md 技能定义的 AI 助手角色
- 配置 `deepseek-v4-flash` 模型，`thinking=true`，纯对话式技能设计

### 角色配置更新
- **多个角色启用 thinking**：architect、coder、reviewer、teacher 角色新增 `"thinking": true`
- **coder TTS 语音关闭**：`"voice": "zh-CN-YunxiNeural"` → `"voice": "none"`，角色级 TTS 静音
- TTS 全局配置始终包含 `"none"` 选项，支持按角色关闭语音

### Workspace 路径配置
- **`config.workspace_path`**：新增配置项，会话初始化时优先使用此路径作为工作目录
- **`AgentSessionManager`**：`CreateSession()` / `SwitchRoleForSession()` 均读取 `workspace_path` 设置工作目录
- `settings.json` 默认空字符串（使用当前目录）

### 面板完善与重写
- **MemoryView 重写**（`memory_view.cc` +256/-6 行）：新增 "Daily Memory" / "Role Memory" 双标签页，使用 ItemList + BorderedContainer 布局
- **ProvidersView 重写**（`providers_view.cc` +163/-43 行）：新增 `ProviderEntryCard` 组件（BorderedContainer + 字段 + ModelCard），支持折叠展开、添加模型、模型卡片列表
- **RolesView 重写**（`roles_view.cc` +202/-91 行）：使用新版 ItemList + Card 布局，内联编辑字段
- **SkillsView 重写**（`skills_view.cc` +243/-92 行）：使用 ItemList + BorderedContainer + SplitPanel，左侧技能列表 + 右侧详情
- **TTSView 增强**（`tts_view.cc` +84/-14 行）：使用 Card 组件重构，测试发音、ASR 心跳检测
- **ConfigView 更新**（`config_view.cc` +91/-65 行）：适配新增配置字段
- **ChatView**：移除表情按钮调试输出
- **ModelCard / ProviderEntryCard**（新增 `model_card.h/cc`、`provider_entry_card.h/cc`）：可复用的模型/提供商卡片组件

### PanelContainer 增强
- **`scroll_region`**：新增可滚动内容区域支持
- **`scroll_to_bottom`**：自动滚动到底部
- **`AddChild()` / `ClearChildren()`**：多子窗口管理
- 新增 `SplitLeft()` / `SplitRightExt(width, w2)` 分割变体

### panel_helpers 重构
- **移除旧 Section/Helper**：删除 `SectionForm`、`LabelRow`、`SectionCard`、`Spacing`、`CardBox` 等旧助手
- **替换为 Card 体系**：所有面板统一使用 `WhiteCard` / `FocusCard` / `StatCard` / `SplitPanel` / `ActionBar`
- **`SectionScroll`** 移除（由 `PanelContainer::scroll_region` 替代）

### i18n 国际化更新
- 新增键：`general_workspace_path`、`btn_send`、`sidebar_group_agents/capabilities/monitor/settings`、`tab_daily_memory/role_memory/skills`
- `panel_no_data` 增加 📭 emoji
- 中英文翻译同步更新

### Voice Engine 增强
- **播放完成回调**：新增 `on_finished_callback`，音频播放完成后通知
- **空闲停止**：`VoiceChannel` 检测 `is_active` 状态，空闲时自动停止音频线程

### Llamacpp Provider 增强
- **多模型 `auto_start` 逻辑**：仅首个模型 `auto_start=true`，后续模型为 `false`
- **配置读取增强**：`model_path` 可选（允许多模型在配置中有不同路径）

### 其他改进
- `command_registry.cc`：`ExtractAutoSwitchRole` 更新匹配逻辑
- `agent_session.cc`：修复 `GetRoleId()` 返回空角色 ID 的问题
- `virtual_sprite.cc`：WindowConfig 调整 SDL 窗口标志位
- `window.cc`：移除 `#ifndef` 字体选择条件编译
- `llamacpp_provider.cc`：修复 `model_path` 配置读取

### 文件统计
- 变更文件：70 个
- 新增：+17,868 行
- 删除：-904 行
- 净变化：+16,964 行

---

## [Unreleased] — 2026-06-12 电脑整理面板 + 角色配置重构 + 响应式侧边栏 + 桌宠延迟管理

本期重点：新增"电脑整理"面板（磁盘扫描、大文件/临时文件/备份文件检测，可回收空间估算）；角色 JSON 配置统一重构（`display_name`→`role_name`，`personality_prompt`→`soul`，`spritesheet_path`→`spritesheet`）；Sidebar 从固定像素改为窗口比例响应式宽度；SprriteManager 新增延迟创建/销毁队列安全跨帧管理精灵生命周期；MediaCore 新增 `DestroyMediaWindow` 窗口销毁 API；Provider 面板 UI 重构（折叠模型卡片、统一字段布局、添加模型按钮）；InputPanel 自适应按钮大小与圆角优化。净变化 +845 行。

### 电脑整理系统
- **ComputerOrganize 面板**（新增 `panels/computer_organize_view.cc`）：磁盘分区选择（已用量/总量标签）、扫描大文件/临时文件/备份文件、取消扫描、可回收空间汇总
- **disk_cleaner 角色**（新增 `config/roles/disk_cleaner.json`）：专用磁盘清理 AI 角色，qwen3:8b 模型，配合 Organize 面板使用
- **sidebar 导航**：新增 `NavItem::ComputerOrganize` 条目 + i18n（`nav_computer_organize` / `view_computer_organize`）
- **导航自动切角色**：点击 "电脑整理" 时自动切换会话角色为 `disk_cleaner`

### 角色配置重构
- **字段重命名**（所有 12 个角色 JSON 文件）：`display_name` → `role_name`，`personality_prompt` → `soul`，`spritesheet_path` → `spritesheet`
- **新增字段**：所有角色新增 `llm.enable_streaming`；Roles 面板编辑新增 `role_name`、`spritesheet`、`soul`、`max_iterations`、`enable_streaming` 字段
- **内部重命名**：`AgentRole::personality_prompt` → `soul`，`ChatPanel::assistant_display_name_` → `assistant_role_name_`，`SpeechBubble::SetAssistantDisplayName` → `SetAssistantRoleName`
- **RoleConfigManager**：新增 `SaveFieldInt` 方法；`HotReload` 适配 `soul`/`role_name` 字段

### 响应式 Sidebar
- **比例宽度**：`LayoutConfig::sidebar_width_expanded` 替换为 `sidebar_width_ratio`（默认 0.12），`GetWidth()` 改为计算值
- **渲染更新**：`Sidebar::Render` 新增 `win_w` 参数
- **选中项样式**：统一为橙色左侧竖条 + 淡橙底色，不再使用 `Selection()` 函数

### SpriteManager 延迟操作
- **安全生命周期**：新增 `QueueCreateSprite` / `RemoveSpriteByRoleId` 方法，将创建/销毁延后到下一帧 `ProcessPendingOps` 处理
- **Role 保存联动**：Roles 面板 Save 时自动创建/销毁对应桌宠

### MediaCore 窗口管理
- **DestroyMediaWindow**：新增窗口销毁 API（移除 handler + `all_windows_` 条目），用于桌宠精灵窗口清理
- **Window 析构**：增加 SDL_Renderer / SDL_Window 显式资源释放
- **CreateMediaWindow**：修复缺失的 ImGuiContext 还原

### ImGuiWidget 扩展
- **TreeNodeEx** / **IsTreeNodeOpen**：新增树节点控制 API
- **ImGuiChildFlags**：公开 `Borders` / `AutoResizeY` 标志
- **Style**：新增 `PushVar_FrameRounding`

### Provider 面板 UI 重构
- **折叠模型卡片**：模型区域改为可折叠（`+`/`-` 切换），节省面板空间
- **统一条目布局**：每个 Entry 使用 `ScopedChild` 边框包装，`field_row` 标签 + 控件对齐
- **添加模型按钮**：条目内新增 "Add Model" 按钮

### UI 视觉统一
- **选中项样式统一**：Sidebar、ConfigView、ProvidersView、RolesView 统一为 "橙条左竖线 + 圆角淡橙底" 选中样式
- **InputPanel**：输入框比例 `0.82→0.88`，按钮宽度自适应，新增 `FrameRounding(6f)` 圆角
- **LayoutConfig**：`input_area_height` 100→56，新增 `panel_left_list_w`（200），`card_widget_offset` 140→200

### i18n 更新
- 中/英文新增 `nav_computer_organize`、`view_computer_organize`、`computer_organize_select_drive`

## [Unreleased] — 2026-06-10 UI 架构重构：侧边栏导航 + 多面板仪表盘 + 运行时角色管理

本期重点：UI 从单体式聊天窗口重构为侧边栏驱动多面板仪表盘（19 个导航项、18 个独立面板视图）；SettingWindow 从 ChatWindow 完全提取为独立模态窗口；新增 RoleConfigManager 运行时热切换角色模型/TTS 语音；Platform 层新增 7 个跨平台 API；布局常量全面迁移至 LayoutConfig 集中管理；新增字体缩放支持（`font_scale`）。净变化 +2,702 行。

### 侧边栏导航系统
- **Sidebar 组件**（新增 `components/sidebar.h/cc`）：左侧可折叠导航面板
  - 19 个 NavItem 分为 5 组：Capabilities/Monitor/Agents/Store/Settings
  - 品牌区域（Logo + 标题），展开/折叠切换
  - 可折叠分组（+/- 按钮），激活项橙色竖条指示
  - 底部 GitHub 星标、官网、版本号、语言切换
  - 折叠模式仅图标，展开模式图标 + i18n 标签
  - 懒加载 Logo 纹理，悬停高亮

### 面板视图系统
- **PanelContainer**（新增 `ui_component/panel_container.h/cc`）：统一面板外层容器
  - 橙色标题 + 白色背景 + CreamBorder 描边（可配置圆角/直角）
  - 自动内容区域计算，`SplitRight()` 右侧分割，`BeginScroll()` 滚动子窗口
- **18 个独立面板视图**（`virtual_sprite/panels/`）：
  - `chat_view`：主聊天界面 + 右侧可折叠角色卡片面板 + token/s 徽章
  - `config_view`：SplitView 左右布局，General/Security 分类配置表单
  - `roles_view`：角色列表 + 复选框 + 编辑区（描述/提示词/模型/语音/自动确认）
  - `providers_view`：提供商列表 + 表单（API Key/Base URL/Timeout/Thinking/模型参数）
  - `tts_view`：TTS 启用/禁用、后端/语音选择、测试朗读、ASR 设置
  - `knowledge_view`：知识库浏览器，可展开分类和条目
  - `sessions_view`：会话卡片列表（ID/角色/状态/聚焦按钮）
  - `status_view`：状态仪表板，StatCards 显示服务/会话/配置/内存状态
  - `logs_view`：日志查看器，All/INFO/WARN/ERROR/DEBUG 过滤
  - `usage_view`：Token 用量统计（总/输入/输出/预估费用）+ 会话表格
  - `local_models_view` / `petstore_view` / `memory_view` / `mcp_view` / `security_view` / `skills_view` / `scheduler_view` / `about_view`：信息面板/占位
- **panel_helpers**：共享 UI 组件库（`SplitView`/`SectionForm`/`StatCard`/`CardBox`/`SaveCancelBar`/`PlaceholderView`）

### ChatWindow 重构
- 从单体 ~471 行缩减为调度器：`RenderCurrentView()` switch 路由到各面板
- `sidebar_` 成员驱动导航，`PanelData` 结构体聚合窗口/面板/缩略图
- `CreateTrayWindow()` / `ShowTray()` / `RenderTray()` 系统托盘支持
- 移除 `RenderContentArea()` / `RenderTopBar()` / `RenderSettingsView()` 等旧方法

### SettingsWindow 独立
- 从 ChatWindow 完全提取为独立 `settings_window.h/cc`（+608 行）
- 模态对话框，6 标签页：General/Roles/Providers/Security/TTS/Local Models
- `SettingsState` 编辑状态副本，`InitState()` 填充 / `SaveSettings()` 写回热切换

### RoleConfigManager（运行时角色配置管理）
- 新增 `RoleConfigManager` 类（`role_config_manager.h/cc`）：运行时角色配置持久化
  - `SaveModel()` / `HotSwitch()`：模型热切换，对所有运行中会话生效
  - `SaveTtsVoice()` / `HotSwitchTtsVoice()`：TTS 语音热切换
  - `SaveField()` / `SaveFieldBool()`：任意字段持久化
  - `HotReload()`：从磁盘重新加载角色 JSON

### Platform 层扩展
- 新增 7 个跨平台 API（`platform.h/cc`）：
  - `BrowseForFile()` / `BrowseForDirectory()`：原生文件/目录选择对话框
  - `OpenWithDefault()`：系统默认打开方式
  - `TrashFile()`：移动到回收站/回收
  - `IsAlreadyRunning()`：命名互斥体/文件锁单实例检测
  - `KillProcessByName()`：按名称终止进程
  - `GetPipeErrorString()`：管道错误信息

### ImGui Widget 扩展
- 新增 `SameLine()`、`GetFontScale()`、`GetItemRectSize()`、`IsItemHovered()`
- `InvisibleButton` / `Button` 添加手型光标
- `DrawList::Selection()` 选择框绘制
- `SetHandCursorOnHover()` 便捷方法

### Config 配置更新
- 新增 `font_scale` 字段（`kFontScaleSmall 0.8f` / `kFontScaleLarge 1.1f` / `kFontScaleSwitch 0.95f`）
- 新增 `sprite_assets_dir`、`enable_summary` 持久化
- `SetGlobalFontScale()` 运行时字体缩放支持

### LayoutConfig 全面集中化
- 从 ~37 行扩展至 ~182 行：所有布局常量集中管理
- 侧边栏/面板框架/SplitView/聊天区/输入区/日志/用量/会话/精灵/气泡/托盘/Token 徽章布局常量
- 移除各文件中的内联硬编码值

### i18n 国际化更新
- en.json / zh-CN.json 新增 ~100+ 翻译键
- 侧边栏导航标签、视图标题、分组标签
- 会话/日志/用量面板文本，`font_size`、`tab_asr`、`panel_no_data`、`btn_focus` 等

### 文档更新
- README.md / README.zh-CN.md 同步更新

### 文件统计
- 变更文件：52 个
- 新增：+3,173 行
- 删除：-471 行
- 净变化：+2,702 行

---

## [0.7.5] — 2026-06-06 语音引擎 + Provider Router 重构 + EdgeTTS 进程内解码 + 设置面板独立

本期重点：全新语音子系统（WebRTC VAD + 噪声抑制 + SDL 音频采集 + 语音通道）；Provider 目录重组为 Router 架构（ASR/TTS/LLM 三路分离）；EdgeTTS 全面重写（移除 ffmpeg 子进程，进程内 dr_mp3 解码）；设置面板从 ChatWindow 独立为可复用窗口；WebSocket 客户端封装；TTS 管线从 TtsSpeaker 迁移至 VoiceEngine + 会话驱动；ASR 从 SenseVoice 子进程切换为 HTTP whisper 服务器。净变化 +13,592 行。

### 语音引擎子系统
- **VoiceEngine 单例**：统一 ASR（语音→文字）与 TTS（文字→语音）管线
  - ASR 管道：`FeedPCM()` 接收 16kHz int16 帧，VAD 状态机（3 帧确认说话 → 10 帧间隔 interim → 20 帧静默终结），`PollResult()` 线程安全取结果
  - TTS 管道：`Synthesize()` 经 TtsProviderRouter 合成，`Speak()` 异步重采样 24kHz→16kHz 后推送到 VoiceChannel 播放
  - `SanitizeTtsText()`：剥离 Emoji（4 字节 UTF-8）、Markdown 符号、前导 `#`
- **VAD 状态机**：`FixedBuffer` 5 帧回放缓存防止丢失语音起始；confirm/silence 计数器过滤噪音抖动
- **WebRTC VAD 嵌入式**：`voice/rtc/` 目录完整移植 WebRTC VAD 库
  - GMM 高斯混合模型语音/噪声似然估计
  - Mel 滤波器组 6 子带能量分析（80Hz–4kHz）
  - 频谱峰值追踪、子带能量特征提取
  - 支持 3 级检测灵敏度，mode 3 最激进
- **WebRTC 噪声抑制**：`voice/rtc/ns.*` 移植 WebRTC 降噪器（2148 行）
  - MMSE 频域噪声估计、最小统计量噪声谱追踪
  - 128/256 点 FFT 分析合成、语音概率建模
- **AudioCapture**：SDL3 连续麦克风采集（16kHz S16LE mono），10s 环形缓冲区
- **VoiceChannel**：全局音频 I/O 泵单例，持有音频线程 + AudioCapture + AudioStreamer，~20ms 轮询
- **Resampler**：24kHz→16kHz 线性插值（3:2 比例），配合 EdgeTTS 输出转 VAD 输入

### Provider Router 架构重构
- **目录重组**：所有 provider router 集中到 `providers/provider_router/` 目录
  - `ProviderRouter` → `LlmProviderRouter`（`llm_provider_router.*`）
  - 新增 `AsrProviderRouter`（`asr_provider_router.*`）
  - 新增 `TtsProviderRouter`（`tts_provider_router.*`）
- **AsrProviderRouter**：替代旧 `AsrService`，读取 `asr.server_url` 创建 `HttpAsrProvider`
  - `AsrProcess(pcm)` 实时 PCM 转写、`TranscribeFile()` 文件转写、结果/错误回调
  - 原子 `transcribing_` 防止重叠转写
- **TtsProviderRouter**：替代旧 `TtsSpeaker`，配置驱动多后端缓存
  - `SetBackend()` 运行时切换，`shared_mutex` 线程安全
  - 当前仅实例化 `EdgeTtsProvider`，always fallback edge-tts
- **HttpAsrProvider**（新增）：HTTP 发送 PCM 到远程 whisper `/v1/transcribe` 服务器
  - 替换 `SenseVoiceAsrProvider`（Python 子进程 funasr）和 `AsrService`

### EdgeTTS 全面重写（进程内解码）
- **移除 ffmpeg 子进程依赖**：改为进程内 `dr_mp3` 库直接解码 MP3 到 PCM
- **PIMPL 移除**：从 PIMPL 模式简化为直接继承 `TtsProvider`
- **WebSocket 封装**：使用 `WebSocketClient` RAII 包装替代裸 `CURL*` 管理
- **流式合成移除**：仅保留全量合成模式，`Synthesize(TtsRequest)` 返回内存 PCM 向量
- **新增 `TtsRequest`/`TtsResponse` 结构体**：文本+语音+采样率→PCM 内存传递
- `ConnectAndExchange()`：WebSocket 连接 + `speech.config` + SSML 请求
- `ReceiveAllMp3()`：拉取二进制帧，跳过 2 字节头 + `\r\n\r\n`，累积 MP3 字节
- 加密辅助函数迁移至 `crypto_utils.h`，日期格式化迁移至 `time_wrapper.h`

### WebSocket 客户端
- 新增 `websocket_client.h/cc`：libcurl WebSocket RAII 封装
  - `Connect(url, headers)` — SSL 验证关闭（兼容 EdgeTTS）
  - `Send()`/`SendText()` — TEXT/BINARY 帧发送
  - `Recv()` — 带 `CURLE_AGAIN` 重试（50ms 轮询，可配置超时）
  - `Close()`/`IsConnected()` — 生命周期管理
  - Move-only 语义

### 设置面板独立
- 新增 `settings_window.h/cc`：从 ChatWindow 内联设置提取为可复用类
  - 6 标签页：General / Roles / Providers / Security / TTS / Local Models
  - TTS 标签页新增**测试发音按钮**（调用 `VoiceEngine::Speak()`）
  - 角色标签页支持逐角色模型/TTS 语音选择
  - Cancel 从磁盘重载配置，Save 通过 `SaveSettings()` 热切换运行中会话
  - 全部标签 i18n 就绪

### TTS 管线重构（会话驱动）
- **TtsSpeaker 删除**：功能迁移至 `VoiceEngine` + `TtsProviderRouter`
- **会话级 TTS 集成**：`AgentSession` 新增 `tts_chunks_` 缓冲区 + `TtsSpeakCallback`
  - 流式 text delta 推入队列，按标点分句后立即回调
  - 不完整句子缓存，`COMPLETE`/`STREAM_MODE_COMPLETE` 时刷新全部
  - `TtsSplitSentences()`：`。！？，.!?,\n` 分句
- **Sprite TTS 适配**：从渲染轮询改为 session 注册回调，移除 `last_tts_text_` 等状态
- **TTS Provider API 重写**：
  - `Synthesize(text, output_path)` → `Synthesize(TtsRequest) → TtsResponse`
  - 流式回调 API 移除，仅保留全量合成
  - `SynthesizeToFile()` 基类便捷方法（PCM → WAV 写入）

### 通用工具库扩展
- **crypto_utils.h**（新增）：`Sha256Hex()`（OpenSSL EVP）、`GenerateUuid()`（UUID v4）
- **list_buffer.h**（新增）：
  - `ListBuffer<T>`：互斥锁保护的可增长缓冲区（`Push`/`PopAll`/`PopFront`）
  - `FixedBuffer<T, N>`：非线程安全环形缓冲区，始终保留最后 N 项
- **mp3_decoder.h/cc**（新增）：基于 `dr_mp3.h` 单头文件库的进程内 MP3 解码
  - `dr_mp3.h` 单头文件 5412 行，`DRMP3_API=static` 避免 SDL3_mixer 符号冲突
- **file_utils 增强**：`LoadWav(path)` 加载 WAV → `vector<int16_t>`；`WriteWav()` 写入 WAV；`ParentDir()` 父目录
- **time_wrapper 增强**：`SystemClock::FormatGmtString()` Python 风格 GMT 日期字符串
- **log_wrapper 增强**：`THROTTLE_LOG(interval_ms, ...)` 限频日志宏

### LLM Provider 精简
- `StreamEvent::kError` 枚举值移除
- `ExecuteStream()` 移除 `default_timeout` 参数
- 新增 `RunChatStream<Handler>()` 模板方法统一流式流程
- OpenAI/Anthropic/Ollama/Llamacpp 全部改用模板

### Llamacpp Provider 增强
- 新增 `ValidateModel()` 方法校验 GGUF 模型兼容性
- 新增 `GetLoadedModelInfo()` 返回加载模型元信息
- 修复线程安全与生命周期管理

### AudioStreamer 简化
- `PushChunk(data, len)` + `PlayWav(path)` 合并为单 `PlayAudio(pcm)` 方法
- 匹配新的内存 PCM 管线

### Config/角色配置更新
- **TtsConfig**：新增 `voice` 字段（默认 `zh-CN-XiaoxiaoNeural`）、`voice_list` 数组
- **AsrConfig**：从 SenseVoice 参数重构为 `push_to_talk` + `server_url`（whisper HTTP）
- **RoleConfigManager**：新增 `SaveModel()`/`HotSwitch()` LLM 热切换；`SaveTtsVoice()`/`HotSwitchTtsVoice()` TTS 语音热切换
- `settings.json` 更新 TTS/ASR/角色配置

### 文档更新
- README.md 重构
- 新增 README.zh-CN.md（中文版）
- 删除 README_en.md（由双语 README 替代）

### 文件统计
- 变更文件：115 个
- 新增：+16,935 行
- 删除：-3,343 行
- 净变化：+13,592 行

---

## [Unreleased] — 2026-05-27 EdgeTTS 直接 HTTP 协议重写 + Config ordered_json 重构 + 多构建变体

本期重点：EdgeTTS provider 从 Python 子进程调用完全重写为直接 HTTP 协议实现（curl + DRM token + Opus→PCM 流式）；Config 序列化全面迁移至 `ordered_json` 以保证字段顺序确定性；Platform 新增管道子进程支持（`CreatePipedSubprocess`）；构建系统新增 4 种并行构建变体；角色配置绑定 TTS 语音。净变化 +890 行。

### EdgeTTS 直接 HTTP 协议重写
- **移除 Python edge-tts 子进程依赖**：改为直接通过 curl 实现 Microsoft Edge TTS HTTP 协议
- **新增 DRM 认证**：SHA256 GEC token 生成、UUID 生成、MUID (Machine Unique ID) 防滥用识别
- **SSML 构建**：直接构建 XML SSML 请求体发送至 `speech.platform.bing.com`
- **流式合成支持**：`SupportsStreaming()` 返回 `true`，通过 Opus→PCM 实时转码流式输出
- **WebM/WAV 转码**：ffmpeg 自动将 Edge 输出的 WebM/Opus 转为 24kHz mono WAV
- **opus 库集成**：CMake FetchContent 引入 opus v1.5.2，直接链接 libopus 做 Opus 解码
- **PIMPL 实现**：`EdgeTtsProvider` 使用 `Impl` 结构体隐藏实现细节

### Config 序列化重构
- **`ordered_json` 全线迁移**：`ProsophorConfig::ToJson()` / `LlamacppModelConfig::ToJson()` 返回类型改为 `nlohmann::ordered_json`，配置文件输出保持字段定义顺序
- **LlamacppModelConfig 精简**：移除 `port`、`server_path`、`start_timeout_ms` 序列化；始终输出所有推理参数字段（不再条件性省略）
- **TTS 序列化简化**：移除 `gs_url` 等废弃字段
- **移除 `tools`/`asr` 配置段输出**：当前版本有意不序列化这两段
- **`ExpandHome()` 迁移**：从 `ProsophorConfig::ExpandHome()` 静态方法移除，改为 `file_utils` 空间函数
- **`SaveToFile()` 改用 `WriteOrderedJson()`**：确保保存的 settings.json 字段顺序可控
- `RoleConfigManager::SaveModel()` 改为 `ordered_json` 解析 + `WriteOrderedJson()` 写入

### FileUtils 增强与解耦
- `GetHomeDir()` 从 `Platform::HomeDir()` 委托改为独立函数，移除 `platform.h` include 依赖
- 新增 `WriteOrderedJson()` — 写入 `ordered_json` 到文件

### Platform 管道子进程
- 新增 `PipedProcess` 结构体（`stdin_fd` / `stdout_fd` / `pid`）
- `CreatePipedSubprocess()`：创建子进程并返回可读写 stdin/stdout 的 fd，stderr 透传
- `WaitProcessWithExitCode()`：等待进程退出并返回退出码
- POSIX 实现：`fork()` + `pipe()` + `dup2()` + `exec()`
- Win32 实现：`CreateProcess()` + `CreatePipe()` + `SetHandleInformation()` + `_open_osfhandle()`

### 角色配置 TTS 语音绑定
- 10 个角色 JSON 新增 `tts.voice` 字段：
  - architect → `zh-CN-YunjianNeural`（云健）
  - coder → `zh-CN-YunxiNeural`（云希）
  - default / keqing / teacher → `zh-CN-XiaoxiaoNeural`（晓晓）
  - ayaka / sayu → `ja-JP-NanamiNeural`（七海）
  - kazuha → `ja-JP-KeitaNeural`（圭太）
  - linnea-2 → `zh-CN-XiaoyiNeural`（晓伊）
  - skirk-2 → `zh-CN-XiaomoNeural`（晓墨）
  - reviewer → `zh-CN-YunxiNeural`（云希）

### ChatPanel 渲染优化
- **thinking+text 合并逻辑重构**：先分别收集 thinking/text 再合并，仅在有内容时输出消息条目
- **流式合并渲染**：thinking→content 过渡阶段合并为单条 assistant 条目，避免显示跳变
- **精确换行宽度计算**：改用 `GetContentRegionAvailWidth()` 代替 `scroll_window_->GetWidth()`，准确匹配子窗口内边距与滚动条占位

### ImGui Widget 扩展
- 新增 `Layout::GetContentRegionAvailWidth()` — 获取当前窗口可用内容区宽度
- 新增 `Style::PushVar_FramePadding()` / `ScopedStyleVar::FramePadding()` — FramePadding 样式变量

### TTS Speaker
- 新增 `SanitizeText()`：过滤 Markdown 格式字符（`*` `` ` ` ` ~ ` ` \ ` ` | ` ` # ``），优化朗读体验

### 构建系统多变体支持
- Makefile 新增 4 种并行构建变体模板：
  - `build_win_tui_fast` — 纯终端 + 仅远程 API，无本地模型
  - `build_win_tui_full` — 纯终端 + 全功能（llama/ASR）
  - `build_win_sdl_fast` — 桌面宠物 + 仅远程 API，快速编译
  - `build_win_sdl_full` — 桌面宠物 + 全功能（原 `build_win_sdl`）
- 新增 `build_variant` 模板函数，各变体独立控制 Vulkan/llama/ASR/SDL 开关

### 杂项
- `active_trigger_manager.cc` / `plugin_manager.cc`：内联 `ExpandHome` 改为统一调用 `prosophor::ExpandHome()`
- `llm_provider.h`：`AddThinking()`/`AddText()` 移除多余换行拼接
- `settings.json`：本地模型 provider entry 的 `thinking` 从 `true` → `false`
- CMakeLists.txt：引入 opus 库依赖

### 文件统计
- 变更文件：36 个
- 新增：+1,137 行
- 删除：-247 行
- 净变化：+890 行

---

## [Unreleased] — 2026-05-26 进程内 llama.cpp + ASR 语音识别 + Provider 目录重组

本期重点：引入进程内 llama.cpp C API 直接推理，彻底替代外部 llama-server 子进程架构；新增 ASR 语音识别子系统（SenseVoice 后端）；Provider 目录重组（LLM providers 移入 `providers/llm/`）；删除 GPT-SoVITS 相关代码；Config 全面重构（`AgentConfig` → `ModelConfig`，`providers` → `llm_providers`，`LocalModelConfig` → `LlamacppModelConfig`）；TTS Speaker 增强（语音缓存、分段队列、语音配置）；ChatWindow 移除 StartLLM/StopLLM 按钮；构建系统拆分为 `prosophor_common`/`prosophor_platform`/`prosophor_core` 三层；新增 Token/s 速率显示。净变化 +985 行。

### 进程内 llama.cpp 推理
- **删除外部 llama-server 架构**：移除 `LocalModelManager`、`LocalModelConfig`、`local_model_utils`、llama.cpp 启停脚本
- **新增 `LlamacppProvider`**：通过 llama.cpp C API 直接加载 GGUF 模型进进程，零网络开销
- **丰富推理参数**：`context_window`、`max_tokens`、`temperature`、`top_p`、`min_p`、`seed`、`flash_attn`、`offload_kqv`、`n_batch`/`n_ubatch`、`type_k`/`type_v` KV 缓存量化类型、GPU 开关、CPU 线程数
- **ProviderRouter 自动加载**：初始化时检测 `llamacpp` provider 配置，`auto_start` 为 true 时自动 `Load()`，同时注册 `"local"` 别名
- **`AgentCore::ValidateSession()`** 支持 `base_url == "local"` 进程内路由
- **`/setup` 命令重写**：改为扫描 GGUF 文件并配置进程内模型，移除 `FindServerBinary` 逻辑
- **`/server` 命令移除**：由进程内生命周期替代
- 编译开关：`PROSOPHOR_BUILD_LLAMA=ON`, `PROSOPHOR_BUILD_LLAMA_CUDA=ON`

### ASR 语音识别子系统
- **`providers/asr/` 新目录**：提供统一的语音识别抽象
- `AsrProvider` 抽象基类 — `Transcribe(audio_path)` 同步接口
- `SenseVoiceAsrProvider` — Python 子进程调用 SenseVoice 模型
- `AsrService` 单例门面 — 同步/异步转写，结果/错误回调
- `AsrConfig` 配置：`enabled`、`backend`、`model_dir`、`script_path`、`language`、`n_threads`
- 编译开关：`PROSOPHOR_BUILD_ASR=ON`

### Provider 目录重组
- `providers/anthropic_provider.*` → `providers/llm/anthropic_provider.*`
- `providers/llm_provider.*` → `providers/llm/llm_provider.*`
- `providers/openai_provider.*` → `providers/llm/openai_provider.*`
- `providers/ollama_provider.*` → `providers/llm/ollama_provider.*`
- `providers/detail/*` → `providers/llm/detail/*`
- 所有 `#include` 路径同步更新，影响 20+ 文件

### 删除 GPT-SoVITS
- 删除 `providers/tts/gpt_sovits_provider.*`（206 + 63 行）
- 删除 `managers/gpt_sovits_manager.*`（120 + 44 行）
- TtsConfig 移除 `gs_url`、`gs_install_path`、`gs_auto_start`、`gs_port`、`gs_ref_audio_*`、`gs_text_lang`
- TTS 后端可选值：`"edge-tts"` | `"gpt-sovits"` | `"sherpa-onnx"`

### Config 全面重构
- **`AgentConfig` → `ModelConfig`**：模型参数与 agent 行为分离
- **`providers` → `llm_providers`**：明确语义（区别于 tts/asr 配置）
- **`ProviderConfig::agents` → `model_configs`**，`ProviderEntryConfig::agents` → `models` + `thinking`
- **`GetAgentConfig()` → `GetModelConfig()`**
- **`LocalModelConfig` 删除** → 替换为 `LlamacppModelConfig`（集成在 `ProviderConfig` 中）
- `LlamacppModelConfig`：`model_path`、`gpu_enable`、`threads`、`auto_start`、推理参数、KV 缓存量化、采样参数
- `TtsConfig` 新增 `sherpa_model_dir`、`sherpa_speaker_id`、`sherpa_speed`
- `AsrConfig` 新增
- `settings.json` 同步更新：`providers` → `llm_providers`，`agents` → `models`，`thinking` 移至 entry 级别

### TTS Speaker 增强
- **`SpeakCached()`**：基于 role+backend+voice+text 的 MD5 缓存，相同文本直接回调缓存 WAV
- **`ApplyVoiceProfile()`**：运行时切换后端+语音
- **`SplitSentences()`**：标点分句，支持流式分段队列播放
- **语音队列**：`speech_queue_` + `queue_running_` + `first_audio_chunk_pending_` 实现分段无间隙播放
- **EdgeTTS WebM→WAV**：Edge TTS 输出 WebM/Opus，新增 ffmpeg 自动转码为 24kHz mono WAV
- **EdgeTTS 文本文件传参**：改用 `-f` 文本文件替代 `-t` 命令行文本，解决 Windows cmd 编码问题
- **Sprite TTS 集成**：回复完成后自动触发 `SpeakCached()`，从 role JSON 读取 `tts_backend`/`tts_voice`，重复文本去重

### 构建系统拆分
- `prosophor_common` 静态库：spdlog + nlohmann_json，零平台依赖
- `prosophor_platform` 静态库：平台抽象层，依赖 `prosophor_common`
- `prosophor_core` 依赖 `prosophor_platform`
- 分层编译，加速增量构建
- Makefile 版本升级 `0.6.3` → `0.7.1`

### ChatWindow 重构
- **移除 StartLLM/StopLLM 按钮**：由进程内 llama.cpp 加载替代
- **移除 GPT-SoVITS 配置编辑面板**
- **`config.providers` → `config.llm_providers`**，`agents` → `models` 同步
- **`local_models` → `llamacpp_models`**
- **新增 `RenderTokenSpeed()`**：流式响应时在聊天区域右下角显示实时 token/s

### Token/s 速率追踪
- `RenderSnapshot::streaming_token_speed` 新增字段
- `AgentSession` 在 `STREAM_CONTENT_TYPING` 阶段追踪字符数并估算 token/s（~4 chars/token）
- `STREAM_CONTENT_START` / `STREAM_THINKING_START` 重置计时器
- 完成/错误时清零

### 对话策略增强
- **`DialogStrategy::CompactIfNeeded()`**：每轮请求前基于 `context_window - max_tokens` 估算 token 用量，超限时二分查找最优保留消息数
- `AgentCore::Loop` 在 `BuildRequest` 前调用 `CompactIfNeeded`
- `Preprocess` 增加空消息跳过保护

### FileUtils 扩展
- 新增 `RemoveFile()` — 删除文件
- 新增 `FileSize()` — 获取文件字节数
- 新增 `WriteFile()` — 写入文本文件

### AudioStreamer 增强
- 新增 `AudioStreamer::PlayWav()` — 静态方法，直接从 WAV 文件创建流式播放器

### 角色配置更新
- 默认角色改为 `["default", "ayaka", "sayu", "keqing"]`
- ayaka、linnea-2、skirk-2 角色配置微调
- settings.json 中 `thinking` 移至 entry 级别

### 杂项
- 色板新增 `MilkyWhite`、`CyanLight`、`PinkBlush`、`Pink`、`GoldLight`、`BlueLightSoft`
- `ScopedStyleVar` RAII 辅助类新增 `WindowPadding`
- `agent_role.h` 新增 `tts_backend`/`tts_voice` 字段
- Docker 中测试通过

### 文件统计
- 变更文件：82 个
- 新增：+2,713 行
- 删除：-1,728 行
- 净变化：+985 行

---

## [Unreleased] — 2026-05-24 Platform API 重构 + 设置面板重写 + 聊天渲染优化

本期重点：Platform API 从 `platform::` 命名空间重构为 `Platform::` 大驼峰类，删除 `pipe_handler.h` 将其功能合并至 Platform 类；设置面板全面重写（多标签页 + StartLLM/StopLLM 按钮 + 浅色主题）；聊天渲染优化（流式滚动长度追踪、thinking 合并显示、气泡过滤 thinking 块）；Config Provider 支持多 entry 数组持久化；角色模型切换至本地 gemma-4；i18n 翻译补充；Debug 构建恢复。净变化 +655 行。

### Platform API 重构
- `platform::` 命名空间 → `Platform::` 类，所有静态方法改为大驼峰命名：`platform::ReadConsoleLine()` → `Platform::ReadConsoleLine()`、`platform::kIsWindows` → `Platform::kIsWindows`、`platform::HomeDir()` → `Platform::HomeDir()` 等
- 删除 `platform/pipe_handler.h`，管道/进程操作（`CreatePipe`、`ClosePipe`、`ForkAndExec`、`WaitProcess` 等）合并为 `Platform` 类的静态方法
- 迁移影响：`file_utils.cc`、`string_utils.cc`、`status_bar.cc`、`main.cc`、`ai_coding.cc`、`config.cc` 等 20+ 文件同步更新调用点
- `time_wrapper.h` 移除 `platform.h` 依赖，内联 `localtime_s`/`localtime_r` 实现

### 设置面板重写
- 设置弹窗全面重写，支持多标签页：General、Roles、Providers、Security、TTS、Local Models
- 新增 StartLLM / StopLLM 按钮，异步启动/停止本地模型服务器（带线程池 detach）
- 设置面板应用浅色主题配色：白底、蓝标题栏、灰色文字
- 编辑状态支持日志级别、摘要开关、角色选择、权限级别、TTS 配置、本地模型参数
- Provider 配置支持 entries 数组读写（多 endpoint 支持）
- About 弹窗改进：唯一 modal ID 防止 ImGui 哈希冲突、显示 i18n `app_name`

### 聊天渲染优化
- **ChatPanel 流式滚动**: 新增 `last_streaming_len_` 追踪流式内容长度变化，滚动仅在长度变化时触发，避免流式中间态频繁滚动
- **thinking 合并渲染**: thinking + text 块合并为单条消息显示，thinking 内容前加 `【thinking】` 标签前缀，保留原始 role 标签
- **SpeechBubble 过滤 thinking**: 新增 `FilterAssistantText()` 静态方法，气泡模式只提取 assistant 的 text content block，过滤 thinking 块

### Config 重构
- Provider 序列化从单一 `provider_json` 对象改为 `entries` 数组（支持多 endpoint 配置）
- `DefaultConfigPath()` 策略调整：优先使用 `~/.prosophor/settings.json`，其次 exe 同级 `.prosophor/`
- 新增 `sprite_assets_dir`、`tts.enabled` 字段持久化
- `TtsConfig` 新增 `enabled` 字段，默认 `true`
- `LocalModelConfig::ToJson()` 新增 `model_path_for_win` 字段写入

### ImGui Widget 扩展
- 新增 `ImGuiWidget::InputInt()` — 整数输入控件
- 新增 `ImGuiWidget::SliderFloat()` — 浮点数滑动条（double → float 转换包装）
- 颜色槽扩展：`Color::Slot::TitleBg`、`TitleBgActive`

### TTS 受控开关
- TTS 语音合成增加 `tts.enabled` 配置门控，关闭时回复完成不再触发语音播放
- `settings.json` 中 `tts.enabled` 默认设为 `false`

### 角色配置更新
- ayaka、kazuha、keqing、sayu 模型从 `deepseek-v4-flash` 切换为 `google_gemma-4-E4B-it-Q4_K_M`，protocol 改为 `openai`
- linnea-2、skirk-2 保持 `deepseek-v4-flash`，protocol 改为 `openai`
- architect 精灵图片从 `ayaka.webp` 改为 `skirk-2.webp`
- settings.json 默认角色新增 `architect`，architect thinking 启用

### i18n 翻译补充
- en.json / zh-CN.json 新增 Security/TTS/Local Models 配置标签翻译键
- 新增 `app_name` 键（用于 About 弹窗和窗口标题）
- 中文标题简化：`智灵(Prosophor)` → `智灵`

### 构建
- Makefile 构建类型从 `RelWithDebInfo` 切换回 `Debug`
- 新增 `-DGGML_VULKAN=ON` Vulkan 后端支持
- 精灵 Debug 边框（窗口轮廓 + hitbox 叠加）暂时注释，`NDEBUG` 条件代码禁用

### 文件统计
- 变更文件：48 个
- 新增：+1,157 行
- 删除：-502 行
- 净变化：+655 行

---

## [Unreleased] — 2026-05-23 TTS 多后端重构 + GPT-SoVITS 流式语音 + Windows 安装发布

本期重点：TTS 从单一服务迁移为 Provider 架构，新增 edge-tts 与 GPT-SoVITS 双后端；接入 GPT-SoVITS 本地 API 生命周期管理和 WAV/PCM 流式播放；VirtualSprite 在回复完成后自动触发语音合成；修复 ImGui Begin/End RAII 配对规则；增强托盘图标、窗口版本号、Release 日志和 Windows NSIS/Gitee 发布流程。净变化 +893 行。

### TTS Provider 架构
- 新增 `TtsProvider` 抽象基类，统一同步合成与流式合成接口
- 新增 `EdgeTtsProvider`，保留 edge-tts 子进程合成能力并作为默认后端
- 新增 `GptSoVitsProvider`，通过本地 HTTP `/tts` 接口支持普通 WAV 输出和 streaming_mode=2 流式输出
- `TtsSpeaker` 从 `services/` 迁移到 `providers/tts/`，改为多后端门面，支持运行时切换后端、语音选择、合成完成回调和音频 chunk 回调
- 非流式后端通过临时 WAV 文件读取方式提供统一的流式回退路径

### GPT-SoVITS 集成
- 新增 `GptSoVitsManager`，负责启动/停止 GPT-SoVITS `api_v2.py` 子进程、端口等待和进程存活检测
- `TtsConfig` 新增 `tts` 配置段，支持 `backend`、`gs_url`、`gs_install_path`、`gs_auto_start`、`gs_port`、参考音频路径/文本/语言和输入文本语言
- `GptSoVitsProvider` 支持参考音频参数、文本语言配置、WAV header 解析，以及从首包中剥离 PCM 数据后转发给播放器

### 语音播放接入
- `VirtualSprite::GlobalInit()` 绑定 `TtsSpeaker` 流式回调，创建 `AudioStreamer` 并推送 PCM chunk 播放
- Agent 回复进入 `COMPLETE` / `STREAM_MODE_COMPLETE` 后，自动对最终回复文本执行 `SpeakStream()`
- `media_engine.h` 导出 `audio_streamer.h`，便于上层窗口系统直接接入流式音频播放

### UI / ImGui 修复与增强
- 修复 `ScopedGuard` 对 ImGui `Begin()/End()`、`BeginChild()/EndChild()` 的配对规则：普通窗口和 Child 即使 Begin 返回 false 也会调用 End，Popup/Menu/Tab 仍按条件调用
- ChatPanel、SpeechBubble、NavBar、ChatWindow 等窗口/Child 使用新的 always-call RAII 规则，避免 ImGui 栈不匹配
- `DrawList` 新增 `CircleFilled`、`CircleOutline`、`Line` 绘制接口，颜色表新增 Cyan/Pink 科技色系
- ChatWindow 标题显示 `PROSOPHOR_VERSION`，语言切换后同步更新带版本号标题
- 托盘窗口改为预加载 `robot_icon.png` 纹理并绘制真实图标，不再绘制文字 P 占位
- Debug 构建重新启用精灵窗口轮廓和 hitbox 边框叠加

### 配置、路径与日志
- `ProsophorConfig::BaseDir()` 明确使用用户目录 `~/.prosophor` 作为可写配置目录，新增 `InstallConfigDir()` 表示 exe 同级只读配置目录
- 多处路径存在性判断改用 `FileExists()` / `DirExists()`，统一平台兼容行为
- Release 构建增加文件日志输出到 `~/.prosophor/log/log-YYYYMMDD.txt`，Debug 构建保持 stdout 日志

### 构建与发布
- Makefile 版本升级到 `0.6.3`，默认构建类型改为 `RelWithDebInfo`
- 新增 `package` 目标，使用 NSIS 生成 Windows 安装包 `Prosophor-<version>-win64-setup.exe`
- `deploy` 改为发布 NSIS 安装包到 GitHub Releases；新增 `deploy_gitee` 和 `deploy_all` 支持发布到 Gitee/双平台
- CMake 为 GUI/TUI 目标加入 `app.rc` 和 `app.ico`，Release/RelWithDebInfo/MinSizeRel 下设置 `WIN32_EXECUTABLE`
- 移除根 CMake 中旧的 assets 与 bin/.prosophor 额外安装步骤

### 资源清理
- 删除旧 ayaka 资产配置与 demo 截图资源
- 新增 Windows 应用图标资源 `main_src/resources/app.ico` / `app.rc`

### 文件统计
- 变更文件：46 个
- 新增：+1,202 行
- 删除：-309 行
- 净变化：+893 行

---

## [Unreleased] — 2026-05-20 托盘窗口独立字体 + 精灵背景纹理清理

本期重点：Tray 窗口独立字体配置（`use_shared_font=false`），避免共享 CJK 字体上下文冲突；移除 Sprite 中废弃的 `LoadBackground()` 方法和 `bg_texture_` 成员（精灵窗口不再使用静态背景纹理）。

### Tray 窗口
- `ChatWindow::CreateTrayWindow()` 设置 `cfg.use_shared_font = false`，托盘窗口不再共享 MediaCore 的 CJK 字体集

### 精灵系统清理
- 移除 `Sprite::LoadBackground()` 方法及 `bg_texture_` 成员
- 精灵窗口创建时不再加载 `solitude.jpg` 背景纹理（纹理层已由 pet spritesheet + Widget UI 替代）

### 文件统计
- 变更文件：3 个
- 新增：+1 行
- 删除：-13 行
- 净变化：-12 行

---

## [Unreleased] — 2026-05-19 ImGui API 命名空间重构 + i18n 国际化 + 精灵系统增强

本期重点：ImGui 工具函数全面命名空间化（`ImGuiFoo` → `ImGuiWindow::`/`Style::`/`Layout::`/`DrawList::`）；新增 i18n 国际化系统（JSON 翻译 + 运行时切换）；精灵系统增强（名字标签、显示名绑定、调试边框、悬浮感知）；ChatPanel 智能自动滚动；OpenAI thinking 格式更新；场景残余清理。净变化 +2,607 行。

### ImGui Widget 命名空间重构
- 所有扁平工具函数（`SetImGuiNextWindowPos`、`ImGuiBegin`、`PushStyleColor` 等）归入命名空间类：`ImGuiWindow::`、`Style::`、`Layout::`、`Scroll::`、`Child::`、`DrawList::`
- 新增 `DrawList` 类：`RoundRect`、`OverlayRectOutline`、`Text`、`Panel`、`ResizeGrip` 等静态绘图方法
- `ImGuiCond_*` 常量从 0..3 改为 power-of-two 位掩码（匹配 ImGui 原生枚举）
- 新增 `ImGuiWindowFlags_MenuBar` 标志常量
- 移除冗余自由函数：`IsItemHovered`、`IsItemActive`、`GetMouseDragDelta`、`ResetMouseDragDelta`、`SetMouseCursor`

### 国际化 (i18n) 系统
- 新增 `common/i18n.{h,cc}` — I18n 单例，JSON 翻译文件懒加载 + LRU 缓存
- 新增 `config/.prosophor/lang/en.json`、`zh-CN.json` 双语言翻译文件
- `VirtualSprite::GlobalInit()` 自动初始化 `zh-CN` 翻译
- `I18n::SetLanguage(lang)` 运行时切换语言，缓存已加载的翻译表

### 精灵系统增强
- **名字标签 Widget**: `Sprite` 从 `DrawTextRect` 裸绘改为 `Label` widget（`name_label_`），奶油底色 + 橙色文字
- **DisplayName 绑定**: `LoadSpriteBindingFromRole()` 返回 `display_name` 字段，role JSON 可配置精灵显示名
- **轮廓调试**: 非 Debug 构建下窗口轮廓 + 精灵 hitbox 叠加半透明边框（`#ifndef NDEBUG`）
- **悬浮感知**: 新增 `SetHovering()` 和 `LEAVE` 鼠标事件处理
- **气泡切换**: 新增 `ToggleSpeechBubble()` 公开方法，右键菜单切换到按精灵逐个体控制气泡
- **气泡设置**: `SpeechBubble` 标题栏和 assistant 显示名使用精灵名称；输入框圆角可配置（`bubble_radius`）
- **访问器增强**: 新增 `GetName()`、`GetCurrentPetSlug()`、`GetCurrentPetName()`、`GetSpritesheetPath()`
- **Spritesheet 增强**: 新增 `GetActionFps()` 逐动作帧率；`SpritesheetAction::COUNT` 枚举总行数；`file_path_` 纹理路径追踪

### 聊天面板智能滚动
- ChatPanel 自动滚动改为基于消息计数变化触发（`last_msg_count_`），流式过程中用户可自由拖拽滚动条
- ChatPanel 滚动条宽度收窄至 3.0f
- 新增 `SetSnapshot()` 方法，session 切换时自动清理暂存消息
- SpeechBubble 内 ChatPanel 用户/助理消息背景设为透明

### OpenAI Provider 更新
- `enable_thinking` 字段替换为 `thinking.type`（`"enabled"`/`"disabled"`），对齐最新 OpenAI API
- `OpenAIProvider` 构造函数移除 `enable_thinking_` 参数，handler 级别不再控制 thinking
- `OpenAIStreamHandler` 不再需要 `enable_thinking_` 标志
- `reasoning_content` 解析不再受 `enable_thinking_` 门控
- 默认 `thinking: false`

### 场景残余清理
- 移除 `scene/ui_renderer.cc/h`（功能迁至 `virtual_sprite/ui_renderer`）
- `asset_define.h`、`layout_config.h` 从 `scene/` 移至 `virtual_sprite/`
- CMake 构建移除 `scene/` 目录扫描和 include 路径
- `UIRenderer` 重构：新增 `SetOnToggleChat(WindowCallback)`（按窗口切换气泡）、`SetOnShowMainWindow`、`SetOnOpenSettings`、`SetOnNewSprite` 回调；`SetOnToggleChat` 签名从无参改为 `Window*` 参数
- ESC 键仅隐藏中央窗口（不再切换上下文菜单）

### 颜色扩展
- 奶油色系: `Cream70`、`CreamTranslucent`、`CreamOpaque90`
- 橙色系: `OrangeLightest`、`OrangeWarm`、`OrangeDeep`
- 聊天气泡色: `BluePale`、`GreenPale`、`Gray20a`
- 白色系: `White80`

### 工具函数新增
- `time_wrapper.h`: `FormatCurrentTime(format)` 自定义格式时间戳；`GetCurrentEpochSeconds()` 秒级时间戳
- `sprite_manager`: `FindByWindow()` 窗口指针查找精灵；`GetFocusedSpriteName()` 焦点精灵显示名
- `agent_role_loader`: role JSON 解析新增 `display_name` 字段

### 构建 & 配置
- CMake: MinGW 安装器包含 SSL ca-bundle.crt 证书包
- Makefile: 包名正式改为 `Prosophor`，版本 `0.6.0`；移除 `release` 目标；`deploy` 简化从 install/bin 直接打包；`run_win` 自动传递 `SSL_CERT_FILE=ca-bundle.crt`
- settings.json: 默认 `thinking` 改为 `false`

### 文件统计
- 变更文件：54 个
- 新增：+3,486 行
- 删除：-879 行
- 净变化：+2,607 行

---

## [Unreleased] — 2026-05-17 角色配置全面 JSON 化 + 新增 6 个角色精灵

本期重点：角色配置全面 JSON 化 + 新增 6 个角色精灵；对话摘要策略重构（贝尔曼衰减）；SpeechBubble/ChatPanel 接入 Widget 渲染树；多精灵管理器动态注册；ImGui 抽象层扩展；Provider 流处理优化；CPack 打包与一键部署。净变化 +328 行。

### 角色系统 JSON 化 & 多角色精灵
- 角色配置从 Markdown (YAML front-matter) 迁移为纯 JSON 格式，结构更清晰、解析更可靠
- 新增 6 个角色：ayaka、kazuha、keqing、sayu、skirk-2、linnea-2，每个绑定独立精灵图 (.webp)
- 默认角色改为多角色组合 `["ayaka", "sayu", "keqing"]`，支持多角色轮换
- 新增精灵图资产目录 `config/.prosophor/assets/`
- Providers agents 字段从 object 改为 array，支持同名 agent 多配置

### 对话系统重构
- `compact_service.{h,cc}` 重命名为 `dialog_strategy.{h,cc}`，统一对话摘要策略抽象
- `ApplyDialogStrategy()` 逻辑重构：支持贝尔曼衰减摘要 + 角色摘要开关
- `AgentCore` 接入新 `DialogStrategy` 接口，移除内联摘要逻辑

### SpeechBubble / ChatPanel 重写
- `SpeechBubble` 继承 `Widget` 渲染树，接入坐标级联系统
- InputPanel / ChatPanel 作为 Widget 子节点，通过 `SetPixelRect()` 直接定位
- 双模式渲染：compact 不透明气泡 + maximized 半透明全屏
- 自动滚动分离：聊天面板智能跟随（在底部时自动滚），气泡内强制滚到底
- 消息间距缩紧、文本颜色统一为 `Gray40`

### 精灵系统增强
- `Sprite` 渲染循环重构，支持多精灵管理器注册
- `SpriteManager` 迭代器注册机制，支持动态添加/移除精灵
- `ChatWindow` 集成新 SpeechBubble，双击切换显示

### 媒体引擎扩展
- **共享字体**: MediaCore 级共享 CJK TTF 数据，多窗口只读一次磁盘
- **ImGui 抽象层**: 新增弹出窗口 (`OpenPopup`/`BeginPopupModal`)、标签栏 (`TabBar`/`TabItem`)、控件 (`Checkbox`/`Combo`/`InputText`/`Button`)、窗口查询 (`GetWindowPos`/`GetScrollMaxY`/`GetScrollY`)
- `Window::SetTitle()` 接口、`SetImGuiNextWindowSize` 条件参数、`ImGuiCond_*` 常量
- `Widget::SetPixelRect()` 直接像素坐标设置，跳过百分比解算

### Provider / Network
- **OpenAI StreamHandler**: `enable_thinking_` 在 handler 级别控制，`Deserialize` 非 thinking 时跳过 `reasoning_content`
- **Anthropic StreamHandler**: 移除冗余 `stop_reason`/`usage` 成员变量，直接写入 `accumulated_response`
- **curl SSL**: 支持 `SSL_CERT_FILE` 环境变量，未设置时关闭对端证书验证
- 修复 `HttpClient::Post` 调试日志输出原始 `res_body` 而非已移动的 `response.body`

### 构建 & 打包
- **CPack 打包**: 集成 CPack，Windows 下优先 NSIS 安装器，降级 ZIP
- **MinGW DLL 捆绑**: 自动复制 libcurl、OpenSSL、zlib 等 20+ 运行时 DLL
- **便携部署**: config 同时安装到 `bin/.prosophor/` 作为备选查找路径
- **一键发布**: Makefile 新增 `deploy` 目标 (cmake --build → cpack)
- 编译定义 `PROSOPHOR_SOURCE_DIR` 暴露源码路径

### 杂项
- `/help roles` 改用 `.json` 文件自动补全
- `ProviderRouter` 默认角色从 string 改为 vector 取第一个
- 重构后 `dialog_strategy.h` 新增 121 行，`dialog_strategy.cc` 净变更 235 行

---

## [2026-05-16] - 桌面宠物精灵窗口系统与多窗口架构

### 新增
- **桌面宠物精灵窗口系统**：Sprite 独立窗口，替代旧式场景渲染
  - `Sprite` 类：独立 SDL 窗口 + ImGui 上下文，拥有自己的 session、pet spritesheet 渲染、拖拽、动画循环
  - `Spritesheet` 精灵表渲染：9 种动作（IDLE/RUN/WAVE/JUMP/FAILED/WAIT/SPRINT/REVIEW），帧动画播放
  - `SpeechBubble` 漫画式气对话框：气泡主体 + 三角尾巴 + 标题栏 + 输入区 + 缩放手柄，支持 compact/maximized 模式
  - `SpriteManager` 多精灵实例管理：`CreateSprite`/`FindBySessionId`/`UpdateAll`
  - 支持拖拽移动、双击切换中央窗口、自动漫游（IDLE 时左右走）
  - `agent_state_observer`/`anime_character` 替换为精灵系统的状态 → 动作映射
- **聊天窗口独立化**：`ChatWindow` 独立 SDL 窗口，与精灵窗口分离
  - 使用 `ChatPanel` + `InputPanel` 构建完整聊天 UI
  - 支持显示/隐藏、托盘图标切换
- **新命令**：
  - `/workspace`（别名 `/ws`）— 设置/查看当前工作区路径
  - `/skills` — 列出和查看可用的技能文件（`/skills list` / `skills show <name>`）
  - 动态技能命令注册（从 `~/.prosophor/skills/` 自动加载）

### 重构
- **多窗口架构**：`media_engine::Window` 完整窗口抽象
  - SDL_Window + Renderer + ImGui 上下文三合一封装
  - `WindowConfig` 支持透明背景、无边框、跳过任务栏、置顶等模式
  - `MediaCore` 升级为多窗口管理器，支持独立帧循环
  - 消除旧版单窗口假设，Window 通过 `BeginFrame`/`EndFrame`/`RenderFrame` 全包/分步渲染
  - 新增 `sdl_interface/window.cc` 实现（225 行）
- **VirtualSprite 精简**：从 monolithic 单体重塑为 `SpriteManager` + `ChatWindow` 协调者
  - 移除 `scene/agent_state_observer.cc/h`（由精灵状态映射替代）
  - 移除 `scene/anime_character.cc/h`（由 Spritesheet 精灵表替代）
  - 移除 `scene/character_sprite.cc/h`、`scene/galgame_mode.cc/h`、`scene/home_screen.cc/h`、`scene/office_*.cc/h`、`scene/pixel_character.cc/h`
  - 移除 `scene/character_state_observer.cc/h`
- **Widget 百分比布局树**：新增 `media_engine::Widget` 基类替代 `UIContainer`
  - 全百分比坐标系统：`SetRoot(px,py)` / `SetPosition(x%,y%,w%,h%)`
  - 父尺寸变化自动级联 ResolveSelf 至整棵布局树
  - 新增 `Label`（像素文本标签）、`NavBar`（底部导航栏）子类
  - 移除 `ui_container.cc/h`（由 Widget 替代）
- **ImGui Widget 大幅精简**：`imgui_widget` 从全组件库精简为仅保留 Button
  - 命名空间 `imgui_widget::` → `media_engine::`
  - 移除：`IconButton`、`Slider`、`Checkbox`、`ColorPicker`、`ComboBox`、`ProgressBar`、`Image`、`InputText`
  - `imgui_widget.cc` 从 ~1052 行减至约 ~400 行，去除 ~650 行冗余 ImGui 包装
- **`imgui_widget.h` 平台无关化**：所有实现移至 `sdl_interface/`，media 层仅保留 `VoidCallback`/`StringCallback` 类型别名和有限的前置声明
- **LayoutConfig 重构**：移除废弃的办公区/场景布局方法，新增精灵窗口尺寸、宠物配置、托盘配置字段
- **`ui_renderer.cc/h` 精简**：适配新架构，移除场景渲染残余

### 改进
- **Colors 增强**：新增 `Color::Slot` ImGui 样式颜色槽索引枚举；扩展灰度色板（GrayBlack/GrayNearBlack/GrayDarkest/Gray20 等）；所有颜色纳入 `media_engine` 命名空间
- **ChatPanel 增强**：新增 `RenderContentInRect()` 方法，支持在指定矩形区域内渲染消息（供 SpeechBubble 使用）
- **HeaderBar/InputPanel/UIPanel 重构**：接入 Widget 布局树，移除硬编码坐标
- **AgentEngine 增强**：多会话 continue 支持，session 状态管理优化
- **配置**：`enable_summary` 默认开启 (`true`)
- **构建**：`Makefile` 新增 `release` 打包目标、`run_llamacpp_server`/`stop_llamacpp_server` 目标；`CMakeLists.txt` 适配新文件结构
- **命令注册**：`command_registry.cc` 新增 `/workspace`、`/skills` 命令及动态技能命令加载

### 清理
- 移除约 3000+ 行废弃代码（旧场景系统、旧角色渲染器、旧布局系统、冗余 ImGui 包装）
- 移除 `time_wrapper.h` 中废弃的函数声明
- 移除 `ui_container.cc/h`

### 文件统计
- 变更文件：76 个
- 新增：+3,708 行
- 删除：-3,380 行
- 净变化：+328 行

---

## [2026-05-11] - 对话摘要系统与上下文压缩重构

### 新增
- **对话摘要系统**: 零额外 API 调用的贝尔曼衰减摘要循环
  - `ApplyDialogStrategy()` 在每轮请求前注入时间戳 + 历史摘要 + 摘要生成指令
  - `ExtractDialogSummary()` 在响应后提取 `[摘要]` 标签内容存入 `MessageSchema::summary`
  - 递推公式：`每轮摘要 = 本轮内容 + γ × 上轮摘要`，关键决策不衰减
  - 新增 `enable_summary` 配置开关（settings.json 根字段 + role 元数据），可在 settings.json 全局关闭
  - system prompt 新增摘要生成指令模板

### 重构
- **上下文压缩策略替换**: 从 LLM 调用压缩（`MaybeCompact`）改为对话摘要 + 简单截断
  - 移除 `AgentCore::MaybeCompact()`（曾额外调用 LLM 生成摘要）
  - 新增 `AgentCore::ApplyDialogStrategy()` / `ExtractDialogSummary()` 替代 LLM 压缩
  - 上下文过大时仅保留最近 2 条消息（1 轮完整 user+assistant），不再调用 LLM 压缩

### 改进
- **API 错误处理优化**:
  - `agent_core.cc`: API 错误时调用 `CleanupInterruptedLoop()` 移除孤立 user 消息
  - `ai_coding.cc`: STATE_ERROR 状态输出具体错误内容到终端
  - `curl_client.cc`: 移除重复的 `LOG_ERROR`，错误信息由调用方统一处理
  - Provider 错误信息格式统一（`AnthropicProvider::ChatStream` / `OpenAIProvider::ChatStream`）
- **配置**: `settings.json` 新增 `enable_summary: false`，`auto_start` 默认关闭

### 文件统计
- 变更文件：14 个
- 新增：+104 行
- 删除：-47 行
- 净变化：+57 行

---

## [2026-05-10] - 清理 scene 界面

### 重构
- **角色渲染系统重构**: `AnimeCharacterRenderer` 从纯程序化绘制（DrawHair/DrawBody/DrawEyes 等多方法）重构为 PNG 立绘优先 + Q 版胸像回退的双层架构
  - 新增 `Texture` 纹理缓存和 `LoadPortraitTexture` 懒加载机制
  - 新增 `GetPalette` 统一调色板、`RenderWithTexture` 纹理渲染、`DrawFallback*` 回退绘制子方法
  - PNG 立绘支持呼吸浮动、脉冲缩放、状态色调覆盖、Error 红 X 覆盖、眨眼覆盖
  - Q 版回退从全身像精简为胸像（大眼可爱风格），移除了腿部/手臂/配件等全身绘制逻辑
  - 移除了 ~1500 行程序化绘制代码（5 角色 x 全身各部位）
- **场景模式精简**: 移除多模式 UI 系统（HOME/GALGAME/VIRTUAL_HUMAN/TERMINAL），固定为虚拟人交互模式
  - 删除 `HomeScreen`, `GalgameScene`, `OfficeBackground`, `OfficeCharacterManager`, `CharacterSpriteRenderer`, `CharacterStateObserver`, `PixelCharacterRenderer` 及其头文件（7 对文件）
  - 删除模式切换逻辑 `SwitchMode()`、ESC 快捷键切换、`saved_callback_` 保存/恢复
  - 删除 GALGAME 模式的像素风格角色绘制、教室背景、对话系统和 WASD 操作
  - 删除办公室 2D 俯视场景（地板/墙壁/门/窗/桌子/电脑/椅子/盆栽绘制）
  - 删除 BFS 寻路、角色状态机（IDLE/WALK/TYPE/READ）、瓷砖地图、精灵图 PNG 加载与帧裁剪
  - 删除 `CharacterStateObserver` 的 AgentRuntimeState→CharacterState 映射和 SetCurrentTool
  - 删除 `UIMode` 枚举及相关回调
- **聊天面板采用快照驱动**: `ChatPanel` 从内部消息存储（`messages_`、`AddMessage`/`UpdateLastMessage`/`ClearMessages`/`StartAssistantMessage`）重构为外部 `RenderSnapshot` 驱动
  - 支持 thinking 块渲染（先显示 thinking 再显示 text）
  - 支持流式 thinking/text 实时渲染
  - `RenderContent()` 签名改为 `RenderContent(const RenderSnapshot&)`
- **UI 渲染器解耦**: `UIRenderer` 移除对 `ChatPanel` 的直接消息操作转发，改为 `SnapshotGetter` 回调获取渲染快照
  - 移除了 `SubmitUserMessage()`, `SendToChatPanel()`, `UpdateLastMessage()`, `StartAssistantMessage()`, `ClearHistory()`
  - 移除了 `SetAgentState()`，改为从 snapshot 获取 state
- **场景背景重构**: `AgentStateVisualizer::DrawBlackboard()` 从程序化黑板绘制（木框+深绿渐变+粉笔字+粉笔槽）改为 JPG 壁纸渲染（支持拉伸/平铺/纯色 fallback）
  - 新增 `LoadBackground()` 从 `BackwallDir()` 加载 `solitude.jpg`
  - 新增 `bg_texture_` 纹理成员
- **自动创建会话**: VirtualSprite 初始化时自动创建默认会话，确保 UI 启动即就绪

### 改进
- **ImGui 标志更新**: 窗口/子窗口标志常量更新为 ImGui 1.92.8 标准值，新增完整标志位定义
- **ScrollWindow 嵌套修复**: 从 `ImGuiBegin()` 改为 `BeginChild()`，正确处理在父窗口内的嵌套滚动
- **窗口最大化**: `SDL_CreateWindowAndRenderer` 增加 `SDL_WINDOW_MAXIMIZED` 标志，启动时窗口最大化
- **窗口缩放响应**: 新增 `SDL_EVENT_WINDOW_RESIZED` 事件处理，自动更新逻辑尺寸和 Letterbox
- **字体路径平台化**: `platform.h` 新增跨平台 `kDefaultFontPath` 常量（Windows `msyh.ttc` / Linux `DroidSansFallbackFull.ttf`）
  - 所有硬编码 `"C:/Windows/Fonts/msyh.ttc"` 引用替换为 `platform::kDefaultFontPath`
- **Color 结构体传参**: `MediaUtil::DrawTextRect` 及相关函数从分离的 r/g/b/a 参数改为 `Color` 结构体传参
- **资源路径统一**: `asset_define.h` 从 `AssetDefine` 单例模式简化为内联辅助函数，使用 `PROSOPHOR_SOURCE_DIR` CMake 编译定义
  - 移除了 EMSCRIPTEN 平台条件编译
  - 新增 `AssetBase()`, `PortraitDir()`, `BackwallDir()`, `ImageDir()`, `SoundDir()`, `MusicDir()`, `FontDir()`, `EffectDir()` 函数
- **字符类型从角色 ID 映射**: 新增 `AnimeCharacterTypeFromRoleId()` 内联函数，按 role_id 字符串选择立绘类型
- **agent_state_observer**: 在 `GetOrCreate` 时根据 role_id 设置角色类型

### 清理
- 移除约 3000+ 行废弃代码（旧场景系统、旧渲染器、模式切换、精灵图系统）
- 移除中文注释噪音（不必要的块注释）

---

## [2026-05-10] - 输入系统重组与引擎 API 简化

### AgentSession 封装与数据隐藏
- `AgentSession` 从 struct 重构为 class，所有字段私有化，通过 getter/setter 访问
- 新增 `agent_session.cc` 实现文件（208 行），接管构造函数、move 语义、`SetOutput()`、快照生成
- `SetOutput()` 从 `AgentCore` 静态方法内聚到 `AgentSession` 成员方法，输出回调、流式文本累积统一由 session 管理
- `AgentCore` 移除 `SetSessionOutput()` 静态方法，所有调用点改为 `session.SetOutput()`
- `stop_requested` 原子标志封装为 `RequestStop()` / `IsStopRequested()` / `ClearStopRequested()` 方法

### AgentEngine API 简化
- 移除 `focused_session_id_` 概念，不再持有 "当前会话" 状态
- TUI (`AiCoding`) 改为在 `Run()` 时 `CreateSession()` 并自行持有 `session_id_`
- 旧 API `ProcessUserMessage(text)` / `StopCurrentSession()` 替换为 `SendUserMessage(session_id, text)` / `StopSession(session_id)`
- 新增 `GetFocusedSessionSnapshot()` 返回 `std::optional<RenderSnapshot>`，UI 每帧无锁取一次渲染快照
- `SwitchRole` 签名改为 `(session_id, new_role_id)`，会话可原地切换角色

### 连续输入合并（pending buffer）
- `AgentSessionManager::SendToSessionAsync` 引入生产-消费模式：所有消息追加到 `pending_inputs_` buffer
- 单 task 内循环 swap 消费，buffer 空即结束，连续输入合并为一次 LLM 调用
- 新增 `StartChain()` 方法管理单线程内的积压处理循环
- 输入流时序：`输入 A → 提交 StartChain → 输入 B/C 追加 → Loop(A) → drain [B,C] 合并 → Loop("B\n\nC")`

### 主动触发管理迁移
- `ActiveTriggerManager` 初始化从 `AgentSessionManager` 迁移到 `AgentEngine::InitializeComponents()`
- `AgentSessionManager` 移除 `ActiveTriggerManager` 和 `ActiveInteractionManager` 依赖

### 删除 ActiveInteractionManager
- 移除 `active_interaction_manager.cc/h`（共 392 行）
- 主动交互逻辑（基于会话事件的 LLM 回调）不再维护

### SDL/UI 渲染重构
- `VirtualSprite` 移除 `RegisterAgentOutputCallback()` / `DispatchSessionStates()` / `RegisterMessageSubmitCallback()`，状态路由改为快照驱动
- 静态方法 `RenderHome` / `RenderVirtualHuman` / `RenderGalgame` / `RenderTerminal` 内联到 `Render()` switch 分支，不再作为独立方法
- 移除 `session_states_` 队列和 `session_states_mutex_` 线程安全队列（由 RenderSnapshot 替代）
- `UIRenderer` 精简：移除 `SendToChatPanel / StartAssistantMessage / UpdateLastMessage / SubmitUserMessage / ClearHistory / SetAgentState` 等 7 个方法
- 新增 `SetSnapshotGetter()`，渲染器在 `RenderImGui()` 时通过回调取最新快照，不再持有状态副本
- `StatusBar` 从 `Drawer` 点阵绘制改为 `MediaUtil::DrawTextRect` 字体渲染，移除 `Drawer` 依赖
- 新增 `StateColor` 结构体和 `MakeVisualProps()` 工具函数，状态颜色定义集中到各自的 cpp 文件

### 角色状态可视化增强
- `agent_state_observer.cc` 新增 `GetStateVisualProps()` 本地函数，细化 13 种 AgentRuntimeState → 视觉属性映射（含 scarf/aura 颜色）
- 新增 streaming/deep thinking 状态颜色支持

### 未使用方法清理
- 移除 `galgame_mode.cc` 的 `ClearDialogue()` / `SetSpeakerColor()`
- 移除 `home_screen.h` 的 `DrawTitle()` / `DrawModeButtons()` / `DrawFooter()` 私有方法
- 移除 `banner.cc` 的 `PrintHelp()` 方法（`/help` 命令由 CommandRegistry 处理）
- 移除 `character_state_observer.cc` 中未使用的 `MapToCharacterState` 调用

### 其他
- `agent_types.h` 新增 `RenderSnapshot` 快照结构体（含 session_id / role_id / state / messages / streaming_text / streaming_thinking）
- `agent_core.h` 移除已废弃的静态方法声明
- `media_engine/CMakeLists.txt` 新增 ZLIB 链接依赖
- `layout_config.h` 新增办公区地砖尺寸常量和区域计算方法
- README.md 更新「输入模型与中断策略」章节，文档化 pending buffer 模式

### 文件统计
- 变更文件：31 个
- 新增：+871 行
- 删除：-1,303 行
- 净变化：-432 行（持续精简）

---

## [2026-05-07] - 会话系统重构与多角色渲染

### 引擎多会话 API
- `AgentEngine` 新增 `CreateSession()` / `SendMessage(session_id, text)` / `StopSession(session_id)` 多会话公开接口，为 server/多角色场景准备
- `current_session_id_` 更名为 `focused_session_id_`，明确单会话便捷接口的语义范围
- `OutputCallback` 签名增加 `session_id` 和 `role_id` 参数，下行通道支持多 session 路由

### 会话管理器重构
- `AgentSessionManager` 移除 `MemoryManager` 依赖和 `SwitchMemoryContext()`，职责简化
- `sessions_` 容器从 `unordered_map<string, AgentSession>` 改为 `unordered_map<string, unique_ptr<AgentSession>>`，确保 session 指针地址稳定
- 新增 `session_mutex` per-session 锁，保证同一 session 的 `AgentCore::Loop` 调用串行执行
- `auto_confirm_tools` 改为 per-session 临时提权：创建 session 时包装 tool_executor，调用期间临时切换 PermissionManager 模式后恢复，不再影响全局
- `GetSession()` 增加 `mutex_` 保护，线程安全

### 角色状态可视化多实例
- `AgentStateVisualizer` 新增 `GetOrCreate(role_id)` 工厂方法，每个角色独立实例
- `UpdateAll()` / `RenderAll()` 静态方法，支持多角色并行更新和渲染
- `VirtualSprite` 输出回调改为写入 `session_states_` 队列，`DispatchSessionStates()` 在渲染循环中统一派发到对应角色的 visualizer

### llama.cpp 脚本精简与迁移
- 启动脚本从 `scripts/` 迁移至 `config/.prosophor/scripts/`，随配置目录一同分发
- 启动脚本去掉 jq/python3 配置解析逻辑，改为纯参数传递，脚本职责单一化
- 删除 Windows `.bat` 版本（`start_llamacpp_server.bat`、`stop_llamacpp_server.bat`）和旧版 `start_llamacpp_server.sh`
- `LocalModelManager::Start()` 改为调用外部脚本而非内联构造参数
- 删除 CMake 中脚本安装规则（脚本已随 config 目录安装）

### 平台层清理
- 新增 `platform::NullDevice()` 跨平台空设备路径（POSIX `/dev/null` / Windows `NUL`）
- `CheckPortOpen()` / `WaitForHealth()` 改用 `NullDevice()` 代替硬编码路径
- `LaunchDetachedCommand()` 大幅简化：删除 Windows `CreateProcess` 内联实现（改由 shell 脚本处理后台逻辑），统一为 `RunShellCommand`

### 文件统计
- 变更文件：19 个
- 新增：+281 行
- 删除：-416 行
- 净变化：-135 行（持续精简）

---

## [2026-05-06] - 核心引擎抽象与前后端分离

### 核心架构重构 - prosophor_core
- 所有业务逻辑集中到 `prosophor_core/` 目录，构建为静态库，零 UI/SDL 依赖
- 新增 `AgentEngine` 单例 — 统一核心入口，管理 MemoryManager、ToolRegistry、SessionManager、ProviderRouter、LSP、Config、LocalModelManager
- 新增 `agent_types.h` — `AgentRuntimeState` 从 `ui_types.h` 迁入核心层，消除 UI 对核心状态枚举的依赖
- 通过 `SetOutputCallback()` / `SetPermissionCallback()` 实现前端无关的回调注册，TUI 和 SDL 共用同一引擎

### TUI 前端重构 - AiCoding
- `AiCoding` 替代 `AgentCommander`，全新的终端输入循环（`InputHandler` + `InputEvent`）
- 注册引擎回调：输出流式渲染（thinking/content/tool/complete 各阶段）、权限交互确认
- `banner.cc/h` 从 `common/` 迁至 `ai_coding/`（终端专属）

### SDL 前端重构 - VirtualSprite
- `SdlApp` 更名为 `VirtualSprite`，重新设计为 AgentEngine 的前端消费层
- 模式切换：HOME / VIRTUAL_HUMAN / GALGAME / TERMINAL
- 通过 `RegisterAgentOutputCallback()` + `RegisterMessageSubmitCallback()` 挂载引擎回调
- 状态可视化属性内联（移除 `agent_state_visualizer.h`）

### 构建系统重构
- `main_src/CMakeLists.txt` 完全重写，分为三个目标：`prosophor_core`（静态库）、TUI `prosophor`、SDL `prosophor`
- TUI 构建仅链接 prosophor_core + ai_coding，无 SDL/media/scene 依赖
- SDL 构建链接 prosophor_core + media_engine + scene + virtual_sprite + components
- 平台源文件（input_handler/pipe_handler）按平台正确选择
- `tests/CMakeLists.txt` 新增 prosophor_core include 路径
- 顶层 `CMakeLists.txt`：安装 llama.cpp 启停脚本

### 删除模块
- 移除 `cli/agent_commander.cc/h`（功能由 AiCoding 替代）
- 移除 `tools/tool_registry.cc/h`（功能由 AgentEngine 封装）
- 移除 `tools/command_tools/background_run_tool.cc/h`
- 移除 `core/agent_state_visualizer.h`
- 移除 `input_event.h` 中的 `OutputEvent`、`InputEventCallback`、`OutputEventCallback`（移至前端层）

### llama.cpp 管理脚本
- 新增 `scripts/start_llamacpp_server.sh` / `.bat` — 从 settings.json 读取配置，自动查找二进制与模型文件，后台启动并输出 PID
- 新增 `scripts/stop_llamacpp_server.sh` / `.bat` — 按 PID 停止，支持 `--force`

### 其他
- media_engine UI 组件内部 include 移除 `media/` 前缀（`media/colors.h` → `colors.h`）
- `agent_state_observer.h` 改为引用 `core/agent_types.h`

### 文件统计
- 变更文件：109 个
- 新增：+1,069 行
- 删除：-2,954 行
- 净变化：-1,885 行（大幅精简）

---

## [2026-05-05] - 管道抽象层与跨平台完善

### 管道与进程抽象 (pipe_handler)
- 新增 `platform/pipe_handler.h` + `pipe_handler_posix.cc` + `pipe_handler_win32.cc`
- 统一 pipe/fork/exec/wait 跨平台接口，消除 LSP/MCP 中的 `#ifndef _WIN32` 条件编译
- `ForkAndExec()`：POSIX 用 fork+execvp，Windows 用 CreateProcess + 管道句柄转 fd
- `CreatePipe/ClosePipe/ReadPipe/WritePipe/Dup2Pipe/WaitProcess/GetCurrentPid` 全平台抽象
- `SetPipeNonBlocking/IsPipeWouldBlock/GetPipeErrorString` 统一错误处理

### MCP Client 重构
- `mcp_client.cc` 移除内联 POSIX pipe/fork/kill/wait 代码，全部改用 `platform::ForkAndExec/KillProcess/ClosePipe/ReadPipe/WritePipe`
- 消除全部 `#ifndef _WIN32` / `#else` 块，净减 ~90 行

### LSP Manager 重构
- `lsp_manager.cc` 同样替换为 `platform::ForkAndExec/WritePipe/ReadPipe/ClosePipe`
- 移除 POSIX 头文件依赖 (`unistd.h`, `sys/wait.h`)，净减 ~50 行

### 平台层增强
- `platform.cc/h` 新增：
  - `NormalizePath()` — Windows MinGW 下 POSIX 路径 `/x/...` → `X:\...` 转换
  - `PathExists()` — 带平台路径归一化的文件存在检查
  - `SelectPlatformPath()` — 编译期跨平台路径选择（无需 `#ifdef`）
  - `LaunchDetachedCommand()` — 分离后台进程启动
  - `ExecuteScriptWithTimeout()` — 从 `subprocess_wrapper` 迁移而来
- Windows: `SetConsoleUtf8()` 增加 ANSI/VT 转义序列支持；`GetSelfExePath()` 增加 Windows 实现
- Windows: `LaunchProcess()`/`LaunchDetachedCommand()` 自动搜索 MinGW bin 目录加入 PATH，确保子进程能找到运行时 DLL

### 本地模型管理优化
- `local_model_manager.cc`：`LaunchProcess(args)` → `LaunchDetachedCommand(shell_cmd)`，使用 `ShellEscape` 防止路径注入
- 健康检查改为等待模型真正加载完成（`/health` 返回 200 而非仅端口开放），超时失败自动 `Stop()`
- 新增超时日志节流，每 10s 打印一次等待状态
- `local_model_utils.cc`：移除 `ResolveModelPath()`（路径归一化统一由 `platform::NormalizePath` 处理）
- 新增 `.exe` 搜索路径变体，新增 `build/` 和 `build_win/` 构建目录搜索

### 配置更新
- `LocalModelConfig` 新增 `model_path_for_win` 字段，跨平台双路径支持
- `config.cc` 加载配置时自动调用 `SelectPlatformPath` + `NormalizePath`
- `settings.json`：`model_path` 改为 `../llama_cpp_model/...`，新增 `model_path_for_win`，`auto_start: true`
- 新增默认 OpenAI provider 配置条目

### 构建系统
- 顶层 `CMakeLists.txt`：Windows 编译定义 `_WIN32_WINNT=0x0A00`、`_USE_MATH_DEFINES`、`M_PI`
- `main_src/CMakeLists.txt`：新增 pipe_handler 源文件平台过滤与选择
- `Makefile`：移除 `run_llamacpp_server` 目标

### subprocess_wrapper 移除
- `subprocess_wrapper.cc/h` 删除，功能完整迁移至 `platform::ExecuteScriptWithTimeout`

### 其他
- `cpu_temperature_monitor/trigger.py` 修复退出码语义：正常不触发 → `exit(0)`，触发 → `exit(1)`
- `images/demo.png`、`images/win_demo.png` 从 `docs/` 迁移至 `images/`

### 文件统计
- 变更文件：23 个
- 新增：+782 行
- 删除：-377 行
- 净变化：+405 行

---

## [2026-05-04] - 本地模型支持与平台抽象层

### 本地模型支持 (llama.cpp 集成)
- **LocalModelManager**：llama-server 完整生命周期管理（start/stop/restart/status）
- 新增 `/server` 命令（别名 `/local`），支持 `start|stop|status|restart` 子命令
- 新增 `/setup` 一键配置：自动检测硬件（NVIDIA GPU / Apple Silicon / CPU 线程数）+ 扫描 .gguf 模型 + 生成配置
- 配置新增 `local_models` 数组，支持 model_path、port、auto_start、n_gpu_layers、n_threads、start_timeout_ms 等参数
- CMake 通过 `FetchContent` 可选编译 llama.cpp（`-DPROSOPHOR_BUILD_LLAMA=ON/OFF`）
- `auto_start: true` 时启动自动拉起本地模型服务
- 新增 `local_model_utils`：硬件检测、llama-server 二进制查找、模型路径解析

### 平台抽象层 (platform/)
- 新增 `platform/platform.h` 和 `platform/platform.cc`，统一跨平台 API：编码转换、终端 I/O、进程管理、Shell 执行
- 编译期平台常量：`kIsWindows`、`kIsLinux`、`kIsMacOS`
- `input_handler` 系列从 `cli/` 迁移至 `platform/`（`terminal_input.cc` 同步更新 include 路径）
- 消除 `#ifdef _WIN32` / `#ifdef` 条件编译，统一改为 platform API 调用，涉及：
  - `agent_commander.cc` — `ReadConsoleLine()`、`kIsWindows`
  - `subprocess_wrapper.cc` — 进程启动/管理
  - `tts_speaker.cc` — 音频播放
  - `main.cc` — `SetConsoleUtf8()`
  - `config.cc` — `GetHomeDir()`
  - `file_utils.cc` — `GetHomeDir()`
  - `input_event.h` — NOMINMAX/WIN32_LEAN_AND_MEAN（改为 include platform.h）
  - `time_wrapper.h` — `LocalTime()`
  - `skill_loader.cc` — `IsBinaryAvailable()`、`GetCurrentOs()`
  - `lsp_manager.cc` — `StartServerForFile()`、`InitializeServer()`、`ShutdownAll()`，移除 Windows `io.h` 兼容宏
  - `anthropic_provider.cc`、`ollama_provider.cc` — `ConvertToUtf8` 不再条件编译

### README 重构
- README.md / README_cn.md 大幅精简，移除冗长功能罗列和配置参考表格
- 新增 llama.cpp 本地模型文档
- 更新架构图、工具列表、模块布局、构建说明

### 其他
- `tool_registry.cc` 精简（-259 行）
- `command_registry.cc` 新增 `/server`、`/setup` 命令处理（+255 行）
- `string_utils.cc` 重构（80 行变更）
- `config.cc/h` 新增 `LocalModelConfig` 结构体及 JSON 序列化
- `Makefile`：默认 `-DPROSOPHOR_BUILD_LLAMA=OFF`，`run_llamacpp_server` 使用 build 输出的 llama-server
- `llm_provider.cc`：Token 用量日志级别 INFO → DEBUG

### 文件统计
- 变更文件：35 个
- 新增：+1,719 行
- 删除：-1,604 行
- 净变化：+115 行

---

## [2026-05-04] - Provider 接口统一与状态回调优化

### HttpClient 单例化与接口规范化
- HttpClient 从静态方法改为单例模式 (`HttpClient::Instance()`)
- `HttpRequest::post_data` → `body`，语义更清晰
- `HttpResponse::error` → `error_msg`，新增 `curl_code` 字段精确区分错误来源
- `success()`/`failed()` 判断逻辑增强（同时检查 `curl_code` 与 `status_code`）
- 新增 `StreamPhase` 枚举，用于流式输出的 thinking/content/tool_calls 阶段追踪
- `StreamHandler::OnEvent()` 改为纯虚函数，`OnStreamEnd()` 移除，接口更简洁
- `SseStreamHandler` 字段命名规范化（移除尾缀下划线），新增 `PendingToolCall` 结构

### Provider Thinking 配置重构
- OpenAI Provider: `thinking` 配置从字符串枚举 (`"off"/"low"/"medium"/"high"`) 改为布尔值
- `ShouldEnableThinking()` 参数类型同步更新，移除冗余的映射逻辑
- `ThinkingToReasoningEffort()` 简化，固定返回 `"medium"`
- 响应反序列化新增 `thinking` 类型 content block 识别
- 修复 OpenAI Provider 在 thinking 关闭时未正确设置 `enable_thinking=false` 的问题

### 状态回调分发优化
- `agent_commander.cc` 状态输出从 if-else 链重构为 `switch` 语句，可读性与性能提升
- SDL 模式 UI 回调同样迁移为 `switch` 结构
- 终端 thinking 标签格式调整：前后增加空格（`<thinking> ` / ` </thinking>`）
- 移除工作区初始化时自动创建 `AGENTS.md` 的逻辑

### 构建与配置
- `settings.json` 中所有 provider 的 `thinking` 字段统一为布尔值 (`true`/`false`)
- 修复重复 model list 问题

### 文件统计
- 变更文件：51 个
- 新增：+1,044 行
- 删除：-1,562 行
- 净变化：-518 行（持续精简）

---

## [2026-05-03] - 回调流程优化

### 状态枚举重构
- `AgentRuntimeState::THINKING` → `BEGINNING`
- `AgentRuntimeState::TOOL_MSG` → `TOOL_USE`
- 移除冗余的 `STREAM_MODE_START` 状态
- 影响范围：`ui_types.h`、`agent_core.cc`、`agent_commander.cc`、`status_bar.cc`、`agent_state_visualizer.h`、`agent_state_observer.cc`、`anime_character.cc`、`character_state_observer.cc`、`sdl_app.cc`

### Provider 流式解析重构
- 新增 `providers/detail/anthropic_stream_handler.h` (131 行) — Anthropic SSE 流处理器
- 新增 `providers/detail/ollama_stream_handler.h` (144 行) — Ollama SSE 流处理器
- 新增 `providers/detail/openai_stream_handler.h` (169 行) — OpenAI SSE 流处理器
- `anthropic_provider.cc` 减少 ~205 行，`openai_provider.cc` 减少 ~208 行，`ollama_provider.cc` 减少 ~142 行
- 各 Provider 类职责更单一，流式解析逻辑与请求逻辑分离

### 流式回调逻辑优化
- `agent_core.cc` 中 thinking/content 阶段通过 `content_phase` 字段（"start"/"delta"/"end"）区分
- 消息历史仅在关键状态节点写入（COMPLETE, TOOL_USE, ERROR），流式中间态不再写入
- `agent_commander.cc` 终端输出流格式调整，thinking 标签包裹改进
- `agent_session.h` provider 配置兜底逻辑修复

### 测试清理
- 移除 `tests/agent_core_test.cc`、`tests/compact_service_test.cc`、`tests/tool_registry_test.cc`、`tests/main.cc`
- 移除 `tools/verify.sh`

### 构建与配置
- `CMakeLists.txt`、`Makefile` 适配新文件结构
- `settings.json` 扩展，`.gitignore` 更新

---

## [2026-04-30] - v0.4.0 重大重构

### 新增功能
- **OpenAI Provider**: 新增 OpenAI 兼容接口支持 (`providers/openai_provider.cc/h`)
- **语音合成 (TTS)**: 新增文本转语音播报功能 (`common/tts_speaker.cc/h`)
- **Galgame 模式**: 新增美少女游戏风格界面 (`scene/galgame_mode.cc/h`)
- **Anime 角色系统**: 新增动画角色渲染 (`scene/anime_character.cc/h`)
- **Home Screen**: 新增主页场景 (`scene/home_screen.cc/h`)
- **Media Engine UI 组件**: 从 `components/` 迁移并重构 UI 组件到 `media_engine/ui_component/`
  - `header_bar.cc/h` - 顶部导航栏
  - `input_panel.cc/h` - 输入面板
  - `ui_container.cc/h` - UI 容器
  - `ui_panel.cc/h` - 通用面板
- **配置增强**: `config.cc/h` 大幅扩展配置项支持

### 重构优化
- **架构重构 - UI 模块**:
  - `components/input_panel.cc/h` → `media_engine/ui_component/input_panel.cc/h`
  - `components/ui_panel.cc/h` → `media_engine/ui_component/ui_panel.cc/h`
  - 移除旧版 `components/ui_panel.h`、`components/ui_types.h` 中的废弃接口
- **Provider 体系重构**:
  - 新增 `providers/openai_provider.cc` (479 行)，统一兼容 OpenAI 格式接口
  - 移除 `providers/qwen_provider.cc/h` (Qwen 功能合并至 OpenAI 兼容模式)
  - 重构 `anthropic_provider.cc/h`、`ollama_provider.cc/h`、`llm_provider.cc/h`
- **工具系统大规模精简**: 删除 20 个工具文件
  - 移除 `agent_tool` (subagent 协调工具)
  - 移除 `ask_user_question_tool` (交互工具)
  - 移除 `lsp_tool` (LSP 语言服务器工具)
  - 移除 `glob_tool`、`grep_tool` (搜索工具)
  - 移除 `cron_tool`、`task_tool`、`todo_write_tool` (任务管理工具)
  - 移除 `worktree_tool` (Git worktree 工具)
  - `tool_registry.cc` 从 ~1070 行变更，大幅精简
- **管理器精简**:
  - 移除 `buddy_manager.cc/h`、`buddy_types.cc/h` (伙伴系统)
  - 移除 `worktree_manager.cc/h` (worktree 管理)
- **CLI 精简**: `command_registry.cc` 删除 ~121 行冗余代码

### 性能与质量
- `agent_core.cc` 核心逻辑重构优化 (~194 行变更)
- `agent_session_manager.cc` 会话管理增强 (~135 行变更)
- `agent_role_loader.cc` 角色加载逻辑改进 (~90 行变更)
- `agent_state_observer.cc` 状态同步机制优化 (~205 行变更)
- `sdl_app.cc` SDL 应用框架增强 (~184 行变更)

### 配置更新
- `config/.prosophor/settings.json` 大幅扩展 (126 行变更)
- `config/.prosophor/roles/` 下所有角色配置微调 (architect, coder, default, reviewer, teacher)
- `CMakeLists.txt` 构建系统更新，适配新文件结构
- `.gitignore` 忽略规则更新

### 文件统计
- 变更文件：110 个
- 新增：+4,359 行
- 删除：-6,575 行
- 净变化：-2,216 行（大规模精简）

---

## [2026-04-19] - 重大更新，增加 UI

### 新增功能
- **UI 系统**: 基于 SDL + ImGui 的完整 UI 界面
  - 聊天面板 (chat_panel.cc/h)
  - 输入面板 (input_panel.cc/h)
  - 状态栏 (status_bar.cc/h)
  - 通用 UI 面板 (ui_panel.cc/h)
- **角色系统**: 新增 5 种 AI 角色配置
  - architect.md - 架构师角色
  - coder.md - 程序员角色
  - default.md - 默认角色
  - reviewer.md - 代码审查角色
  - teacher.md - 教学角色
- **场景系统**: 办公室场景渲染
  - 角色精灵 (character_sprite.cc/h)
  - 办公室背景 (office_background.cc/h)
  - 角色管理器 (office_character_manager.cc/h)
  - UI 渲染器 (ui_renderer.cc/h)
- **Agent 状态观察器**: 实时同步 Agent 状态到 UI
- **媒体引擎**: SDL 封装层
  - 音频 (audior.cc/h)
  - 绘图 (drawer.cc/h)
  - 字体 (font.cc/h)
  - 纹理 (texture.cc/h)
  - 颜色系统 (colors.h)
- **输入系统**: Windows 终端输入支持 (terminal_input.cc)
- **输出管理**: 统一输出管理 (output_manager.cc/h)
- **Provider 路由**: 多 LLM Provider 支持 (provider_router.cc/h)
- **Agent 角色加载器**: 动态加载角色配置 (agent_role_loader.cc/h)
- **Agent 会话管理**: 独立会话管理模块 (agent_session_manager.cc/h)
- **线程池**: 通用线程池实现 (thread_pool.h)
- **输入事件系统**: 统一输入事件定义 (input_event.h)
- **内存整合服务**: 自动内存整合 (memory_consolidation_service.cc/h)

### 重构优化
- **架构调整**: 核心模块拆分重组
  - `core/` → `cli/`: agent_commander, command_registry
  - `core/` → `common/`: messages_schema
  - `core/` → `managers/`: session_manager, skill_loader
  - `tools/` → 分类目录: agent_tools, command_tools, lsp_tools, search_tools, task_tools, worktree_tools
- **AgentCommander**: 从 core 移至 cli，代码重构 (462 行新增)
- **CommandRegistry**: 大规模重构 (438 行变更)
- **AgentCore**: 精简优化 (555 行变更)
- **ToolRegistry**: 扩展支持 (1927 行变更)
- **CurlClient**: 增强连接处理 (161 行变更)
- **TimeWrapper**: 时间处理优化 (180 行变更)

### 配置更新
- **CMakeLists.txt**: 构建系统升级
- **Makefile**: 编译配置优化
- **settings.json**: Claude Code 配置更新
- **.gitignore**: 忽略规则更新

### 文件统计
- 新增文件：140 个
- 修改文件：12953 行新增，2948 行删除
- 核心变更：UI 系统、场景渲染、角色系统、媒体引擎

---

## [2026-04-19] - 增加 Provider 请求报错信息，连接超时设置

### 优化
- 改进 LLM Provider 请求错误信息提示
- 增加连接超时配置

---

## [2026-04-19] - 支持多 LLM Provider 切换

### 新增
- 支持 Ollama Provider
- 支持 Qwen Provider
- Provider 动态切换

---

## [2026-04-19] - 适配 Windows 平台

### 兼容性
- Windows 平台适配
- 跨平台支持优化

---

## [2026-04-19] - 初始化版本

### 初始功能
- 基础 Agent 框架
- 工具系统集成
- 会话管理

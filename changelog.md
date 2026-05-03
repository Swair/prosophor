# Changelog

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

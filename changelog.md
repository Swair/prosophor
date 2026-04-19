# Changelog

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

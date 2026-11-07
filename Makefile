# ==============================================================================
# Prosophor Makefile
# ==============================================================================
# 编排：目标名不区分平台（build_tui / build_sdl / build_web / build_nutribot），
# 平台在 Makefile 内部按当前主机自适应；Windows 变体分 fast / full。
#
# 常用入口：
#   make build_tui           TUI（默认 full；fast 变体：build_tui_fast）
#   make build_sdl           SDL 桌面宠物（默认 full；fast 变体：build_sdl_fast）
#   make build_web           Web 多用户服务端
#   make help                列出所有入口
# ==============================================================================

PROJECT_DIR ?= $(abspath ./)
CMAKE ?= cmake
MAKE ?= make
NUM_JOB ?= 8

PACKAGE_NAME ?= Prosophor
PACKAGE_VERSION ?= 0.8.1
# BUILD_TYPE ?= RelWithDebInfo
BUILD_TYPE ?= Debug

# 主机平台检测：目标名不区分平台，由这里选择实现
ifeq ($(OS),Windows_NT)
  HOST_PLATFORM := win
else
  HOST_PLATFORM := unix
endif

all: help
.PHONY: all

help:
	@echo "Prosophor build entry points (platform detected automatically):"
	@echo "  TUI      : build_tui, build_tui_fast, build_tui_full (+ run_/clean_)"
	@echo "  SDL      : build_sdl, build_sdl_fast, build_sdl_full (+ run_/clean_)"
	@echo "  Web      : build_web, run_web, clean_web"
	@echo "  Package  : package_sdl_full, package_sdl_fast (Windows only)"
	@echo "  Deploy   : deploy_sdl_full, deploy_all_sdl_full (Windows only)"
	@echo "  Tests    : tests"
	@echo "  NutriBot : build_nutribot, run_nutribot, test_nutribot, clean_nutribot"
.PHONY: help



# ==============================================================================
# 平台配置与构建宏（目标名不区分平台，平台差异收敛在这里）
# ==============================================================================
# GPU 后端由 CMakeLists 按"工具链是否可用"自动探测，优先级 CUDA → Vulkan → CPU，
# 不区分平台（Windows 下 CUDA 需要 MSVC host 编译器，MinGW 会落回 Vulkan）。
# Makefile 只传语义开关（LLAMA 开/关、SDL_UI 开/关），不出现平台参数。

ifeq ($(HOST_PLATFORM),win)
# ---- Windows（MSYS2/MinGW）----
# Set PATH for MSYS2/MinGW tools and NVIDIA utilities (nvidia-smi)
export PATH := /c/Windows/System32:/e/devtool/msys64/mingw64/bin:$(PATH)

CMAKE_GENERATOR := Ninja
RUN_ENV := SSL_CERT_FILE=ca-bundle.crt
EXE_SUFFIX := .exe
# Windows SDL 默认带本地 TTS/ASR（sherpa-onnx 预编译运行时）
SHERPA_VOICE_FLAG := -DPROSOPHOR_BUILD_SHERPA_VOICE=ON
else
# ---- Unix（Linux / macOS）----
CMAKE_GENERATOR :=
RUN_ENV :=
EXE_SUFFIX :=
SHERPA_VOICE_FLAG :=
endif

CMAKE_ARGS ?= \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	-DPACKAGE_VERSION=$(PACKAGE_VERSION) \
	$(CMAKE_EXTRA_ARGS)

# build_form_variant 参数:
#   $(1)=构建目录 $(2)=LLAMA $(3)=SDL_UI $(4)=额外参数
define build_form_variant
	mkdir -p $(1) && \
	cd $(1) && \
	$(CMAKE) $(CMAKE_ARGS) $(if $(CMAKE_GENERATOR),-G $(CMAKE_GENERATOR)) \
		-DCMAKE_INSTALL_PREFIX=$(1)/install \
		-DPROSOPHOR_BUILD_LLAMA=$(2) \
		-DPROSOPHOR_SDL_UI=$(3) $(4) $(PROJECT_DIR) && \
	$(CMAKE) --build . --parallel $(NUM_JOB) && \
	$(CMAKE) --install .
endef

# run_variant 参数: $(1)=安装目录 $(2)=可执行文件名（不含后缀） $(3)=启动参数
define run_variant
	cd $(1)/bin && $(RUN_ENV) ./$(2)$(EXE_SUFFIX) $(3)
endef

# clean_variant 参数: $(1)=构建目录
define clean_variant
	rm -rf $(1)
endef


# ==============================================================================
# 构建目录（中性命名，不含平台标记；变体目录都从 BUILD_DIR 派生）
# ==============================================================================

BUILD_DIR ?= $(PROJECT_DIR)/build

BUILD_DIR_TUI_FAST ?= $(BUILD_DIR)/tui_fast
BUILD_DIR_TUI_FULL ?= $(BUILD_DIR)/tui_full
BUILD_DIR_SDL_FAST ?= $(BUILD_DIR)/sdl_fast
BUILD_DIR_SDL_FULL ?= $(BUILD_DIR)/sdl_full
BUILD_DIR_WEB      ?= $(BUILD_DIR)/web

INSTALL_DIR_TUI_FAST ?= $(BUILD_DIR_TUI_FAST)/install
INSTALL_DIR_TUI_FULL ?= $(BUILD_DIR_TUI_FULL)/install
INSTALL_DIR_SDL_FAST ?= $(BUILD_DIR_SDL_FAST)/install
INSTALL_DIR_SDL_FULL ?= $(BUILD_DIR_SDL_FULL)/install
INSTALL_DIR_WEB      ?= $(BUILD_DIR_WEB)/install


# ==============================================================================
# 形态一：TUI（纯终端）
# ==============================================================================

build_tui: build_tui_full
.PHONY: build_tui

# tui_fast: 纯终端 + 仅远程 API，无本地模型
build_tui_fast:
	$(call build_form_variant,$(BUILD_DIR_TUI_FAST),OFF,OFF)
run_tui_fast:
	$(call run_variant,$(INSTALL_DIR_TUI_FAST),prosophor)
clean_tui_fast:
	$(call clean_variant,$(BUILD_DIR_TUI_FAST))
.PHONY: build_tui_fast run_tui_fast clean_tui_fast

# tui_full: 纯终端 + 本地模型（GPU 后端自动探测：CUDA → Vulkan → CPU）
build_tui_full:
	$(call build_form_variant,$(BUILD_DIR_TUI_FULL),ON,OFF)
run_tui_full:
	$(call run_variant,$(INSTALL_DIR_TUI_FULL),prosophor)
clean_tui_full:
	$(call clean_variant,$(BUILD_DIR_TUI_FULL))
.PHONY: build_tui_full run_tui_full clean_tui_full

run_tui: run_tui_full
.PHONY: run_tui

clean_tui: clean_tui_full
.PHONY: clean_tui


# ==============================================================================
# 形态二：SDL（桌面宠物 / GUI）
# ==============================================================================
# SDL3 等依赖由 media_engine 通过 FetchContent 构建。

build_sdl: build_sdl_full
.PHONY: build_sdl

# sdl_fast: 桌面宠物 + 仅远程 API，无本地模型
build_sdl_fast:
	$(call build_form_variant,$(BUILD_DIR_SDL_FAST),OFF,ON,$(SHERPA_VOICE_FLAG))
run_sdl_fast:
	$(call run_variant,$(INSTALL_DIR_SDL_FAST),prosophor)
clean_sdl_fast:
	$(call clean_variant,$(BUILD_DIR_SDL_FAST))
.PHONY: build_sdl_fast run_sdl_fast clean_sdl_fast

# sdl_full: 桌面宠物 + 本地模型（GPU 后端自动探测：CUDA → Vulkan → CPU）
build_sdl_full:
	$(call build_form_variant,$(BUILD_DIR_SDL_FULL),ON,ON,$(SHERPA_VOICE_FLAG))
run_sdl_full:
	$(call run_variant,$(INSTALL_DIR_SDL_FULL),prosophor)
clean_sdl_full:
	$(call clean_variant,$(BUILD_DIR_SDL_FULL))
.PHONY: build_sdl_full run_sdl_full clean_sdl_full

run_sdl: run_sdl_full
.PHONY: run_sdl

clean_sdl: clean_sdl_full
.PHONY: clean_sdl


# ==============================================================================
# 形态三：Web 服务端（多用户，无 SDL，无本地模型；前端 npm 构建）
# ==============================================================================

build_web:
	$(call build_form_variant,$(BUILD_DIR_WEB),OFF,OFF,-DPROSOPHOR_BUILD_WEB_FRONTEND=ON)
run_web:
	$(call run_variant,$(INSTALL_DIR_WEB),prosophor_web,--host 0.0.0.0)
clean_web:
	$(call clean_variant,$(BUILD_DIR_WEB))
.PHONY: build_web run_web clean_web


# ==============================================================================
# 发布：NSIS 安装包 + GitHub / Gitee Releases（仅 Windows）
# ==============================================================================

ifeq ($(HOST_PLATFORM),win)

NSIS ?= /e/devtool/msys64/mingw64/bin/makensis.exe
PACKAGE_DIR ?= $(PROJECT_DIR)/tool/packaging

define do_package
	cp /e/devtool/msys64/mingw64/etc/ssl/certs/ca-bundle.crt $(1)/bin/
	cd $(PACKAGE_DIR) && "$(NSIS)" \
		-DPRODUCT_VERSION=$(PACKAGE_VERSION) \
		-DBIN_DIR="$(strip $(1))\bin" \
		$(2) \
		installer.nsi
	mv $(PACKAGE_DIR)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)-win64-setup.exe $(1)/
	@echo "=== Installer created: $(1)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)-win64-setup.exe ==="
endef

package_sdl_full: build_sdl_full
	$(call do_package,$(INSTALL_DIR_SDL_FULL),)
	@echo "Variant: sdl_full (full feature)"
.PHONY: package_sdl_full

package_sdl_fast: build_sdl_fast
	$(call do_package,$(INSTALL_DIR_SDL_FAST),)
	@echo "Variant: sdl_fast (no local LLM)"
.PHONY: package_sdl_fast

# ---- GitHub Release ----

CHANGELOG_NOTES ?= $(BUILD_DIR_SDL_FULL)/release_notes.md

define do_deploy
	python "$(PROJECT_DIR)/tool/extract_changelog.py" $(PACKAGE_VERSION) > "$(CHANGELOG_NOTES)" 2>/dev/null; \
	if [ -s "$(CHANGELOG_NOTES)" ]; then \
	  NOTES_OPT="--notes-file \"$(CHANGELOG_NOTES)\""; \
	else \
	  NOTES_OPT="--generate-notes"; \
	fi; \
	gh release create v$(PACKAGE_VERSION) \
	  $(1)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)-win64-setup.exe \
	  --title "v$(PACKAGE_VERSION) ($(2))" \
	  $$NOTES_OPT
endef

deploy_sdl_full: package_sdl_full
	$(call do_deploy,$(INSTALL_DIR_SDL_FULL),sdl_full)
.PHONY: deploy_sdl_full

deploy_sdl_fast: package_sdl_fast
	$(call do_deploy,$(INSTALL_DIR_SDL_FAST),sdl_fast)
.PHONY: deploy_sdl_fast

# ---- Gitee Release ----

GITEE_API ?= https://gitee.com/api/v5
GITEE_OWNER ?= swair
GITEE_REPO ?= prosophor
GITEE_TOKEN ?= $(GITEE_ACCESS_TOKEN)
GITEE_TARGET_COMMITISH ?= main
GITEE_MAX_ASSET_BYTES ?= 104857600
GITEE_RELEASE_BODY ?= Release v$(PACKAGE_VERSION)

# 构建+NSIS 安装包+发布到 Gitee 发行版（需要先 git tag，需设置 GITEE_TOKEN）
deploy_gitee_sdl_full: package_sdl_full upload_gitee_sdl_full
.PHONY: deploy_gitee_sdl_full

deploy_gitee_sdl_fast: package_sdl_fast upload_gitee_sdl_fast
.PHONY: deploy_gitee_sdl_fast

# 仅发布已有安装包到 Gitee，避免 deploy_gitee_* 失败后重新打包
upload_gitee_sdl_full:
	$(call do_upload_gitee,$(INSTALL_DIR_SDL_FULL))
.PHONY: upload_gitee_sdl_full

upload_gitee_sdl_fast:
	$(call do_upload_gitee,$(INSTALL_DIR_SDL_FAST))
.PHONY: upload_gitee_sdl_fast

define do_upload_gitee
	@if [ -z "$(GITEE_TOKEN)" ]; then \
	  echo "GITEE_TOKEN is required. Example: make upload_gitee_sdl_full GITEE_TOKEN=xxxxx"; \
	  exit 1; \
	fi
	@if [ ! -f "$(1)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)-win64-setup.exe" ]; then \
	  echo "Installer not found: $(1)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)-win64-setup.exe"; \
	  exit 1; \
	fi
	@echo "Creating Gitee release v$(PACKAGE_VERSION)..."
	@response=$$(curl -sS -X POST "$(GITEE_API)/repos/$(GITEE_OWNER)/$(GITEE_REPO)/releases" \
	  -F "access_token=$(GITEE_TOKEN)" \
	  -F "tag_name=v$(PACKAGE_VERSION)" \
	  -F "target_commitish=$(GITEE_TARGET_COMMITISH)" \
	  -F "name=v$(PACKAGE_VERSION)" \
	  -F "body=$(GITEE_RELEASE_BODY)"); \
	release_id=$$(printf '%s' "$$response" | PYTHONIOENCODING=utf-8 python -c "import json,sys; data=json.load(sys.stdin); print(data.get('id',''))"); \
	if [ -z "$$release_id" ]; then \
	  printf '%s' "$$response" | PYTHONIOENCODING=utf-8 python -c "import json,sys; data=json.load(sys.stdin); print('Failed to create Gitee release: ' + str(data.get('message') or data.get('error') or data))"; \
	  exit 1; \
	fi; \
	installer="$(1)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)-win64-setup.exe"; \
	installer_size=$$(wc -c < "$$installer"); \
	if [ "$$installer_size" -gt "$(GITEE_MAX_ASSET_BYTES)" ]; then \
	  echo "Gitee release created: v$(PACKAGE_VERSION)"; \
	  echo "Skip Gitee asset upload: $$installer_size bytes exceeds 100 MB limit"; \
	  exit 0; \
	fi; \
	echo "Uploading installer to Gitee release $$release_id..."; \
	upload_response=$$(curl -sS -X POST "$(GITEE_API)/repos/$(GITEE_OWNER)/$(GITEE_REPO)/releases/$$release_id/attach_files" \
	  -F "access_token=$(GITEE_TOKEN)" \
	  -F "file=@$$installer"); \
	upload_message=$$(printf '%s' "$$upload_response" | PYTHONIOENCODING=utf-8 python -c "import json,sys; data=json.load(sys.stdin); print(data.get('message',''))"); \
	if [ -n "$$upload_message" ]; then \
	  printf '%s' "$$upload_response" | PYTHONIOENCODING=utf-8 python -c "import json,sys; data=json.load(sys.stdin); print('Failed to upload Gitee asset: ' + str(data.get('message') or data))"; \
	  exit 1; \
	fi; \
	echo "Gitee release and asset created: v$(PACKAGE_VERSION)"
endef

# 构建+NSIS 安装包+同时发布到 GitHub 和 Gitee
deploy_all_sdl_full: deploy_sdl_full deploy_gitee_sdl_full
.PHONY: deploy_all_sdl_full

deploy_all_sdl_fast: deploy_sdl_fast deploy_gitee_sdl_fast
.PHONY: deploy_all_sdl_fast

else

# Unix 主机不提供打包/发布入口
package_sdl_full package_sdl_fast \
deploy_sdl_full deploy_sdl_fast \
deploy_gitee_sdl_full deploy_gitee_sdl_fast \
deploy_all_sdl_full deploy_all_sdl_fast \
upload_gitee_sdl_full upload_gitee_sdl_fast:
	@echo "Packaging/deploy requires Windows (MSYS2/MinGW + NSIS)." >&2; exit 1
.PHONY: package_sdl_full package_sdl_fast deploy_sdl_full deploy_sdl_fast \
       deploy_gitee_sdl_full deploy_gitee_sdl_fast deploy_all_sdl_full deploy_all_sdl_fast \
       upload_gitee_sdl_full upload_gitee_sdl_fast

endif


# ==============================================================================
# 测试（独立 CMake 工程 tests/，安装到 bin/unitests）
# ==============================================================================
# NOTE: 动态库 (libwhisper.dll/ggml*.dll) 位于 bin/，需加入 PATH。
# 默认在 SDL full 变体的安装目录运行，可用 TEST_INSTALL_DIR 覆盖。

ifeq ($(HOST_PLATFORM),win)
  TESTS_GLOB := *.exe
else
  TESTS_GLOB := *
endif

TEST_INSTALL_DIR ?= $(INSTALL_DIR_SDL_FULL)

.PHONY: tests
tests:
	@echo "Running all tests in $(TEST_INSTALL_DIR)/bin/unitests..."
	@export PATH="$(TEST_INSTALL_DIR)/bin:$$PATH"; \
	for test in $(TEST_INSTALL_DIR)/bin/unitests/$(TESTS_GLOB); do \
	  [ -f "$$test" ] || continue; \
	  echo "Running $$(basename $$test)..."; \
	  "$$test" || exit 1; \
	done


# ==============================================================================
# 形态四：NutriBot（仿 tui/sdl：同一主树，编译开关切换形态，无独立模块构建）
#   LLM(aidgen) 已并入 prosophor_core/providers/llm；
#   入口统一为 main_src/main.cc（PROSOPHOR_NUTRIBOT 宏分支）。
#   QNN 平台形态默认：无 llamacpp、无 SDL。
#   高通板（Rhino Pi-X1 / QCS8550）是 aarch64 Linux，走 Unix 分支原生构建；
#   从 x86 交叉编译时通过 CMAKE_EXTRA_ARGS 传工具链：
#     make build_nutribot CMAKE_EXTRA_ARGS="-DCMAKE_TOOLCHAIN_FILE=..."
# ==============================================================================

BUILD_DIR_NUTRIBOT ?= $(BUILD_DIR)/nutribot
INSTALL_DIR_NUTRIBOT ?= $(BUILD_DIR_NUTRIBOT)/install
CMAKE_ARGS_NUTRIBOT ?= \
	-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR_NUTRIBOT) \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DPACKAGE_VERSION=$(PACKAGE_VERSION) \
	-DPROSOPHOR_NUTRIBOT=ON \
	-DPROSOPHOR_BUILD_LLAMA=OFF \
	-DPROSOPHOR_SDL_UI=OFF \
	$(CMAKE_EXTRA_ARGS)

build_nutribot:
	mkdir -p $(BUILD_DIR_NUTRIBOT) && \
	cd $(BUILD_DIR_NUTRIBOT) && \
	$(CMAKE) $(CMAKE_ARGS_NUTRIBOT) $(if $(CMAKE_GENERATOR),-G $(CMAKE_GENERATOR)) $(PROJECT_DIR) && \
	$(CMAKE) --build . --parallel $(NUM_JOB)
.PHONY: build_nutribot

run_nutribot:
	$(BUILD_DIR_NUTRIBOT)/main_src/prosophor$(EXE_SUFFIX)
.PHONY: run_nutribot

# nutribot 单测（独立于主树构建，同源收集；用例数见运行输出）
test_nutribot:
	mkdir -p $(BUILD_DIR_NUTRIBOT)/tests && \
	cd $(BUILD_DIR_NUTRIBOT)/tests && \
	$(CMAKE) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(PROJECT_DIR)/tests/nutribot && \
	$(CMAKE) --build . --parallel $(NUM_JOB) && ./nr_tests
.PHONY: test_nutribot

clean_nutribot:
	rm -rf $(BUILD_DIR_NUTRIBOT)
.PHONY: clean_nutribot


# ==============================================================================
# NutriBot 端侧模型切换（改 settings/角色 JSON，重启进程生效）
#   make model_8b     - Qwen3-VL-8B（qnn248，默认；识别最强，较慢）
#   make model_3b     - Qwen2.5-VL-3B（qnn236；响应最快，回退用）
#   make model_4b     - Qwen3-VL-4B（qnn240；视觉需 aidlite-qnn240>=2.5.1）
#   make model_status - 显示当前模型
# ==============================================================================

switch_nutribot_model ?= python3 $(PROJECT_DIR)/scripts/switch_nutribot_model.py

model_3b:
	$(switch_nutribot_model) 3b
.PHONY: model_3b

model_4b:
	$(switch_nutribot_model) 4b
.PHONY: model_4b

model_8b:
	$(switch_nutribot_model) 8b
.PHONY: model_8b

model_status:
	$(switch_nutribot_model) status
.PHONY: model_status


# ==============================================================================
# 兼容提示：旧目标已改名（否则 make 会把 build/ 目录当成空目标而静默无操作）
# ==============================================================================

build run clean:
	@echo "Targets renamed: use build_tui / build_sdl (fast/full variants available)" >&2; exit 1
.PHONY: build run clean

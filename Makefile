# ==============================================================================
# Prosophor Makefile
# ==============================================================================
# 编译目标说明：
#   1. build        - Linux/macOS 构建 (Linux: CUDA GPU / macOS: CPU)
#   2. build_win    - Windows 构建 (MSYS2/MinGW, Vulkan GPU, 无 SDL UI)
#   3. build_win_sdl- Windows 构建 (MSYS2/MinGW, Vulkan GPU, 启用 SDL UI)
# ==============================================================================

PROJECT_DIR ?= $(abspath ./)
CMAKE ?= cmake
MAKE ?= make
NUM_JOB ?= 8

PACKAGE_NAME ?= Prosophor
PACKAGE_VERSION ?= 0.8.1
# BUILD_TYPE ?= RelWithDebInfo
BUILD_TYPE ?= Debug


all:
	@echo hello world
.PHONY: all


# ==============================================================================
# Linux/macOS 构建配置
# ==============================================================================

BUILD_DIR ?= $(PROJECT_DIR)/build
INSTALL_DIR ?= $(BUILD_DIR)/install
CMAKE_ARGS ?= \
	-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR) \
	-DPACKAGE_BUILD_DIR=$(BUILD_DIR) \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	-DPACKAGE_VERSION=$(PACKAGE_VERSION) \
	-DGGML_VULKAN=OFF \
	-DPROSOPHOR_BUILD_LLAMA_VULKAN=OFF \
	-DPROSOPHOR_BUILD_LLAMA=ON \
	-DPROSOPHOR_BUILD_LLAMA_CUDA=ON \
	$(CMAKE_EXTRA_ARGS)

build:
	mkdir -p ${BUILD_DIR} && \
	cd ${BUILD_DIR} && \
	${CMAKE} ${CMAKE_ARGS} .. && \
	${MAKE} -j${NUM_JOB}; \
	${MAKE} install
.PHONY: build

run:
	$(INSTALL_DIR)/bin/prosophor
.PHONY: run


# ==============================================================================
# Windows 构建配置 (MSYS2/MinGW)
# ==============================================================================

# Set PATH for MSYS2/MinGW tools and NVIDIA utilities (nvidia-smi)
export PATH := /c/Windows/System32:/e/devtool/msys64/mingw64/bin:$(PATH)

BUILD_DIR_WIN ?= $(PROJECT_DIR)/build_win
INSTALL_DIR_WIN ?= $(BUILD_DIR_WIN)/install
CMAKE_ARGS_WIN ?= \
	-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR_WIN) \
	-DPACKAGE_BUILD_DIR=$(BUILD_DIR_WIN) \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DGGML_VULKAN=ON \
	-DPROSOPHOR_BUILD_LLAMA_VULKAN=ON \
	-DPROSOPHOR_BUILD_LLAMA=ON \
	-DPROSOPHOR_BUILD_LLAMA_CUDA=OFF \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	-DPACKAGE_VERSION=$(PACKAGE_VERSION) \
	$(CMAKE_EXTRA_ARGS)

# Windows 构建 - 无 SDL UI (纯终端模式)
build_win:
	mkdir -p ${BUILD_DIR_WIN} && \
	cd ${BUILD_DIR_WIN} && \
	${CMAKE} ${CMAKE_ARGS_WIN} -DPROSOPHOR_SDL_UI=OFF .. && \
	ninja -j${NUM_JOB}; \
	ninja install
.PHONY: build_win

# Windows 构建 - 启用 SDL UI (图形界面模式)
build_win_sdl:
	mkdir -p ${BUILD_DIR_WIN} && \
	cd ${BUILD_DIR_WIN} && \
	${CMAKE} ${CMAKE_ARGS_WIN} -DPROSOPHOR_SDL_UI=ON .. && \
	ninja -j${NUM_JOB}; \
	ninja install
.PHONY: build_win_sdl

# ==============================================================================
# Windows 构建变体（独立目录，支持并行构建）
# ==============================================================================
# 变体说明：
#   tui_fast    - 纯终端+仅远程API，最小体积（无 llama）
#   tui         - 纯终端+本地模型
#   sdl_fast    - 桌面宠物+仅远程API，快速编译
#   sdl         - 桌面宠物+全功能（当前默认）
# ==============================================================================

BUILD_DIR_TUI_FAST ?= $(PROJECT_DIR)/build_win_tui_fast
INSTALL_DIR_TUI_FAST ?= $(BUILD_DIR_TUI_FAST)/install

BUILD_DIR_TUI_FULL ?= $(PROJECT_DIR)/build_win_tui
INSTALL_DIR_TUI_FULL ?= $(BUILD_DIR_TUI_FULL)/install

BUILD_DIR_SDL_FAST ?= $(PROJECT_DIR)/build_win_sdl_fast
INSTALL_DIR_SDL_FAST ?= $(BUILD_DIR_SDL_FAST)/install

BUILD_DIR_SDL_FULL ?= $(PROJECT_DIR)/build_win_sdl
INSTALL_DIR_SDL_FULL ?= $(BUILD_DIR_SDL_FULL)/install

# build_variant 参数: $(1)=目录 $(2)=Vulkan $(3)=LLAMA $(4)=CUDA $(5)=SDL_UI
define build_variant
	mkdir -p $(1) && \
	cd $(1) && \
	$(CMAKE) $(CMAKE_ARGS_WIN) \
		-DGGML_VULKAN=$(2) \
		-DPROSOPHOR_BUILD_LLAMA_VULKAN=$(2) \
		-DPROSOPHOR_BUILD_LLAMA=$(3) \
		-DPROSOPHOR_BUILD_LLAMA_CUDA=$(4) \
		-DPROSOPHOR_SDL_UI=$(5) .. && \
	ninja -j$(NUM_JOB); \
	ninja install
endef

# tui_fast: 纯终端 + 仅远程API，无本地模型
build_win_tui_fast:
	$(call build_variant,$(BUILD_DIR_TUI_FAST),OFF,OFF,OFF,OFF)
.PHONY: build_win_tui_fast

# tui: 纯终端 + 本地模型 (Vulkan)
build_win_tui_full:
	$(call build_variant,$(BUILD_DIR_TUI_FULL),ON,ON,OFF,OFF)
.PHONY: build_win_tui_full

# sdl_fast: 桌面宠物 + 仅远程API，无本地模型
build_win_sdl_fast:
	$(call build_variant,$(BUILD_DIR_SDL_FAST),OFF,OFF,OFF,ON)
.PHONY: build_win_sdl_fast

# sdl: 桌面宠物 + 全功能 (Vulkan)
build_win_sdl_full:
	$(call build_variant,$(BUILD_DIR_SDL_FULL),ON,ON,OFF,ON)
.PHONY: build_win_sdl_full

# ---- Web 服务端变体(多用户,无 SDL,含本地 llama.cpp;前端 npm 构建) ----
# 服务端同样支持本地 GGUF 推理(llamacpp provider),与桌面端共享模型配置。

BUILD_DIR_WEB ?= $(PROJECT_DIR)/build_win_web
INSTALL_DIR_WEB ?= $(BUILD_DIR_WEB)/install

build_web:
	mkdir -p $(BUILD_DIR_WEB) && \
	cd $(BUILD_DIR_WEB) && \
	$(CMAKE) $(CMAKE_ARGS_WIN) \
		-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR_WEB) \
		-DGGML_VULKAN=ON \
		-DPROSOPHOR_BUILD_LLAMA_VULKAN=ON \
		-DPROSOPHOR_BUILD_LLAMA=ON \
		-DPROSOPHOR_BUILD_LLAMA_CUDA=OFF \
		-DPROSOPHOR_SDL_UI=OFF \
		-DPROSOPHOR_BUILD_WEB_FRONTEND=ON .. && \
	ninja -j$(NUM_JOB); \
	ninja install
.PHONY: build_web

run_web:
	cd $(INSTALL_DIR_WEB)/bin && SSL_CERT_FILE=ca-bundle.crt ./prosophor_web.exe --host 0.0.0.0
.PHONY: run_web

clean_web:
	rm -rf $(BUILD_DIR_WEB)
.PHONY: clean_web

# ---- 运行变体 ----

run_win_tui_fast:
	cd $(INSTALL_DIR_TUI_FAST)/bin && SSL_CERT_FILE=ca-bundle.crt ./prosophor.exe
.PHONY: run_win_tui_fast

run_win_tui_full:
	cd $(INSTALL_DIR_TUI_FULL)/bin && SSL_CERT_FILE=ca-bundle.crt ./prosophor.exe
.PHONY: run_win_tui_full

run_win_sdl_fast:
	cd $(INSTALL_DIR_SDL_FAST)/bin && SSL_CERT_FILE=ca-bundle.crt ./prosophor.exe
.PHONY: run_win_sdl_fast

run_win_sdl_full:
	cd $(INSTALL_DIR_SDL_FULL)/bin && SSL_CERT_FILE=ca-bundle.crt ./prosophor.exe
.PHONY: run_win_sdl_full

# ---- 清理变体 ----

clean_win_tui_fast:
	rm -rf $(BUILD_DIR_TUI_FAST)
.PHONY: clean_win_tui_fast

clean_win_tui_full:
	rm -rf $(BUILD_DIR_TUI_FULL)
.PHONY: clean_win_tui_full

clean_win_sdl_fast:
	rm -rf $(BUILD_DIR_SDL_FAST)
.PHONY: clean_win_sdl_fast

clean_win_sdl_full:
	rm -rf $(BUILD_DIR_SDL_FULL)
.PHONY: clean_win_sdl_full

clean:
	rm -rf ${BUILD_DIR}
.PHONY: clean

run_win:
	cd $(INSTALL_DIR_WIN)/bin && SSL_CERT_FILE=ca-bundle.crt ./prosophor.exe
.PHONY: run_win

clean_win:
	rm -rf ${BUILD_DIR_WIN}
.PHONY: clean_win

# NSIS 安装包路径
NSIS ?= /e/devtool/msys64/mingw64/bin/makensis.exe
PACKAGE_DIR ?= $(PROJECT_DIR)/tool/packaging

package: build_win_sdl
	cp /e/devtool/msys64/mingw64/etc/ssl/certs/ca-bundle.crt $(INSTALL_DIR_WIN)/bin/
	cd $(PACKAGE_DIR) && "$(NSIS)" -DPRODUCT_VERSION=$(PACKAGE_VERSION) installer.nsi
	mv $(PACKAGE_DIR)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)-win64-setup.exe $(BUILD_DIR_WIN)/
	@echo "=== Installer created: $(BUILD_DIR_WIN)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)-win64-setup.exe ==="
.PHONY: package

# ---- 构建变体打包 ----

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

package_sdl_full: build_win_sdl_full
	$(call do_package,$(INSTALL_DIR_SDL_FULL),)
	@echo "Variant: sdl_full (full feature)"
.PHONY: package_sdl_full

package_sdl_fast: build_win_sdl_fast
	$(call do_package,$(INSTALL_DIR_SDL_FAST),)
	@echo "Variant: sdl_fast (no local LLM)"
.PHONY: package_sdl_fast

# ---- 部署（构建+打包+GitHub Release） ----

CHANGELOG_NOTES ?= $(BUILD_DIR_WIN)/release_notes.md

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

deploy: package
	$(call do_deploy,$(BUILD_DIR_WIN),full)
.PHONY: deploy

deploy_sdl_full: package_sdl_full
	$(call do_deploy,$(INSTALL_DIR_SDL_FULL),sdl_full)
.PHONY: deploy_sdl_full

deploy_sdl_fast: package_sdl_fast
	$(call do_deploy,$(INSTALL_DIR_SDL_FAST),sdl_fast)
.PHONY: deploy_sdl_fast

GITEE_API ?= https://gitee.com/api/v5
GITEE_OWNER ?= swair
GITEE_REPO ?= prosophor
GITEE_TOKEN ?= $(GITEE_ACCESS_TOKEN)
GITEE_TARGET_COMMITISH ?= main
GITEE_MAX_ASSET_BYTES ?= 104857600
GITEE_RELEASE_BODY ?= Release v$(PACKAGE_VERSION)

# 构建+NSIS 安装包+发布到 Gitee 发行版（需要先 git tag，需设置 GITEE_TOKEN）
deploy_gitee: package upload_gitee
.PHONY: deploy_gitee

deploy_gitee_sdl_full: package_sdl_full upload_gitee_sdl_full
.PHONY: deploy_gitee_sdl_full

deploy_gitee_sdl_fast: package_sdl_fast upload_gitee_sdl_fast
.PHONY: deploy_gitee_sdl_fast

# 仅发布已有安装包到 Gitee，避免 deploy_gitee 失败后重新打包
upload_gitee:
	$(call do_upload_gitee,$(BUILD_DIR_WIN))
.PHONY: upload_gitee

upload_gitee_sdl_full:
	$(call do_upload_gitee,$(INSTALL_DIR_SDL_FULL))
.PHONY: upload_gitee_sdl_full

upload_gitee_sdl_fast:
	$(call do_upload_gitee,$(INSTALL_DIR_SDL_FAST))

define do_upload_gitee
	@if [ -z "$(GITEE_TOKEN)" ]; then \
	  echo "GITEE_TOKEN is required. Example: make upload_gitee GITEE_TOKEN=xxxxx"; \
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
deploy_all: deploy deploy_gitee
.PHONY: deploy_all

deploy_all_sdl_full: deploy_sdl_full deploy_gitee_sdl_full
.PHONY: deploy_all_sdl_full

deploy_all_sdl_fast: deploy_sdl_fast deploy_gitee_sdl_fast
.PHONY: deploy_all_sdl_fast

# 运行所有单元测试 (执行 bin/tests 目录下所有测试程序)
# NOTE: 动态库 (libwhisper.dll/ggml*.dll) 位于 bin/，需加入 PATH
.PHONY: tests
tests:
	@echo "Running all tests in $(INSTALL_DIR_WIN)/bin/unitests..."
	@export PATH="$(INSTALL_DIR_WIN)/bin:$$PATH"; \
	for test in $(INSTALL_DIR_WIN)/bin/unitests/*.exe; do \
	  echo "Running $$(basename $$test)..."; \
	  $$test || exit 1; \
	done

# ==============================================================================
# nutribot 形态（仿 tui/sdl：同一主树，编译开关切换形态，无独立模块构建）
#   LLM(aidgen) 已并入 prosophor_core/providers/llm；
#   入口统一为 main_src/main.cc（PROSOPHOR_NUTRIBOT 宏分支）。
#   QNN 平台形态默认：无 llamacpp、无 SDL。
# ==============================================================================

BUILD_DIR_NUTRIBOT ?= $(PROJECT_DIR)/build_nutribot
INSTALL_DIR_NUTRIBOT ?= $(BUILD_DIR_NUTRIBOT)/install
CMAKE_ARGS_NUTRIBOT ?= \
	-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR_NUTRIBOT) \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DPACKAGE_VERSION=$(PACKAGE_VERSION) \
	-DPROSOPHOR_NUTRIBOT=ON \
	-DPROSOPHOR_BUILD_LLAMA=OFF \
	$(CMAKE_EXTRA_ARGS)

build_nutribot:
	mkdir -p $(BUILD_DIR_NUTRIBOT) && \
	cd $(BUILD_DIR_NUTRIBOT) && \
	$(CMAKE) $(CMAKE_ARGS_NUTRIBOT) .. && \
	$(MAKE) -j$(NUM_JOB)
.PHONY: build_nutribot

run_nutribot:
	$(BUILD_DIR_NUTRIBOT)/main_src/prosophor
.PHONY: run_nutribot

# nutribot 单测（独立于主树构建，同源收集；33 用例）
test_nutribot:
	mkdir -p $(BUILD_DIR_NUTRIBOT)/tests && \
	cd $(BUILD_DIR_NUTRIBOT)/tests && \
	$(CMAKE) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(PROJECT_DIR)/tests/nutribot && \
	$(MAKE) -j$(NUM_JOB) && ./nr_tests
.PHONY: test_nutribot

clean_nutribot:
	rm -rf $(BUILD_DIR_NUTRIBOT)
.PHONY: clean_nutribot

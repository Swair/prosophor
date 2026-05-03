# ==============================================================================
# Prosophor Makefile
# ==============================================================================
# 编译目标说明：
#   1. build        - Linux/macOS 构建 (使用 system 默认编译器)
#   2. build_win    - Windows 构建 (MSYS2/MinGW, 无 SDL UI)
#   3. build_win_sdl- Windows 构建 (MSYS2/MinGW, 启用 SDL UI)
# ==============================================================================

PROJECT_DIR ?= $(abspath ./)
CMAKE ?= cmake
MAKE ?= make
NUM_JOB ?= 8

# Set PATH for MSYS2/MinGW tools (always needed on this system)
export PATH := /e/devtool/msys64/mingw64/bin:$(PATH)

PACKAGE_NAME ?= Demo
PACKAGE_VERSION ?= 0.2.0
BUILD_TYPE ?= RelWithDebInfo


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
	-DCMAKE_PROJECT_VERSION=$(PACKAGE_VERSION) \
	$(CMAKE_EXTRA_ARGS)

build:
	mkdir -p ${BUILD_DIR} && \
	cd ${BUILD_DIR} && \
	${CMAKE} ${CMAKE_ARGS} .. && \
	${MAKE} -j${NUM_JOB}; \
	${MAKE} install
.PHONY: build

run:
	$(INSTALL_DIR)/bin/prosophor;
.PHONY: run

tests:
	@echo "Running all tests in $(BUILD_DIR)/unitests..."
	@for test in $(INSTALL_DIR)/bin/unitests/*_test; do \
		if [ -x "$$test" ] && [ ! -d "$$test" ]; then \
			$$test --gtest_list_tests >/dev/null 2>&1 || continue; \
			echo "=== $$(basename $$test) ==="; \
			$$test || exit 1; \
		fi \
	done
.PHONY: tests

clean:
	rm -rf ${BUILD_DIR}
.PHONY: clean


# ==============================================================================
# Windows 构建配置 (MSYS2/MinGW)
# ==============================================================================

BUILD_DIR_WIN ?= $(PROJECT_DIR)/build_win
INSTALL_DIR_WIN ?= $(BUILD_DIR_WIN)/install
CMAKE_ARGS_WIN ?= \
	-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR_WIN) \
	-DPACKAGE_BUILD_DIR=$(BUILD_DIR_WIN) \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	-DCMAKE_PROJECT_VERSION=$(PACKAGE_VERSION) \
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

run_win:
	cd $(INSTALL_DIR_WIN)/bin && ./prosophor.exe
.PHONY: run_win

clean_win:
	rm -rf ${BUILD_DIR_WIN}
.PHONY: clean_win

# 运行所有单元测试 (执行 bin/tests 目录下所有测试程序)
.PHONY: run_win_tests
run_win_tests:
	@echo "Running all tests in $(INSTALL_DIR_WIN)/bin/tests..."
	@for test in $(INSTALL_DIR_WIN)/bin/tests/*.exe; do echo "Running $$(basename $$test)..."; $$test || exit 1; done


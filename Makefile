PROJECT_DIR ?= $(abspath ./)
CMAKE ?= cmake
MAKE ?= make
NUM_JOB ?= 8


BUILD_DIR ?= $(PROJECT_DIR)/build
INSTALL_DIR ?= $(BUILD_DIR)/install
DEPLOY_DIR ?= $(BUILD_DIR)/deploy
TEST_DIR ?= $(PROJECT_DIR)/test
CMAKE_ARGS ?= \
	-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR) \
	-DPACKAGE_BUILD_DIR=$(BUILD_DIR) \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	$(CMAKE_EXTRA_ARGS)



all:
	@echo hello world
.PHONY: all


build:
	cd ${BUILD_DIR} && \
	${CMAKE} ${CMAKE_ARGS} ${PROJECT_DIR} && \
	${MAKE} -j ${NUM_JOB} && ${MAKE} install
.PHONY: build


run:
	${INSTALL_DIR}/bin/aicode
.PHONY: run


clean:
	rm -rf ${BUILD_DIR}/*
.PHONY: clean

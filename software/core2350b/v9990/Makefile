# NEO9990 V9990 firmware for the Waveshare Core2350B.
#
#   make            build, then copy the UF2 to <repo>/firmware/neo9990-v9990-<version>.uf2
#   make clean      remove the build directory (the firmware/ folder is kept)
#   make VERSION=v1.01
#
# Needs the Raspberry Pi Pico SDK, an Arm GNU toolchain, CMake and (optionally)
# Ninja. They are looked up in ~/.pico-sdk (the layout the VS Code Pico extension
# installs); override PICO_SDK_PATH, PICO_TOOLCHAIN_PATH, CMAKE, NINJA or
# PICOTOOL_DIR to use other copies.

CHIP    := v9990
VERSION ?= v1.00

HOME_DIR       := $(subst \,/,$(or $(USERPROFILE),$(HOME)))
PICO_HOME      ?= $(HOME_DIR)/.pico-sdk
PICO_SDK_VERSION ?= 2.3.1
PICO_SDK_PATH  ?= $(PICO_HOME)/sdk/$(PICO_SDK_VERSION)
PICO_TOOLCHAIN_PATH ?= $(lastword $(sort $(wildcard $(PICO_HOME)/toolchain/*)))
PICOTOOL_DIR   ?= $(PICO_HOME)/picotool/$(PICO_SDK_VERSION)/picotool
CMAKE          ?= $(or $(lastword $(sort $(wildcard $(PICO_HOME)/cmake/*/bin/cmake.exe $(PICO_HOME)/cmake/*/bin/cmake))),cmake)
NINJA          ?= $(lastword $(sort $(wildcard $(PICO_HOME)/ninja/*/ninja.exe $(PICO_HOME)/ninja/*/ninja)))

BUILD_DIR := build
OUT_DIR   := ../../../firmware
UF2       := neo9990-$(CHIP)-$(VERSION).uf2

CMAKE_ARGS := -S . -B $(BUILD_DIR) \
	-DPICO_SDK_PATH="$(PICO_SDK_PATH)" \
	-DNEO_FW_VERSION="$(VERSION)"
ifneq ($(PICO_TOOLCHAIN_PATH),)
CMAKE_ARGS += -DPICO_TOOLCHAIN_PATH="$(PICO_TOOLCHAIN_PATH)"
endif
ifneq ($(wildcard $(PICOTOOL_DIR)),)
CMAKE_ARGS += -Dpicotool_DIR="$(PICOTOOL_DIR)"
endif
ifneq ($(NINJA),)
CMAKE_ARGS += -G Ninja -DCMAKE_MAKE_PROGRAM="$(NINJA)"
endif

.PHONY: all configure build clean

all: build
	"$(CMAKE)" -E make_directory $(OUT_DIR)
	"$(CMAKE)" -E copy $(BUILD_DIR)/neo9990_$(CHIP).uf2 $(OUT_DIR)/$(UF2)
	@echo Built $(OUT_DIR)/$(UF2)

configure:
	"$(CMAKE)" $(CMAKE_ARGS)

build: configure
	"$(CMAKE)" --build $(BUILD_DIR)

clean:
	"$(CMAKE)" -E remove_directory $(BUILD_DIR)

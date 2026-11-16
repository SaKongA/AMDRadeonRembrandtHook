NAME := AMDRadeonRembrandtHook
VERSION := 0.1.1
BUILD_DIR ?= build
SDK_DIR ?= upstream/MacKernelSDK
LILU_DIR ?= upstream/Lilu/Lilu
CLANG ?= clang
CLANGXX ?= clang++
LD := ld

TARGET := x86_64-apple-macos10.15
KERNEL_FLAGS := -target $(TARGET) -mkernel -nostdinc -fno-builtin -fno-common -mno-red-zone -O2 \
    -fno-asynchronous-unwind-tables -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
    -I$(SDK_DIR)/Headers -I$(LILU_DIR) -Iinclude
CPP_FLAGS := $(KERNEL_FLAGS) -std=c++17 -fapple-kext -nostdlib -fno-rtti -fno-exceptions \
    -fno-use-cxa-atexit -mllvm -disable-atexit-based-global-dtor-lowering \
    -DPRODUCT_NAME=$(NAME) -DMODULE_VERSION=$(VERSION) -Wno-deprecated-declarations

CPP_SOURCES := $(shell find src -name '*.cpp' -type f)
CPP_OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/obj/%.o,$(CPP_SOURCES))
KMOD_OBJECT := $(BUILD_DIR)/obj/KmodInfo.o
FIRMWARE_OBJECT := $(BUILD_DIR)/obj/Hardware/Rembrandt/Firmware/FirmwareImages.o
LILU_OBJECT := $(BUILD_DIR)/obj/LiluPluginStart.o
OBJECTS := $(CPP_OBJECTS) $(KMOD_OBJECT) $(FIRMWARE_OBJECT) $(LILU_OBJECT)
KEXT_DIR := $(BUILD_DIR)/$(NAME).kext/Contents
KEXT_EXECUTABLE := $(KEXT_DIR)/MacOS/$(NAME)

.PHONY: all clean check-firmware

all: $(KEXT_EXECUTABLE)

$(BUILD_DIR)/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CLANGXX) $(CPP_FLAGS) -MMD -MP -c $< -o $@

$(KMOD_OBJECT): src/KmodInfo.c
	@mkdir -p $(dir $@)
	$(CLANG) $(KERNEL_FLAGS) -MMD -MP -c $< -o $@

$(LILU_OBJECT): $(LILU_DIR)/Library/plugin_start.cpp
	@mkdir -p $(dir $@)
	$(CLANGXX) $(CPP_FLAGS) -MMD -MP -c $< -o $@

check-firmware:
	@printf '%s\n' \
	  '90921c2a86e44f305baa8c52fb4873500198a52f2bc4718bf6be352f36a6ea3d  src/Hardware/Rembrandt/Firmware/yellow_carp_me.bin' \
	  '15eb2633afb17cec342cde936a77bb0b707bcd67344e67ec91d394e55e72c88d  src/Hardware/Rembrandt/Firmware/yellow_carp_pfp.bin' \
	  'fd1a7950353fdb519de6ba265cb93c78c7040e22aba3ad3cc6264d428ad930ba  src/Hardware/Rembrandt/Firmware/yellow_carp_ce.bin' \
	  'f2effb1097c2665c791167887d30fffd4227236c1814cfce0f95695f9c90f7df  src/Hardware/Rembrandt/Firmware/yellow_carp_mec.bin' \
	  '201096bed913db50e90c0b52e123c5c20bd0989e186335c6120b802973118297  src/Hardware/Rembrandt/Firmware/yellow_carp_rlc.bin' \
	  '52fb816b574c95dfcb878a6c6a7fd1733e1fbcdbc86367316a1c7abb42225365  src/Hardware/Rembrandt/Firmware/yellow_carp_sdma.bin' \
	  '9fc8da2f6106bf657222ca4bcca687e66e657e5710278c694d9d0e862221758d  src/Hardware/Rembrandt/Firmware/yellow_carp_toc.bin' \
	  'fe24782dd771ccb9d59d90e4c1ee2c8759b9edbe4aa6c277936bae012af51fef  src/Hardware/Rembrandt/Firmware/yellow_carp_dmcub.bin' | shasum -a 256 -c

$(FIRMWARE_OBJECT): src/Hardware/Rembrandt/Firmware/FirmwareImages.s $(wildcard src/Hardware/Rembrandt/Firmware/yellow_carp_*.bin) check-firmware
	@mkdir -p $(dir $@)
	$(CLANG) -target $(TARGET) -c $< -o $@

$(KEXT_EXECUTABLE): $(OBJECTS) Info.plist
	@mkdir -p $(dir $@)
	$(LD) -arch x86_64 -kext -platform_version macos 10.15 10.15 -no_data_const -o $@ $(OBJECTS) $(SDK_DIR)/Library/x86_64/libkmod.a
	cp Info.plist $(KEXT_DIR)/Info.plist

clean:
	rm -rf $(BUILD_DIR)

-include $(CPP_OBJECTS:.o=.d) $(KMOD_OBJECT:.o=.d) $(LILU_OBJECT:.o=.d)

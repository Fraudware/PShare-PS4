# Run from the project root: make -f ps4/Makefile
# SPDX-License-Identifier: GPL-3.0-or-later
.DEFAULT_GOAL := pkg
SHELL := /bin/bash
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT := 1
OO_PS4_TOOLCHAIN ?= /opt/OpenOrbis/PS4Toolchain
CC := clang
CXX := clang++
LD := ld.lld
TITLE_ID := PSHR00004
CONTENT_ID := IV0000-$(TITLE_ID)_00-PSHAREPS40000001
BUILD := build/ps4
STAGE := $(BUILD)/pkg
CORE := $(filter-out src/pshare/core/upload_curl.cpp,$(wildcard src/pshare/core/*.cpp))
CPP := ps4/main.cpp ps4/ui_kit.cpp ps4/thumbs.cpp ps4/upload_http.cpp ps4/ps4_stat.cpp daemon/main.cpp $(CORE)
C := src/third_party/stb_image/stb_image.c src/third_party/qrcodegen/qrcodegen.c $(wildcard third_party/libjbc/*.c)
OBJECTS := $(addprefix $(BUILD)/,$(addsuffix .o,$(CPP) $(C)))
FLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 \
 -isysroot $(OO_PS4_TOOLCHAIN) -isystem $(OO_PS4_TOOLCHAIN)/include \
 -I$(OO_PS4_TOOLCHAIN)/include/SDL2 -Isrc -Ips4 -Ithird_party/libjbc \
 -D_GNU_SOURCE=1 -D_DEFAULT_SOURCE=1 -D_BSD_SOURCE=1 -DPSHARE_PS4=1 -DPSHARE_EMBEDDED=1 -DPSHARE_BUILD=ps4r100
CXXFLAGS := $(FLAGS) -std=c++20 -isystem $(OO_PS4_TOOLCHAIN)/include/c++/v1 -include ps4/ps4_stat.h
LIBS := -lc++ -lc++abi -lc -lkernel -lSDL2 -lSceUserService -lSceVideoOut -lSceAudioOut \
 -lScePad -lSceSysmodule -lSceFreeType -lSceNet -lSceNetCtl -lSceHttp -lSceSsl -lSceSystemService
TOOLS := $(OO_PS4_TOOLCHAIN)/bin/linux
.PHONY: pkg self check stage clean
check:
	@test -f "$(OO_PS4_TOOLCHAIN)/lib/libSDL2.a" || (echo 'Set OO_PS4_TOOLCHAIN to the OpenOrbis 0.5.4 toolchain'; exit 1)
	@command -v $(CXX) >/dev/null
	@command -v $(LD) >/dev/null
$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(FLAGS) -std=gnu11 -MMD -MP -c $< -o $@
$(BUILD)/pshare.elf: $(OBJECTS)
	$(LD) -m elf_x86_64 -pie --script $(OO_PS4_TOOLCHAIN)/link.x --eh-frame-hdr \
	 -L$(OO_PS4_TOOLCHAIN)/lib $(OO_PS4_TOOLCHAIN)/lib/crt1.o $(OBJECTS) \
	 --start-group $(LIBS) --end-group -o $@
self: check $(BUILD)/pshare.elf
	@mkdir -p $(STAGE)
	$(TOOLS)/create-fself -in=$(BUILD)/pshare.elf -out=$(BUILD)/pshare.oelf \
	 --eboot $(STAGE)/eboot.bin --paid 0x3800000000000011
stage: self
	@cp -a ps4/sce_sys ps4/sce_module $(STAGE)/
	@cp -a assets $(STAGE)/
	$(TOOLS)/PkgTool.Core sfo_new $(STAGE)/sce_sys/param.sfo
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo APP_VER --type Utf8 --maxsize 8 --value '01.00'
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value gd
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value 'PShare'
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $(STAGE)/sce_sys/param.sfo VERSION --type Utf8 --maxsize 8 --value '01.00'
pkg: stage
	@python3 ps4/make_gp4.py $(STAGE) $(CONTENT_ID)
	@mkdir -p dist
	@cd $(STAGE) && "$(abspath $(TOOLS))/PkgTool.Core" pkg_build pshare.gp4 "$(abspath dist)"
clean:
	rm -rf $(BUILD)
-include $(OBJECTS:.o=.d)

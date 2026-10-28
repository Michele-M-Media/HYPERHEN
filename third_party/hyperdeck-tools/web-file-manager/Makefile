ifneq ($(filter-out linux linux-deps clean,$(MAKECMDGOALS)),)
  ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
  else
    $(error PS5_PAYLOAD_SDK is undefined)
  endif
endif
ifeq ($(MAKECMDGOALS),)
  ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
  else
    $(error PS5_PAYLOAD_SDK is undefined)
  endif
endif

VERSION_TAG := v1.11
TITLE_ID    := FMGR88888
PYTHON      ?= python3
CMAKE       ?= cmake
STRIP       ?= $(PS5_PAYLOAD_SDK)/bin/prospero-strip
PKG_CONFIG  ?= $(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config
HOST_CC     ?= cc
HOST_STRIP  ?= strip
HOST_PKG_CONFIG ?= pkg-config

BIN        := web-file-mgr.elf
LINUX_BIN  := web-file-mgr-linux
PS5_DEBUG  := $(if $(filter debug,$(MAKECMDGOALS)),1,0)
PS5_BUILD_DIR := .build-ps5/$(if $(filter debug,$(MAKECMDGOALS)),debug,release)
PS5_BUILD_BIN := $(PS5_BUILD_DIR)/$(BIN)
COMMON_SRCS := src/main.c src/websrv.c src/filemgr.c src/file_response.c src/task.c src/upload.c src/download.c src/text.c src/list.c src/space.c src/fs_util.c src/json_util.c src/path_util.c src/asset.c src/mime.c src/notify.c src/pkg_installer.c src/pkg_info.c src/archive_extract.c src/archive_helper.c src/smb.c src/vfs.c src/transfer.c
HYPERHEN_NO_MEDIA_TILE ?= 0
ifeq ($(HYPERHEN_NO_MEDIA_TILE),1)
PS5_SRCS    := $(COMMON_SRCS) hyperhen/no_media_tile.c
else
PS5_SRCS    := $(COMMON_SRCS) src/app_installer.c
endif
HEADERS     := $(wildcard src/*.h)
SMB2_HEAD   := $(shell if test -f vendor/libsmb2/.git; then git -C vendor/libsmb2 rev-parse --git-path HEAD; fi)
SMB2_INPUTS := $(SMB2_HEAD) $(wildcard vendor/libsmb2/CMakeLists.txt vendor/libsmb2/cmake/* vendor/libsmb2/lib/* vendor/libsmb2/include/*.h vendor/libsmb2/include/smb2/*)
SMB2_CMAKE_FLAGS := -DBUILD_SHARED_LIBS=OFF -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF -DENABLE_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
SMB2_LINUX_LIB := .build-smb2/linux/build/lib/libsmb2.a
SMB2_PS5_LIB := .build-smb2/ps5/build/lib/libsmb2.a
LINUX_SRCS  := $(COMMON_SRCS)
BASE_ASSETS := $(filter-out %.dds,$(wildcard assets/*))
ifneq ($(filter linux,$(MAKECMDGOALS)),)
ASSETS      := $(BASE_ASSETS)
else
ASSETS      := $(filter-out assets/icon0.png,$(BASE_ASSETS))
endif
GEN_SRCS    := $(patsubst assets/%,gen/%, $(ASSETS:=.c))

CFLAGS := -Oz -flto=thin -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Werror -ffunction-sections -fdata-sections -Isrc -DVERSION_TAG=\"$(VERSION_TAG)\" -DTITLE_ID=\"$(TITLE_ID)\"
CFLAGS += `$(PKG_CONFIG) libmicrohttpd --cflags`
CFLAGS += -DWFM_DEBUG=$(PS5_DEBUG)
CFLAGS += -Ivendor/libsmb2/include
LDFLAGS := -Wl,--gc-sections
LDADD  := `$(PKG_CONFIG) libmicrohttpd --libs`
LDADD  += -lSceIpmi -lSceAppInstUtil -lSceUserService
LINUX_CFLAGS := -O2 -flto=auto -Wall -Werror -Isrc -DVERSION_TAG=\"$(VERSION_TAG)\" -DTITLE_ID=\"$(TITLE_ID)\"
LINUX_CFLAGS += `$(HOST_PKG_CONFIG) libmicrohttpd --cflags`
LINUX_CFLAGS += -DWFM_DEBUG=1
LINUX_CFLAGS += -Ivendor/libsmb2/include
LINUX_LDADD := `$(HOST_PKG_CONFIG) libmicrohttpd --libs` -pthread

.PHONY: all debug linux deps linux-deps clean force-ps5-copy
.SECONDARY: .build-smb2/linux/source.stamp .build-smb2/ps5/source.stamp

all: deps $(BIN)

debug: deps $(BIN)

linux: linux-deps $(LINUX_BIN)

deps:
	@$(PKG_CONFIG) --exists libmicrohttpd || ./install-libmicrohttpd.sh

linux-deps:
	@$(HOST_PKG_CONFIG) --exists libmicrohttpd || \
	  (echo "libmicrohttpd development package is required for make linux" >&2; exit 1)

gen:
	mkdir gen

clean:
	rm -rf $(BIN) $(LINUX_BIN) gen
	rm -rf .build-ps5/release .build-ps5/debug
	rm -rf .build-smb2

gen/%.c: assets/% gen-asset-module.py | gen
	$(PYTHON) gen-asset-module.py --path $* $< > $@

$(BIN): $(PS5_BUILD_BIN) force-ps5-copy
	cp $< $@

$(PS5_BUILD_BIN): $(PS5_SRCS) $(HEADERS) $(GEN_SRCS) assets/icon0.png assets/param.json Makefile $(SMB2_PS5_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter %.c,$^) $(SMB2_PS5_LIB) $(LDADD)
	$(STRIP) $@

$(LINUX_BIN): $(LINUX_SRCS) $(HEADERS) $(GEN_SRCS) Makefile $(SMB2_LINUX_LIB)
	$(HOST_CC) $(LINUX_CFLAGS) -o $@ $(filter %.c,$^) $(SMB2_LINUX_LIB) $(LINUX_LDADD)
	$(HOST_STRIP) $@

.build-smb2/%/source.stamp: Makefile patches/libsmb2-rename-replace.patch .gitmodules $(SMB2_INPUTS)
	@test -f vendor/libsmb2/include/smb2/libsmb2.h || \
	  (echo "Initialize libsmb2 with: git submodule update --init --recursive" >&2; exit 1)
	rm -rf .build-smb2/$*/source
	mkdir -p .build-smb2/$*/source
	cp -R vendor/libsmb2/. .build-smb2/$*/source/
	rm -f .build-smb2/$*/source/.git
	git apply --directory=.build-smb2/$*/source patches/libsmb2-rename-replace.patch
	touch $@

$(SMB2_LINUX_LIB): .build-smb2/linux/source.stamp
	$(CMAKE) -S .build-smb2/linux/source -B .build-smb2/linux/build $(SMB2_CMAKE_FLAGS) -DCMAKE_C_COMPILER="$(HOST_CC)"
	$(CMAKE) --build .build-smb2/linux/build --target smb2 --parallel 4

$(SMB2_PS5_LIB): .build-smb2/ps5/source.stamp
	$(CMAKE) -S .build-smb2/ps5/source -B .build-smb2/ps5/build $(SMB2_CMAKE_FLAGS) \
	  -DCMAKE_SYSTEM_NAME=FreeBSD -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	  -DCMAKE_C_COMPILER="$(PS5_PAYLOAD_SDK)/bin/prospero-clang" \
	  -DCMAKE_AR="$(PS5_PAYLOAD_SDK)/bin/prospero-ar" \
	  -DCMAKE_RANLIB="$(PS5_PAYLOAD_SDK)/bin/prospero-ranlib" \
	  '-DCMAKE_C_FLAGS=-flto=thin -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables' \
	  '-DCMAKE_C_FLAGS_RELEASE=-Oz -DNDEBUG'
	$(CMAKE) --build .build-smb2/ps5/build --target smb2 --parallel 4

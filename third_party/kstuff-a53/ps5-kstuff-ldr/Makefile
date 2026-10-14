#   Copyright (C) 2025 John Törnblom
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING. If not see
# <http://www.gnu.org/licenses/>.

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

LOADER_OPT ?= -O2
KSTUFF_OBS ?= 0
HOMEBREW_ROOT := $(PS5_PAYLOAD_SDK)/target/user/homebrew
HYPERHEN_INCLUDE_DIR ?= ../../../source/include
HYPERHEN_HEADERS := \
  $(HYPERHEN_INCLUDE_DIR)/onion/firmware_capabilities.h \
  $(HYPERHEN_INCLUDE_DIR)/onion/ready.h \
  $(HYPERHEN_INCLUDE_DIR)/onion/system_tmp.h
CFLAGS += -Wall -Werror
CPPFLAGS += -Iinclude -I$(HOMEBREW_ROOT)/include -I$(HYPERHEN_INCLUDE_DIR)

LDFLAGS += -L$(HOMEBREW_ROOT)/lib \
           -lz \
           -lSceSystemService \
           -lSceUserService \
           -lSceAppInstUtil \
           -lSceFsInternalForVsh \
           -lsqlite3 \
           -lSceSysCore \
           -lkernel_sys \
		   -Wl,-s

# Automatically grab all .c files in src/
SRC := $(shell find src -name '*.c')

all: kstuff.elf

.FORCE:

../ps5-kstuff/payload.bin: .FORCE
	$(MAKE) -C ../ps5-kstuff/ KSTUFF_OBS="$(KSTUFF_OBS)" \
		HYPERHEN_INCLUDE_DIR="$(HYPERHEN_INCLUDE_DIR)"

kstuff.elf: ../ps5-kstuff/payload.bin $(SRC) $(HYPERHEN_HEADERS) include/shellcore_dynlib_abi.h ../lib/r0gdb-bootstrap.h ../lib/shellcore-imports.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOADER_OPT) $(SRC) -o $@ $(LDFLAGS)

clean:
	rm -f *.o *.elf

test: kstuff.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

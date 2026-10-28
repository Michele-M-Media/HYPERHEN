PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk

PS5_CC := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
PS5_INCDIR := $(PS5_PAYLOAD_SDK)/target/include
PS5_LIBDIR := $(PS5_PAYLOAD_SDK)/target/lib

CFLAGS := -O2 -Wall -D_BSD_SOURCE -std=gnu11 -Isrc -I$(PS5_INCDIR)
SQLITE_FLAGS := -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_WAL -w
LDFLAGS := -L$(PS5_LIBDIR)
LIBS := -lkernel_sys -lkernel -lSceSystemService -lSceUserService -lSceFsInternalForVsh -lSceRegMgr

all: garlic-savemgr.elf

HOST_CC ?= cc
# SDK v0.44 (or a validated CRT that includes 11.xx/12.xx/13.xx) required.
# The full firmware table resides in SDK crt1.o; app guard cannot add CRT cases.
check-offsets:
	@mkdir -p .host-tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc -o .host-tests/test_fw_offsets tests/test_fw_offsets_guard.c
	@.host-tests/test_fw_offsets
	@rm -rf .host-tests

src/ui.h: src/ui.html
	xxd -i $< > $@

src/sqlite3.o: src/sqlite3.c
	$(PS5_CC) -O2 -D_BSD_SOURCE -std=gnu11 -I$(PS5_INCDIR) $(SQLITE_FLAGS) -c -o $@ $<

garlic-savemgr.elf: src/main.c src/fw_offsets_guard.h src/ui.h src/sqlite3.o
	@test -x "$(PS5_CC)" || (echo "Missing PS5 compiler $(PS5_CC), install SDK v0.44"; exit 1)
	@test -f "$(PS5_LIBDIR)/crt1.o" || (echo "Missing SDK crt1.o. Install SDK v0.44."; exit 1)
	$(PS5_CC) $(CFLAGS) $(LDFLAGS) -o $@ src/main.c src/sqlite3.o $(LIBS)

clean:
	rm -f garlic-savemgr.elf src/ui.h src/sqlite3.o
	rm -rf .host-tests

.PHONY: all clean check-offsets

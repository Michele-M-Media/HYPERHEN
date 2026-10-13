PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := ps5-hen.elf

HEN_BIN := hen/hen.bin
CFLAGS := -std=c++2b -Wall -Werror -g -DHEN_BIN_PATH="\"$(HEN_BIN)\""

all: $(HEN_BIN) $(ELF)

SRCS := main.cpp hv_defeat.cpp gpu_dma.cpp hen.S

$(HEN_BIN):
	$(MAKE) -C hen

$(ELF): $(SRCS) $(HEN_BIN)
	$(CXX) $(CFLAGS) -o $@ $(SRCS)
	$(STRIP) $@

clean:
	$(MAKE) -C hen clean
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

debug: $(ELF)
	gdb-multiarch \
	-ex "set architecture i386:x86-64" \
	-ex "target extended-remote $(PS5_HOST):2159" \
	-ex "file $(ELF)" \
	-ex "remote put $(ELF) /data/$(ELF)" \
	-ex "set remote exec-file /data/$(ELF)" \
	-ex "start"

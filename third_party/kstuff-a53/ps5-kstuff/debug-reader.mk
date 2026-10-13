ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
$(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := debug-reader.elf
BIN := debug-reader.bin

CFLAGS += -Wall -Wextra -g

all: $(BIN)

$(ELF): debug-reader.c uelf/shared_area.h
	$(CC) $(CFLAGS) -o $@ debug-reader.c

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(ELF) $(BIN)

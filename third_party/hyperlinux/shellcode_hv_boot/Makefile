CC := gcc

# Physical addresses staged by the userland (keep in sync with include/config.h:
# cave == cave_hv_paging, and cave_hv_code = cave + 0x3000).
CAVE_HV_PAGING := 0x100000000
CAVE_HV_CODE := 0x100003000

CFLAGS := -DCAVE_HV_PAGING=$(CAVE_HV_PAGING) -DCAVE_HV_CODE=$(CAVE_HV_CODE)

TARGET := hv_boot.elf
BIN    := hv_boot.bin
DUMP   := hv_boot.h

all: $(DUMP)

$(TARGET): entry.o
	ld -nostdlib -no-pie -T link.ld entry.o -o $(TARGET)

entry.o: entry.S
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(TARGET)
	objcopy -O binary $(TARGET) $(BIN)

$(DUMP): $(BIN) ../scripts/bin2c.py
	python3 ../scripts/bin2c.py $(BIN) > $(DUMP)

clean:
	rm -f entry.o $(TARGET) $(BIN) $(DUMP)

CC := gcc

CFLAGS := -O2 -ffreestanding -nostdlib -fno-pic -fno-plt \
  -fno-stack-protector -fno-stack-clash-protection \
  -fomit-frame-pointer -fno-asynchronous-unwind-tables \
  -fcf-protection=none -fno-jump-tables -mgeneral-regs-only \
  -mno-red-zone -Wall -Wextra

SRC := break_hv.c entry.S
OBJ := break_hv.o entry.o

TARGET := break_hv.elf
BIN    := break_hv.bin
DUMP   := break_hv.h

all: $(DUMP)

$(TARGET): $(OBJ)
	ld -nostdlib -no-pie -T link.ld $(OBJ) -o $(TARGET)

break_hv.o: break_hv.c
	$(CC) $(CFLAGS) -c $< -o $@

entry.o: entry.S
	$(CC) -c $< -o $@

$(BIN): $(TARGET)
	objcopy -O binary $(TARGET) $(BIN)

$(DUMP): $(BIN) ../scripts/bin2c.py
	python3 ../scripts/bin2c.py $(BIN) > $(DUMP)

clean:
	rm -f $(OBJ) $(TARGET) $(BIN) $(DUMP)

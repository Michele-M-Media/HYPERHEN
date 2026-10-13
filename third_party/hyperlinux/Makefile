.PHONY: all clean

ifndef PS5_PAYLOAD_SDK
    PS5_PAYLOAD_SDK = /opt/ps5-payload-sdk/
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

BIN := bin/pLinux5-NG.elf
SRC := $(wildcard source/*.c source/byepervisor/*.c)
OBJS := $(SRC:.c=.o)

CFLAGS  := -std=c23 -Wall -Iinclude -Iinclude/byepervisor -Ishellcode_hv -Ishellcode_kernel -Ishellcode_break_hv -Ishellcode_hv_boot -Ishellcode_hv_takeover
# pLabs5 toolchain override (see PORTING.md). Pass LLVM_CONFIG in the environment:
#   export LLVM_CONFIG=/nyaa/coding/public/PS5/dRPC5/pLabs5-SDK/tools/nixos/llvm-config
LDFLAGS :=

SC_0607_H := shellcode_0607/shellcode_0607.h
SC_HV_H := shellcode_hv/shellcode_hv.h
SC_K_H  := shellcode_kernel/shellcode_kernel.h
SC_BREAK_H := shellcode_break_hv/break_hv.h
SC_BOOT_H := shellcode_hv_boot/hv_boot.h
SC_TAKEOVER_H := shellcode_hv_takeover/takeover.h

all: $(SC_0607_H) $(SC_HV_H) $(SC_K_H) $(SC_BREAK_H) $(SC_BOOT_H) $(SC_TAKEOVER_H) $(BIN)

$(SC_0607_H):
	$(MAKE) -C shellcode_0607

$(SC_HV_H):
	$(MAKE) -C shellcode_hv

$(SC_K_H): $(SC_HV_H)
	$(MAKE) -C shellcode_kernel

$(SC_BREAK_H):
	$(MAKE) -C shellcode_break_hv

$(SC_BOOT_H):
	$(MAKE) -C shellcode_hv_boot

$(SC_TAKEOVER_H):
	$(MAKE) -C shellcode_hv_takeover

$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# The userland objects include the generated shellcode headers; make the
# dependency explicit so `make -j` cannot compile them before the blobs exist.
$(OBJS): $(SC_0607_H) $(SC_HV_H) $(SC_K_H) $(SC_BREAK_H) $(SC_BOOT_H) $(SC_TAKEOVER_H)

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -f $(BIN) $(OBJS)
	$(MAKE) -C shellcode_0607 clean
	$(MAKE) -C shellcode_hv clean
	$(MAKE) -C shellcode_kernel clean
	$(MAKE) -C shellcode_break_hv clean
	$(MAKE) -C shellcode_hv_boot clean
	$(MAKE) -C shellcode_hv_takeover clean

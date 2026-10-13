PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
PROSPERO_CLANG ?= prospero-clang

TARGET := bypervisor5XX.elf
SRC := bypervisor5XX.c

CPPFLAGS ?= -I.
CFLAGS ?= -Wall
LDLIBS ?=

export PATH := $(PS5_PAYLOAD_SDK)/bin:$(PATH)

.DEFAULT_GOAL := all

.PHONY: all clean rebuild print-config

all: $(TARGET)

$(TARGET): $(SRC) offsets.h
	$(PROSPERO_CLANG) $(CPPFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)

rebuild: clean all

print-config:
	@echo "PS5_PAYLOAD_SDK=$(PS5_PAYLOAD_SDK)"
	@echo "PROSPERO_CLANG=$(PROSPERO_CLANG)"
	@echo "TARGET=$(TARGET)"
	@echo "CPPFLAGS=$(CPPFLAGS)"
	@echo "CFLAGS=$(CFLAGS)"
	@echo "EXTRA_CFLAGS=$(EXTRA_CFLAGS)"
	@echo "LDFLAGS=$(LDFLAGS)"
	@echo "LDLIBS=$(LDLIBS)"

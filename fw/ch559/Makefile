# KB Bridge CH559 firmware (SDCC). `make` -> build/kb_bridge_ch559.{ihx,hex,bin}
# BUILD_ID = CRC32 of the sources: the nRF compares it with the HELLO to decide on an update.

SDCC    ?= sdcc
PROTO   := ../nrf/kb_bridge
SRCS    := main.c util.c link.c usbhost.c cdc.c
HDRS    := $(wildcard *.h) $(PROTO)/link_proto.h
BUILD   := build
BUILD_ID := $(shell (cat $(SRCS) $(HDRS)) | python3 -c "import sys,zlib;print('0x%08XUL'%zlib.crc32(sys.stdin.buffer.read()))")

# Code below the bootloader (0xF400) and the data flash (0xF000); xRAM 0x0000-0x01FF holds the
# USB DMA buffers (fixed addresses in the sources), the rest is for the linker.
CFLAGS  := -mmcs51 --model-small --opt-code-size -I$(PROTO) -DBUILD_ID=$(BUILD_ID)
LDFLAGS := -mmcs51 --model-small --code-size 0xF000 --xram-loc 0x0200 --xram-size 0x1600 --iram-size 256

RELS    := $(SRCS:%.c=$(BUILD)/%.rel)

all: $(BUILD)/kb_bridge_ch559.bin $(BUILD)/kb_bridge_ch559.hex
	@echo "BUILD_ID $(BUILD_ID)"
	@echo "$(BUILD_ID)" > $(BUILD)/build_id.txt

$(BUILD)/%.rel: %.c $(HDRS) | $(BUILD)
	$(SDCC) $(CFLAGS) -c $< -o $@

$(BUILD)/kb_bridge_ch559.ihx: $(RELS)
	$(SDCC) $(LDFLAGS) $(RELS) -o $@

$(BUILD)/kb_bridge_ch559.hex: $(BUILD)/kb_bridge_ch559.ihx
	packihx $< > $@

$(BUILD)/kb_bridge_ch559.bin: $(BUILD)/kb_bridge_ch559.ihx
	makebin -p $< $@

# main.c carries BUILD_ID: rebuild it whenever any source changes
$(BUILD)/main.rel: $(SRCS)

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean

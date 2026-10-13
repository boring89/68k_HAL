BUILD=build
ISO_ROOT=$(BUILD)/iso_root

ASM=nasm
CC=gcc

CFLAGS=-m32 -ffreestanding -fno-pie -fno-asynchronous-unwind-tables -nostdlib $(EXTRA_CFLAGS)

HOST_CC=gcc
HOST_CFLAGS=-std=c11 -Wall -Wextra -g -I. -fsanitize=address,undefined

.PHONY: all clean iso run test test-host test-qemu

C_SOURCES := $(shell find boot hal kernel arch emu -name "*.c")
ASM_SOURCES := $(shell find boot hal kernel arch emu -name "*.asm")

C_OBJECTS := $(C_SOURCES:%.c=$(BUILD)/%.o)
ASM_OBJECTS := $(ASM_SOURCES:%.asm=$(BUILD)/%.o)

OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)

$(info C sources: $(C_SOURCES))
$(info ASM sources: $(ASM_SOURCES))

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	mkdir -p $(dir $@)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/kernel.elf: $(OBJECTS)
	ld -m elf_i386 -T linker.ld \
		$(OBJECTS) \
		-o $@

clean:
	rm -rf $(BUILD)

iso: $(BUILD)/kernel.elf
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $(BUILD)/kernel.elf $(ISO_ROOT)/boot/
	cp grub.cfg $(ISO_ROOT)/boot/grub/
	grub-mkrescue -o $(BUILD)/68k_HAL.iso $(ISO_ROOT)

run: iso
	qemu-system-i386 -cdrom $(BUILD)/68k_HAL.iso -serial stdio

all: $(BUILD)/kernel.elf

# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------

# Hardware-independent sources that can be built and tested on the host.
HOST_LIB_SOURCES := \
	kernel/lib/k_string.c \
	kernel/lib/k_number.c \
	kernel/lib/k_memory.c \
	kernel/lib/ring_buffer.c \
	kernel/shell/parser.c \
	$(wildcard emu/m68k/*.c)

HOST_LIB_HEADERS := $(wildcard kernel/lib/*.h emu/m68k/*.h) kernel/shell/parser.h

HOST_TESTS := $(patsubst tests/host/%.c,$(BUILD)/host/%,$(wildcard tests/host/test_*.c))

$(BUILD)/host/%: tests/host/%.c tests/host/test.h $(HOST_LIB_SOURCES) $(HOST_LIB_HEADERS)
	mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) $< $(HOST_LIB_SOURCES) -o $@

test-host: $(HOST_TESTS)
	@for t in $(HOST_TESTS); do ./$$t || exit 1; done

# Kernel built with -DKERNEL_SELFTEST, booted directly by QEMU (no ISO).
# QEMU exits with 33 on PASS (see kernel/test/selftest.c).
QEMU_TEST_FLAGS := -display none -serial stdio -no-reboot \
	-device isa-debug-exit,iobase=0xf4,iosize=0x04

test-qemu:
	$(MAKE) --no-print-directory BUILD=$(BUILD)/test \
		EXTRA_CFLAGS=-DKERNEL_SELFTEST $(BUILD)/test/kernel.elf
	@timeout 10 qemu-system-i386 -kernel $(BUILD)/test/kernel.elf $(QEMU_TEST_FLAGS); \
	status=$$?; \
	if [ $$status -eq 33 ]; then \
		echo "QEMU selftest: PASS"; \
	else \
		echo "QEMU selftest: FAIL (exit status $$status)"; \
		exit 1; \
	fi

test: test-host test-qemu

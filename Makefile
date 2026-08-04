BUILD=build
ISO_ROOT=$(BUILD)/iso_root

ASM=nasm
CC=gcc

CFLAGS=-m32 -ffreestanding -fno-pie -fno-asynchronous-unwind-tables -nostdlib

.PHONY: all clean iso run

C_SOURCES := $(shell find boot hal kernel arch -name "*.c")
ASM_SOURCES := $(shell find boot hal kernel arch -name "*.asm")

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
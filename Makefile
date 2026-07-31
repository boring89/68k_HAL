BUILD=build
ISO_ROOT=$(BUILD)/iso_root

ASM=nasm
CC=gcc

CFLAGS=-m32 -ffreestanding -fno-pie -fno-asynchronous-unwind-tables -nostdlib

all: $(BUILD)/kernel.elf

.PHONY: all clean iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: boot/boot.asm | $(BUILD)
	$(ASM) -f elf32 boot/boot.asm -o $(BUILD)/boot.o

$(BUILD)/vga.o: hal/vga/vga.c | $(BUILD)
	$(CC) $(CFLAGS) -c hal/vga/vga.c -o $(BUILD)/vga.o

$(BUILD)/serial.o: hal/serial/serial.c | $(BUILD)
	$(CC) $(CFLAGS) -c hal/serial/serial.c -o $(BUILD)/serial.o

$(BUILD)/console.o: kernel/console/console.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/console/console.c -o $(BUILD)/console.o

$(BUILD)/log.o: kernel/log/log.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/log/log.c -o $(BUILD)/log.o

$(BUILD)/kernel.o: kernel/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/main.c -o $(BUILD)/kernel.o


$(BUILD)/kernel.elf: \
	$(BUILD)/boot.o \
	$(BUILD)/kernel.o \
	$(BUILD)/vga.o \
	$(BUILD)/serial.o \
	$(BUILD)/console.o \
	$(BUILD)/log.o

	ld -m elf_i386 -T linker.ld \
		$(BUILD)/boot.o \
		$(BUILD)/kernel.o \
		$(BUILD)/vga.o \
		$(BUILD)/serial.o \
		$(BUILD)/console.o \
		$(BUILD)/log.o \
		-o $(BUILD)/kernel.elf

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
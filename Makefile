BUILD=build
ISO_ROOT=$(BUILD)/iso_root

ASM=nasm
CC=gcc

CFLAGS=-m32 -ffreestanding -fno-pie -fno-asynchronous-unwind-tables -nostdlib

all: $(BUILD)/kernel.elf


$(BUILD):
	mkdir -p $(BUILD)


$(BUILD)/boot.o: boot/boot.asm | $(BUILD)
	$(ASM) -f elf32 boot/boot.asm -o $(BUILD)/boot.o

$(BUILD)/vga.o: hal/vga.c | $(BUILD)
	$(CC) $(CFLAGS) -c hal/vga.c -o $(BUILD)/vga.o

$(BUILD)/kernel.o: kernel/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/main.c -o $(BUILD)/kernel.o


$(BUILD)/kernel.elf: $(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/vga.o
	ld -m elf_i386 -T linker.ld \
	$(BUILD)/boot.o \
	$(BUILD)/kernel.o \
	$(BUILD)/vga.o \
	-o $(BUILD)/kernel.elf

clean:
	rm -rf $(BUILD)

iso: $(BUILD)/kernel.elf
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $(BUILD)/kernel.elf $(ISO_ROOT)/boot/
	cp grub.cfg $(ISO_ROOT)/boot/grub/
	grub-mkrescue -o $(BUILD)/68k_HAL.iso $(ISO_ROOT)
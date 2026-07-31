BUILD=build
ISO_ROOT=$(BUILD)/iso_root

ASM=nasm
CC=gcc

CFLAGS=-m32 -ffreestanding -fno-pie -fno-asynchronous-unwind-tables -nostdlib

all: $(BUILD)/kernel.elf

.PHONY: all clean iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/gdt.o: boot/gdt.asm | $(BUILD)
	$(ASM) -f elf32 boot/gdt.asm -o $(BUILD)/gdt.o

$(BUILD)/boot.o: boot/boot.asm | $(BUILD)
	$(ASM) -f elf32 boot/boot.asm -o $(BUILD)/boot.o

$(BUILD)/isr.o: kernel/interrupt/isr.asm | $(BUILD)
	$(ASM) -f elf32 kernel/interrupt/isr.asm -o $(BUILD)/isr.o

$(BUILD)/vga.o: hal/vga/vga.c | $(BUILD)
	$(CC) $(CFLAGS) -c hal/vga/vga.c -o $(BUILD)/vga.o

$(BUILD)/serial.o: hal/serial/serial.c | $(BUILD)
	$(CC) $(CFLAGS) -c hal/serial/serial.c -o $(BUILD)/serial.o

$(BUILD)/console.o: kernel/console/console.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/console/console.c -o $(BUILD)/console.o

$(BUILD)/log.o: kernel/log/log.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/log/log.c -o $(BUILD)/log.o

$(BUILD)/k_string.o: kernel/lib/k_string.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/lib/k_string.c -o $(BUILD)/k_string.o

$(BUILD)/k_number.o: kernel/lib/k_number.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/lib/k_number.c -o $(BUILD)/k_number.o

$(BUILD)/printf.o: kernel/printf/printf.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/printf/printf.c -o $(BUILD)/printf.o

$(BUILD)/idt.o: kernel/interrupt/idt.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/interrupt/idt.c -o $(BUILD)/idt.o

$(BUILD)/interrupt.o: kernel/interrupt/interrupt.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/interrupt/interrupt.c -o $(BUILD)/interrupt.o

$(BUILD)/kernel.o: kernel/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/main.c -o $(BUILD)/kernel.o


$(BUILD)/kernel.elf: \
	$(BUILD)/gdt.o \
	$(BUILD)/boot.o \
	$(BUILD)/kernel.o \
	$(BUILD)/vga.o \
	$(BUILD)/serial.o \
	$(BUILD)/console.o \
	$(BUILD)/log.o \
	$(BUILD)/k_string.o \
	$(BUILD)/k_number.o \
	$(BUILD)/printf.o \
	$(BUILD)/idt.o \
	$(BUILD)/interrupt.o \
	$(BUILD)/isr.o

	ld -m elf_i386 -T linker.ld \
		$(BUILD)/gdt.o \
		$(BUILD)/boot.o \
		$(BUILD)/kernel.o \
		$(BUILD)/vga.o \
		$(BUILD)/serial.o \
		$(BUILD)/console.o \
		$(BUILD)/log.o \
		$(BUILD)/k_string.o \
		$(BUILD)/k_number.o \
		$(BUILD)/printf.o \
		$(BUILD)/idt.o \
		$(BUILD)/interrupt.o \
		$(BUILD)/isr.o \
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
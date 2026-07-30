BUILD=build

ASM=nasm
CC=gcc

CFLAGS=-m32 -ffreestanding -fno-pie -nostdlib

all: $(BUILD)/kernel.elf


$(BUILD):
	mkdir -p $(BUILD)


$(BUILD)/boot.o: boot/boot.asm | $(BUILD)
	$(ASM) -f elf32 boot/boot.asm -o $(BUILD)/boot.o


$(BUILD)/kernel.o: kernel/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c kernel/main.c -o $(BUILD)/kernel.o


$(BUILD)/kernel.elf: $(BUILD)/boot.o $(BUILD)/kernel.o
	ld -m elf_i386 -T linker.ld \
	$(BUILD)/boot.o $(BUILD)/kernel.o \
	-o $(BUILD)/kernel.elf


clean:
	rm -rf $(BUILD)
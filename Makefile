# The kernel's architecture: x86_64 (the default, docs/x86_64.md) or i386 (32-bit protected mode).
# make ARCH=i386 builds into build/i386 and runs the tests with qemu-system-i386. The programs
# for the program disk (build/initrd, build/sdk) are i386 programs for both.
ARCH ?= x86_64

ifeq ($(ARCH),x86_64)
BUILD ?= build
else ifeq ($(ARCH),i386)
BUILD ?= build/i386
else
$(error ARCH must be i386 or x86_64)
endif

ISO_ROOT=$(BUILD)/iso_root

ASM=nasm
CC=gcc

ifeq ($(ARCH),x86_64)
# The "kernel" code model puts the kernel in the top 2 GiB; no red zone because interrupts use the
# kernel stack; no SSE/x87 in kernel code (user programs' FPU state is saved with FXSAVE).
# HAL_PHYS_BASE: where all RAM is mapped (hal/mmu.h); HAL_KERNEL_OFFSET: virtual - physical of the
# kernel image (linker64.ld).
ARCH_FLAGS := -m64 -mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
	-DHAL_PHYS_BASE=0xFFFF800000000000ull -DHAL_KERNEL_OFFSET=0xFFFFFFFF80000000ull
# Musashi may use double (its FPU emulation), so the compiler may use SSE there.
ARCH_FLAGS_MUSASHI := -m64 -mcmodel=kernel -mno-red-zone
BOOT_ASM := platform/pc/boot/boot64.asm
ASM_FORMAT := elf64
LD_ARCH := -m elf_x86_64 -z max-page-size=0x1000 -z noseparate-code
LINKER_SCRIPT := linker64.ld
QEMU ?= qemu-system-x86_64
# Files of arch/x86 and platform/pc that only exist for the 32-bit kernel.
ARCH_EXCLUDE := arch/x86/gdt arch/x86/interrupt/idt.c arch/x86/interrupt/isr.asm \
	arch/x86/interrupt/irq_stub.asm arch/x86/interrupt/exception.c arch/x86/mmu arch/x86/task \
	arch/x86/user arch/x86/test platform/pc/boot/boot.asm platform/pc/smp/trampoline.asm
else
ARCH_FLAGS := -m32
ARCH_FLAGS_MUSASHI := -m32
BOOT_ASM := platform/pc/boot/boot.asm
ASM_FORMAT := elf32
LD_ARCH := -m elf_i386
LINKER_SCRIPT := linker.ld
QEMU ?= qemu-system-i386
ARCH_EXCLUDE := arch/x86_64 platform/pc/boot/boot64.asm platform/pc/smp/trampoline64.asm
endif

# The tools/*.py scripts start this QEMU for the tests that boot an image.
export QEMU_SYSTEM := $(QEMU)

CFLAGS=$(ARCH_FLAGS) -std=gnu11 -ffreestanding -I. -Wall -Wextra -Werror -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -MMD -MP \
	-DMUSASHI_CNF='"emu/m68k/m68kconf.h"' $(EXTRA_CFLAGS)

HOST_CC=gcc
HOST_CFLAGS=-std=c11 -Wall -Wextra -Werror -g -I. -Ithird_party/musashi \
	-DMUSASHI_CNF='"emu/m68k/m68kconf.h"' -fsanitize=address,undefined -MMD -MP

.PHONY: test-pci-roots test-power usb-image test-usb-image test-e1000e test-usb test-noapic test-q35 test-uefi test-fb test-mac-keys test-virtio-net test-net test-video test-mac-sound test-mac-persist test-mac-clock sdk initrd test-shell all clean iso run test test-boundary test-host test-qemu test-sst test-rom test-rom-qemu mac-iso mac-screenshot mac-input-test test-mac-stub run-mac

.DEFAULT_GOAL := all

# The program disk (see the user programs section); defined early because the iso rule needs it.
INITRD := build/initrd.tar

# Musashi (third_party/musashi) is unmodified third-party code. m68kmake
# generates m68kops.c/h from m68k_in.c. The core always includes its FPU code,
# which needs softfloat. Musashi itself is built without -Werror (its style
# does not pass our warning flags); our wrapper in emu/m68k/ is built normally.
# The same sources are built twice: for the host tests and, further down, for
# the kernel (-m32, with the stand-in libc headers and functions in
# emu/m68k/freestanding/).
MUSASHI := third_party/musashi
MUSASHI_GEN := $(BUILD)/host/musashi
MUSASHI_CNF := emu/m68k/m68kconf.h
MUSASHI_CFLAGS := -std=gnu11 -w -g -I. -I$(MUSASHI) -I$(MUSASHI_GEN) \
	-DMUSASHI_CNF='"$(MUSASHI_CNF)"' -MMD -MP
MUSASHI_SOURCES := $(MUSASHI)/m68kcpu.c $(MUSASHI)/softfloat/softfloat.c
MUSASHI_OBJECTS := $(MUSASHI_SOURCES:%.c=$(BUILD)/host/obj/%.o) \
	$(BUILD)/host/obj/musashi_gen/m68kops.o

$(MUSASHI_GEN)/m68kmake: $(MUSASHI)/m68kmake.c
	mkdir -p $(dir $@)
	$(HOST_CC) -O1 -w $< -o $@

# m68kmake writes m68kops.c and m68kops.h next to each other.
$(MUSASHI_GEN)/m68kops.c $(MUSASHI_GEN)/m68kops.h: $(MUSASHI_GEN)/m68kmake $(MUSASHI)/m68k_in.c
	$<  $(MUSASHI_GEN) $(MUSASHI)/m68k_in.c

$(BUILD)/host/obj/musashi_gen/m68kops.o: $(MUSASHI_GEN)/m68kops.c $(MUSASHI_GEN)/m68kops.h
	mkdir -p $(dir $@)
	$(HOST_CC) $(MUSASHI_CFLAGS) -c $< -o $@

# The core includes the generated m68kops.h.
$(BUILD)/host/obj/$(MUSASHI)/%.o: $(MUSASHI)/%.c $(MUSASHI_GEN)/m68kops.h
	mkdir -p $(dir $@)
	$(HOST_CC) $(MUSASHI_CFLAGS) -c $< -o $@

-include $(MUSASHI_OBJECTS:.o=.d)

# Kernel build of Musashi. The stand-in headers come first (-isystem) because
# the build machine has no 32-bit libc headers.
KMUSASHI_CFLAGS := $(ARCH_FLAGS_MUSASHI) -ffreestanding -O2 -w -fno-pie -fno-stack-protector \
	-fno-asynchronous-unwind-tables -nostdlib -I. -I$(MUSASHI) \
	-I$(MUSASHI_GEN) -DMUSASHI_CNF='"$(MUSASHI_CNF)"' \
	-isystem emu/m68k/freestanding/include -MMD -MP
KMUSASHI_OBJECTS := $(MUSASHI_SOURCES:%.c=$(BUILD)/%.o) \
	$(BUILD)/musashi_gen/m68kops.o

$(BUILD)/musashi_gen/m68kops.o: $(MUSASHI_GEN)/m68kops.c $(MUSASHI_GEN)/m68kops.h
	mkdir -p $(dir $@)
	$(CC) $(KMUSASHI_CFLAGS) -c $< -o $@

$(BUILD)/$(MUSASHI)/%.o: $(MUSASHI)/%.c $(MUSASHI_GEN)/m68kops.h
	mkdir -p $(dir $@)
	$(CC) $(KMUSASHI_CFLAGS) -c $< -o $@

-include $(KMUSASHI_OBJECTS:.o=.d)


# Only search directories that exist (e.g. drivers/ is picked up once created).
# emu/ (the 68k emulator) is built into the kernel as well as for host tests.
SRC_DIRS := $(wildcard arch drivers emu hal kernel platform)

C_SOURCES := $(filter-out $(addsuffix %,$(ARCH_EXCLUDE)),$(shell find $(SRC_DIRS) -name "*.c"))
ASM_SOURCES := $(filter-out $(addsuffix %,$(ARCH_EXCLUDE)),$(shell find $(SRC_DIRS) -name "*.asm"))

C_OBJECTS := $(C_SOURCES:%.c=$(BUILD)/%.o)
ASM_OBJECTS := $(ASM_SOURCES:%.asm=$(BUILD)/%.o)

OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS) $(KMUSASHI_OBJECTS)


$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# The stand-in libc for Musashi has floating point functions (sin, pow...): SSE is allowed there.
$(BUILD)/emu/m68k/freestanding/%.o: emu/m68k/freestanding/%.c
	mkdir -p $(dir $@)
	$(CC) $(filter-out -mno-sse -mno-mmx -mno-80387,$(CFLAGS)) -c $< -o $@

$(BUILD)/%.o: %.asm
	mkdir -p $(dir $@)
	$(ASM) -f $(ASM_FORMAT) -I. $< -o $@

# The 64-bit kernel is linked as an ELF64 file (kernel.elf.elf64, for debugging) and converted to a
# 32-bit ELF file: GRUB's "multiboot" command and QEMU's -kernel only load those, and the entry code
# is 32-bit anyway. The load addresses (physical) survive the conversion, the high virtual ones do not.
ifeq ($(ARCH),x86_64)
define LINK_KERNEL
	ld $(LD_ARCH) -T $(LINKER_SCRIPT) $(1) -o $(2).elf64
	objcopy -O elf32-i386 $(2).elf64 $(2)
endef
else
define LINK_KERNEL
	ld $(LD_ARCH) -T $(LINKER_SCRIPT) $(1) -o $(2)
endef
endif

$(BUILD)/kernel.elf: $(OBJECTS) $(LINKER_SCRIPT)
	$(call LINK_KERNEL,$(OBJECTS),$@)

# The same kernel with a Multiboot header that asks for a graphics mode.
BOOT_OBJECT := $(BOOT_ASM:%.asm=$(BUILD)/%.o)

$(BUILD)/boot_gfx.o: $(BOOT_ASM)
	mkdir -p $(dir $@)
	$(ASM) -f $(ASM_FORMAT) -d GRAPHICS $< -o $@

$(BUILD)/kernel-gfx.elf: $(filter-out $(BOOT_OBJECT),$(OBJECTS)) $(BUILD)/boot_gfx.o $(LINKER_SCRIPT)
	$(call LINK_KERNEL,$(filter-out $(LINKER_SCRIPT),$^),$@)

# For tools/uefi_check.py: the graphics image with a Multiboot2 header that asks for 1920x1080.
$(BUILD)/boot_gfx1080.o: $(BOOT_ASM)
	mkdir -p $(dir $@)
	$(ASM) -f $(ASM_FORMAT) -d GRAPHICS -d GFX_WIDTH=1920 -d GFX_HEIGHT=1080 $< -o $@

$(BUILD)/kernel-gfx1080.elf: $(filter-out $(BOOT_OBJECT),$(OBJECTS)) $(BUILD)/boot_gfx1080.o $(LINKER_SCRIPT)
	$(call LINK_KERNEL,$(filter-out $(LINKER_SCRIPT),$^),$@)

# Header dependencies generated by -MMD.
-include $(C_OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD)

iso: $(BUILD)/kernel.elf $(BUILD)/kernel-gfx.elf $(INITRD)
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $(BUILD)/kernel.elf $(BUILD)/kernel-gfx.elf $(INITRD) $(ISO_ROOT)/boot/
	cp grub.cfg $(ISO_ROOT)/boot/grub/
	grub-mkrescue -o $(BUILD)/68k_HAL.iso $(ISO_ROOT)

# A bootable image for a USB stick (BIOS/CSM and UEFI): GRUB menu with normal, safe mode,
# hardware diagnostics and Mac entries. It is only built here; writing it to a stick is up to the
# user (docs/p520.md). With MAC_ROM and MAC_DISK existing, copies of them go on the image (local
# use only; the image must never be committed or shared).
USB_IMAGE := $(BUILD)/68k_HAL_usb.img
usb-image: $(BUILD)/kernel.elf $(BUILD)/kernel-gfx.elf $(BUILD)/kernel-gfx1080.elf $(INITRD) grub-usb.cfg
	rm -rf $(BUILD)/usb_root
	mkdir -p $(BUILD)/usb_root/boot/grub
	cp $(BUILD)/kernel.elf $(BUILD)/kernel-gfx.elf $(BUILD)/kernel-gfx1080.elf $(INITRD) $(BUILD)/usb_root/boot/
	cp grub-usb.cfg $(BUILD)/usb_root/boot/grub/grub.cfg
	@if [ -f "$(MAC_ROM)" ] && [ -f "$(MAC_DISK)" ]; then \
		echo "usb-image: adding the Mac ROM and disk (local copy)"; \
		cp "$(MAC_ROM)" $(BUILD)/usb_root/mac_rom; cp "$(MAC_DISK)" $(BUILD)/usb_root/mac_disk; \
	else echo "usb-image: no Mac ROM/disk (MAC_ROM, MAC_DISK): the Mac entry will say so"; fi
	grub-mkrescue -o $(USB_IMAGE) $(BUILD)/usb_root
	@ls -l $(USB_IMAGE)

test-usb-image: usb-image
	python3 -I tools/usb_image_test.py $(USB_IMAGE)

run: iso
	$(QEMU) -cdrom $(BUILD)/68k_HAL.iso -serial stdio -netdev user,id=n0 -device e1000,netdev=n0

all: $(BUILD)/kernel.elf $(BUILD)/kernel-gfx.elf

# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------

# Hardware-independent sources that can be built and tested on the host.
HOST_LIB_SOURCES := \
	arch/x86/cpu/cpuid_decode.c \
	arch/x86/interrupt/page_fault.c \
	arch/x86/mmu/paging_entry.c \
	arch/x86_64/mmu/paging_entry.c \
	drivers/ps2_keyboard/keymap.c \
	drivers/ps2_keyboard/scancode.c \
	drivers/ps2_mouse/mouse_packet.c \
	kernel/input/event.c \
	kernel/console/history.c \
	kernel/usb/descriptor.c \
	kernel/usb/hid.c \
	kernel/input/linux_keys.c \
	kernel/keyboard/keychar.c \
	kernel/keyboard/keyseq.c \
	kernel/mac/adbkeys.c \
	kernel/block/block.c \
	kernel/block/ramdisk.c \
	kernel/console/sink.c \
	kernel/display/text_display.c \
	kernel/display/fb_pattern.c \
	kernel/display/fb_scale.c \
	kernel/display/fbcon.c \
	kernel/display/fbcon_font.c \
	kernel/fs/apm.c \
	kernel/fs/hfs/hfs.c \
	kernel/fs/volume.c \
	kernel/lib/k_string.c \
	kernel/lib/k_number.c \
	kernel/lib/k_snprintf.c \
	kernel/lib/k_memory.c \
	kernel/lib/k_time.c \
	kernel/lib/ring_buffer.c \
	kernel/memory/heap.c \
	kernel/net/packet.c \
	kernel/net/tcp.c \
	kernel/pci/pci_names.c \
	kernel/memory/pmm.c \
	kernel/shell/parser.c \
	kernel/shell/complete.c \
	kernel/usb/usb.c \
	kernel/sound/sound.c \
	kernel/user/elf.c \
	kernel/user/region.c \
	kernel/vfs/procfs.c \
	kernel/vfs/ramfs.c \
	kernel/vfs/tarfs.c \
	kernel/vfs/vfs.c \
	kernel/user/stack.c \
	kernel/terminal/ansi.c \
	kernel/terminal/input.c \
	kernel/terminal/terminal.c \
	platform/pc/boot/hal_boot.c \
	platform/pc/boot/multiboot.c \
	platform/pc/acpi/acpi_parse.c \
	platform/pc/pci/pci_bar.c \
	platform/pc/rtc/rtc_decode.c \
	platform/pc/timer/tsc_math.c \
	$(wildcard emu/m68k/*.c) \
	$(wildcard emu/mac/*.c)

HOST_LIB_OBJECTS = $(HOST_LIB_SOURCES:%.c=$(BUILD)/host/obj/%.o) $(MUSASHI_OBJECTS)

HOST_TESTS := $(patsubst tests/host/%.c,$(BUILD)/host/%,$(wildcard tests/host/test_*.c))
HOST_TEST_OBJECTS := $(HOST_TESTS:$(BUILD)/host/%=$(BUILD)/host/obj/tests/host/%.o)

# Keep test objects so make does not delete them as intermediates.
.SECONDARY: $(HOST_TEST_OBJECTS)


# Each source is compiled separately so -MMD can emit one .d file per object.
$(BUILD)/host/obj/%.o: %.c
	mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

$(BUILD)/host/%: $(BUILD)/host/obj/tests/host/%.o $(HOST_LIB_OBJECTS)
	mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) $^ -lm -o $@

# 68k test programs (tests/m68k/*.s), assembled with the GNU m68k binutils
# and flattened to raw binaries that tests/host/test_m68k.c loads.
M68K_AS := m68k-linux-gnu-as
M68K_OBJCOPY := m68k-linux-gnu-objcopy
M68K_TEST_BINS := $(patsubst tests/m68k/%.s,$(BUILD)/m68k/%.bin,$(wildcard tests/m68k/*.s))

$(BUILD)/m68k/%.bin: tests/m68k/%.s
	mkdir -p $(dir $@)
	$(M68K_AS) -m68030 -o $(@:.bin=.o) $<
	$(M68K_OBJCOPY) -O binary -j .text $(@:.bin=.o) $@

-include $(HOST_LIB_OBJECTS:.o=.d) $(HOST_TEST_OBJECTS:.o=.d)

# Host runner for the Macintosh model (see docs/mac.md).
$(BUILD)/host/mac_run: tools/mac_run.c $(HOST_LIB_OBJECTS) $(BUILD)/host/obj/$(MUSASHI)/m68kdasm.o
	mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) $^ -lm -o $@

# The same runner without the sanitizers and with optimization, about 10x
# faster; for long runs of the real ROM.
MAC_FAST_SOURCES := tools/mac_run.c $(wildcard emu/mac/*.c) emu/m68k/vm.c \
	emu/m68k/trap.c kernel/lib/k_memory.c
$(BUILD)/host/mac_run_fast: $(MAC_FAST_SOURCES) $(wildcard emu/mac/*.h) $(wildcard emu/m68k/*.h) $(MUSASHI_GEN)/m68kops.c \
		$(MUSASHI)/m68kcpu.c $(MUSASHI)/m68kdasm.c $(MUSASHI)/softfloat/softfloat.c
	mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -w -I. -I$(MUSASHI) -I$(MUSASHI_GEN) \
		-DMUSASHI_CNF='"$(MUSASHI_CNF)"' $(MAC_FAST_SOURCES) \
		$(MUSASHI_GEN)/m68kops.c $(MUSASHI)/m68kcpu.c $(MUSASHI)/m68kdasm.c \
		$(MUSASHI)/softfloat/softfloat.c -lm -o $@

# The user's own ROM and disk image. They are never copied into the
# repository, and make test does not need them.
MAC_ROM ?= $(HOME)/mac/mac_rom_archive_-_as_of_8-19-2011/368CADFE - Mac IIci.ROM
MAC_DISK ?= $(HOME)/mac/System7_5_3.img

# Boots the real ROM on the host. Skips (successfully) when it is missing.
test-rom: $(BUILD)/host/mac_run_fast
	@if [ ! -f "$(MAC_ROM)" ]; then \
		echo "test-rom: SKIP (no ROM at '$(MAC_ROM)'; set MAC_ROM=...)"; \
	else \
		$(BUILD)/host/mac_run_fast -n 2000000 "$(MAC_ROM)"; \
		if [ -f "$(MAC_DISK)" ]; then \
			n=$$($(BUILD)/host/mac_run_fast -F 1200 -G -d "$(MAC_DISK)" "$(MAC_ROM)" | grep -c '^gdevice'); \
			if [ "$$n" != 1 ]; then echo "test-rom: FAIL: $$n GDevices after boot (want 1: empty slots must not look like cards)"; exit 1; fi; \
			echo "test-rom: ok: one GDevice"; \
			for want in 1:800x600 9:1920x1080 12:3840x2160; do \
				got=$$($(BUILD)/host/mac_run_fast -F 3700 -C 1024x768 $$(tools/mac_monitors.sh $${want%%:*}) \
					-d "$(MAC_DISK)" "$(MAC_ROM)" | sed -n 's/^mac: screen crc .*(\(.*\))$$/\1/p'); \
				if [ "$$got" != "$${want#*:}" ]; then echo "test-rom: FAIL: Monitors gave $$got, want $${want#*:}"; exit 1; fi; \
			done; \
			echo "test-rom: ok: Monitors switched the NuBus card to 800x600, 1920x1080, 3840x2160"; \
		fi; \
	fi

# The same Mac in the kernel (kernel/mac), booted by QEMU with the ROM and the
# disk image as boot modules (kernel/mac/machine.h). The kernel runs
# MAC_TEST_FRAMES frames without a screen and prints the CRC-32 of the final
# picture, which must equal what the host runner computes for the same number
# of frames. KVM is used when available. Local only: needs MAC_ROM and
# MAC_DISK.
MAC_TEST_FRAMES ?= 600
test-rom-qemu: $(BUILD)/host/mac_run_fast
	@if [ ! -f "$(MAC_ROM)" ] || [ ! -f "$(MAC_DISK)" ]; then \
		echo "test-rom-qemu: SKIP (no ROM or disk; set MAC_ROM and MAC_DISK)"; exit 0; fi; \
	mkdir -p $(BUILD)/mac; \
	$(MAKE) --no-print-directory BUILD=$(BUILD)/mac/kernel-$(MAC_TEST_FRAMES) \
		EXTRA_CFLAGS="-DKERNEL_MAC_TEST -DKERNEL_MAC_TEST_FRAMES=$(MAC_TEST_FRAMES)" \
		$(BUILD)/mac/kernel-$(MAC_TEST_FRAMES)/kernel.elf || exit 1; \
	cp "$(MAC_ROM)" $(BUILD)/mac/rom.bin; \
	cp "$(MAC_DISK)" $(BUILD)/mac/disk.img; \
	want=$$($(BUILD)/host/mac_run_fast -F $(MAC_TEST_FRAMES) -d "$(MAC_DISK)" "$(MAC_ROM)" | sed -n 's/^mac: screen crc \([0-9a-f]*\) .*/\1/p'); \
	accel=""; [ -w /dev/kvm ] && accel="-enable-kvm"; \
	out=$(BUILD)/mac/qemu.out; \
	$(QEMU) $$accel -m 128 -display none -serial stdio \
		-kernel $(BUILD)/mac/kernel-$(MAC_TEST_FRAMES)/kernel.elf \
		-initrd "$(BUILD)/mac/rom.bin mac_rom,$(BUILD)/mac/disk.img mac_disk" > $$out 2>&1 & \
	pid=$$!; n=0; \
	until grep -q "mac: screen crc\|mac: could not run" $$out || [ $$n -ge 900 ]; do sleep 1; n=$$((n + 1)); done; \
	kill $$pid 2>/dev/null; wait $$pid 2>/dev/null; \
	got=$$(sed -n 's/.*mac: screen crc \([0-9A-Fa-f]*\) .*/\1/p' $$out | head -1 | tr 'A-F' 'a-f'); \
	echo "test-rom-qemu: host crc $$want, kernel crc $$got"; \
	[ -n "$$want" ] && [ "$$want" = "$$got" ]

# The Mac's sound: the system beeps (mac_sysbeep, injected at frame 1200) in the
# kernel under QEMU with an HDA sound card whose output is recorded; the
# recording must contain the 440 Hz beep. Local only: needs MAC_ROM and
# MAC_DISK.
test-mac-sound:
	@if [ ! -f "$(MAC_ROM)" ] || [ ! -f "$(MAC_DISK)" ]; then \
		echo "test-mac-sound: SKIP (no ROM or disk; set MAC_ROM and MAC_DISK)"; exit 0; fi; \
	mkdir -p $(BUILD)/mac; \
	$(MAKE) --no-print-directory BUILD=$(BUILD)/mac/kernel-beep \
		EXTRA_CFLAGS="-DKERNEL_MAC_TEST -DKERNEL_MAC_TEST_FRAMES=1500 -DKERNEL_MAC_TEST_BEEP=1200" \
		$(BUILD)/mac/kernel-beep/kernel.elf || exit 1; \
	cp "$(MAC_ROM)" $(BUILD)/mac/rom.bin; \
	cp "$(MAC_DISK)" $(BUILD)/mac/disk.img; \
	accel=""; [ -w /dev/kvm ] && accel="-enable-kvm"; \
	out=$(BUILD)/mac/qemu_sound.out; \
	rm -f $(BUILD)/mac/beep.wav; \
	$(QEMU) $$accel -m 128 -display none -serial stdio \
		-audiodev wav,id=snd0,path=$(BUILD)/mac/beep.wav \
		-device intel-hda -device hda-output,audiodev=snd0 \
		-kernel $(BUILD)/mac/kernel-beep/kernel.elf \
		-initrd "$(BUILD)/mac/rom.bin mac_rom,$(BUILD)/mac/disk.img mac_disk" > $$out 2>&1 & \
	pid=$$!; n=0; \
	until grep -q "mac: screen crc\|mac: could not run" $$out || [ $$n -ge 900 ]; do sleep 1; n=$$((n + 1)); done; \
	kill -INT $$pid 2>/dev/null; sleep 1; kill $$pid 2>/dev/null; wait $$pid 2>/dev/null; \
	python3 -I tools/check_wav.py $(BUILD)/mac/beep.wav 440

# Writing to the ATA drive: the Mac runs from a COPY of MAC_DISK as an IDE drive
# (no mac_disk module), makes a folder in the Finder and shuts down (the
# system then writes its changes), and the folder must be on the image
# afterwards. Needs hfsutils to look into the image. Local only.
test-mac-persist:
	@if [ ! -f "$(MAC_ROM)" ] || [ ! -f "$(MAC_DISK)" ]; then \
		echo "test-mac-persist: SKIP (no ROM or disk; set MAC_ROM and MAC_DISK)"; exit 0; fi; \
	mkdir -p $(BUILD)/mac; \
	$(MAKE) --no-print-directory BUILD=$(BUILD)/mac/kernel-persist \
		EXTRA_CFLAGS="-DKERNEL_MAC_TEST -DKERNEL_MAC_TEST_FRAMES=2600 -DKERNEL_MAC_TEST_FOLDER" \
		$(BUILD)/mac/kernel-persist/kernel.elf || exit 1; \
	cp "$(MAC_ROM)" $(BUILD)/mac/rom.bin; \
	cp "$(MAC_DISK)" $(BUILD)/mac/persist.img; \
	accel=""; [ -w /dev/kvm ] && accel="-enable-kvm"; \
	out=$(BUILD)/mac/qemu_persist.out; \
	$(QEMU) $$accel -m 128 -display none -serial stdio \
		-drive file=$(BUILD)/mac/persist.img,format=raw,if=ide,index=0,media=disk \
		-kernel $(BUILD)/mac/kernel-persist/kernel.elf \
		-initrd "$(BUILD)/mac/rom.bin mac_rom" > $$out 2>&1 & \
	pid=$$!; n=0; \
	until grep -q "mac: screen crc\|mac: could not run" $$out || [ $$n -ge 900 ]; do sleep 1; n=$$((n + 1)); done; \
	kill $$pid 2>/dev/null; wait $$pid 2>/dev/null; \
	if (export HOME=$(abspath $(BUILD)/mac/hfshome); mkdir -p $$HOME; \
		hmount $(BUILD)/mac/persist.img > /dev/null 2>&1 && \
		hls -la ":Desktop Folder" | grep -q "untitled folder"); then \
		echo "test-mac-persist: PASS (the new folder is on the disk)"; \
	else echo "test-mac-persist: FAIL (no new folder on the disk)"; exit 1; fi

# The Mac's real-time behaviour: the normal kernel with the Mac modules runs
# the Mac for a while; its frames must follow the real clock and its own clock
# (the system's Time global) must show the real time. Local only.
test-mac-clock: $(BUILD)/kernel.elf
	@if [ ! -f "$(MAC_ROM)" ] || [ ! -f "$(MAC_DISK)" ]; then \
		echo "test-mac-clock: SKIP (no ROM or disk; set MAC_ROM and MAC_DISK)"; exit 0; fi; \
	rm -rf $(BUILD)/mac/clock; mkdir -p $(BUILD)/mac/clock/boot/grub; \
	cp $(BUILD)/kernel.elf $(BUILD)/mac/clock/boot/; \
	cp "$(MAC_ROM)" $(BUILD)/mac/clock/mac_rom; cp "$(MAC_DISK)" $(BUILD)/mac/clock/mac_disk; \
	printf 'set default=0\nset timeout=0\nmenuentry "mac" {\n  multiboot /boot/kernel.elf\n  module /mac_rom mac_rom\n  module /mac_disk mac_disk\n  boot\n}\n' > $(BUILD)/mac/clock/boot/grub/grub.cfg; \
	grub-mkrescue -o $(BUILD)/mac/clock.iso $(BUILD)/mac/clock > /dev/null 2>&1 || exit 1; \
	python3 -I tools/mac_clock_check.py $(BUILD)/mac/clock.iso

# A screenshot of the Mac running in the kernel under QEMU (GRUB graphics
# mode, 640x480): build/mac/qemu_screen.ppm. MAC_SHOT_FRAMES frames (about
# 2.4 ms each with KVM) are run before the picture is taken. Local only.
MAC_SHOT_FRAMES ?= 4200
# Enlargement of the picture (make mac-screenshot MAC_SHOT_SCALE=2 gives a
# 1280x960 screenshot).
MAC_SHOT_SCALE ?= 1

# The ISO for those: the kernel in graphics mode with the ROM and the disk
# image (copies in build/mac/iso, never in the repository).
mac-iso:
	@if [ ! -f "$(MAC_ROM)" ] || [ ! -f "$(MAC_DISK)" ]; then \
		echo "mac-iso: SKIP (no ROM or disk; set MAC_ROM and MAC_DISK)"; exit 0; fi; \
	mkdir -p $(BUILD)/mac; \
	$(MAKE) --no-print-directory BUILD=$(BUILD)/mac/kernel-$(MAC_SHOT_FRAMES)-$(MAC_SHOT_SCALE) \
		EXTRA_CFLAGS="-DKERNEL_MAC_TEST -DKERNEL_MAC_TEST_FRAMES=$(MAC_SHOT_FRAMES) -DKERNEL_MAC_TEST_SCALE=$(MAC_SHOT_SCALE)" \
		$(BUILD)/mac/kernel-$(MAC_SHOT_FRAMES)-$(MAC_SHOT_SCALE)/kernel-gfx.elf || exit 1; \
	rm -rf $(BUILD)/mac/iso; mkdir -p $(BUILD)/mac/iso/boot/grub; \
	cp $(BUILD)/mac/kernel-$(MAC_SHOT_FRAMES)-$(MAC_SHOT_SCALE)/kernel-gfx.elf $(BUILD)/mac/iso/boot/; \
	cp "$(MAC_ROM)" $(BUILD)/mac/iso/mac_rom; cp "$(MAC_DISK)" $(BUILD)/mac/iso/mac_disk; \
	printf 'set default=0\nset timeout=0\nmenuentry "mac" {\n  multiboot /boot/kernel-gfx.elf\n  module /mac_rom mac_rom\n  module /mac_disk mac_disk\n  boot\n}\n' > $(BUILD)/mac/iso/boot/grub/grub.cfg; \
	grub-mkrescue -o $(BUILD)/mac/mac.iso $(BUILD)/mac/iso > /dev/null 2>&1

# Interactive: boot the normal graphics kernel with your ROM and disk as GRUB
# modules, in a QEMU window. Type `mac` + Enter in the window (the shell's
# output goes to this terminal), ` stops the Mac, Ctrl+Alt+G releases the
# mouse. The ISO holds copies of MAC_ROM and MAC_DISK; it stays in build/,
# which git ignores. Local only.
run-mac: $(BUILD)/kernel-gfx.elf
	@if [ ! -f "$(MAC_ROM)" ] || [ ! -f "$(MAC_DISK)" ]; then \
		echo "run-mac: no ROM or disk; set MAC_ROM and MAC_DISK"; exit 1; fi; \
	rm -rf $(BUILD)/mac/play; mkdir -p $(BUILD)/mac/play/boot/grub; \
	cp $(BUILD)/kernel-gfx.elf $(BUILD)/mac/play/boot/; \
	cp "$(MAC_ROM)" $(BUILD)/mac/play/mac_rom; cp "$(MAC_DISK)" $(BUILD)/mac/play/mac_disk; \
	printf 'set default=0\nset timeout=0\nmenuentry "mac" {\n  multiboot /boot/kernel-gfx.elf\n  module /mac_rom mac_rom\n  module /mac_disk mac_disk\n  boot\n}\n' > $(BUILD)/mac/play/boot/grub/grub.cfg; \
	grub-mkrescue -o $(BUILD)/mac/play.iso $(BUILD)/mac/play > /dev/null 2>&1 || exit 1; \
	accel=""; [ -w /dev/kvm ] && accel="-enable-kvm"; \
	echo "run-mac: type mac + Enter in the QEMU window; \` stops; Ctrl+Alt+G frees the mouse"; \
	$(QEMU) $$accel -m 256 -cdrom $(BUILD)/mac/play.iso -serial stdio

mac-screenshot: mac-iso
	@[ -f $(BUILD)/mac/mac.iso ] || exit 0; \
	python3 -I tools/mac_qemu_input.py $(BUILD)/mac --wait-crc $(BUILD)/mac/qemu_screen.ppm

# Types a key into the Mac running in the kernel under QEMU: 'T' selects the
# Trash in the Finder. Takes a picture before and after (build/mac/input_*.ppm).
mac-input-test:
	@if [ ! -f "$(MAC_ROM)" ] || [ ! -f "$(MAC_DISK)" ]; then \
		echo "mac-input-test: SKIP (no ROM or disk)"; exit 0; fi; \
	$(MAKE) --no-print-directory MAC_SHOT_FRAMES=14000 mac-iso || exit 1; \
	python3 -I tools/mac_qemu_input.py $(BUILD)/mac --input

# Keyboard modifiers on the Mac: Alt is the Command key, so Alt-N makes a new
# folder in the Finder (a QEMU screenshot before and after must differ). Local only.
test-mac-keys:
	@if [ ! -f "$(MAC_ROM)" ] || [ ! -f "$(MAC_DISK)" ]; then \
		echo "test-mac-keys: SKIP (no ROM or disk)"; exit 0; fi; \
	$(MAKE) --no-print-directory MAC_SHOT_FRAMES=40000 mac-iso || exit 1; \
	python3 -I tools/mac_qemu_input.py $(BUILD)/mac --command-n

# Disk images for the host tests, generated by our own scripts.
$(BUILD)/images/apm.img: tools/make_apm_image.py
	mkdir -p $(dir $@)
	python3 -I $< $@

# Needs hfsutils; the script gives it a temporary HOME (see the script).
$(BUILD)/images/hfs.img: tools/make_hfs_image.sh
	mkdir -p $(dir $@)
	tools/make_hfs_image.sh $@

# The HFS volume inside an Apple Partition Map, like a real Mac disk.
$(BUILD)/images/disk.img: $(BUILD)/images/hfs.img tools/make_disk_image.py
	python3 -I tools/make_disk_image.py $< $@

# The bytes of emu/mac/stub.s kept in stub_code.h must match the assembler.
test-mac-stub:
	python3 -I tools/gen_mac_stub.py --check

test-host: test-mac-stub $(HOST_TESTS) $(M68K_TEST_BINS) $(BUILD)/images/apm.img $(BUILD)/images/hfs.img $(BUILD)/images/disk.img
	@for t in $(HOST_TESTS); do ./$$t || exit 1; done

# SingleStepTests 68000 data (https://github.com/SingleStepTests/680x0, put
# the v1/*.json.gz files in tests/data/sst/). Not part of "make test": it
# needs the download and takes minutes. Prints the pass rate per instruction.
test-sst: $(BUILD)/host/sst_run
	@test -n "$$(ls tests/data/sst/*.json.gz 2>/dev/null)" || \
		{ echo "no data in tests/data/sst/"; exit 1; }
	@for f in tests/data/sst/*.json.gz; do \
		n=$$(basename $$f .json.gz); \
		python3 -I tools/sst_convert.py $$f | $(BUILD)/host/sst_run $$n || exit 1; \
	done

# ---------------------------------------------------------------------
# C SDK and user programs (docs/userland.md). These live in build/sdk and
# build/initrd whatever BUILD is.
# ---------------------------------------------------------------------

SDK_LIBC := build/sdk/sysroot/lib/librt68k.a
USERLAND_SOURCES := $(wildcard userland/*.c)
USERLAND_BINS := $(patsubst userland/%.c,build/initrd/bin/%,$(USERLAND_SOURCES)) build/initrd/bin/lua build/initrd/bin/tcc build/initrd/bin/sqlite3 build/initrd/bin/kilo build/initrd/bin/dash build/initrd/bin/make

# musl and the runtime helpers (downloads musl on the first run).
sdk: $(SDK_LIBC)

$(SDK_LIBC): tools/build_sdk.sh sdk/rt/divdi3.c $(wildcard sdk/include/linux/*.h)
	tools/build_sdk.sh

build/initrd/bin/%: userland/%.c $(SDK_LIBC) tools/68khal-cc
	mkdir -p $(dir $@)
	tools/68khal-cc -O2 -Wall -Wextra $< -o $@ -lm

# Lua, a real program ported with the SDK (downloads its source on the first run).
build/initrd/bin/lua: tools/build_lua.sh $(SDK_LIBC) tools/68khal-cc
	mkdir -p $(dir $@)
	tools/build_lua.sh $@

# Kilo, a terminal text editor: needs raw mode, escape sequences and the window size.
build/initrd/bin/make: tools/build_make.sh $(SDK_LIBC) tools/68khal-cc
	@mkdir -p $(dir $@)
	tools/build_make.sh $@

build/initrd/bin/dash: tools/build_dash.sh $(SDK_LIBC) tools/68khal-cc
	@mkdir -p $(dir $@)
	tools/build_dash.sh $@

build/initrd/bin/kilo: tools/build_kilo.sh $(SDK_LIBC) tools/68khal-cc
	mkdir -p $(dir $@)
	tools/build_kilo.sh $@

# SQLite, a database engine as a bigger test of the SDK (downloads its source on the first run).
build/initrd/bin/sqlite3: tools/build_sqlite.sh $(SDK_LIBC) tools/68khal-cc
	mkdir -p $(dir $@)
	tools/build_sqlite.sh $@

# The C compiler (tcc) with the headers and libraries it needs under usr/.
build/initrd/bin/tcc: tools/build_tcc.sh $(SDK_LIBC) tools/68khal-cc
	tools/build_tcc.sh build/initrd

# The program disk: bin/ (programs), data/ (files they read) and expected/
# (what the self test compares their output with), packed as a ustar archive.
initrd: $(INITRD)

$(INITRD): $(USERLAND_BINS) $(wildcard userland/data/*) $(wildcard userland/expected/*) $(wildcard userland/etc/*)
	rm -rf build/initrd/data build/initrd/expected build/initrd/etc
	mkdir -p build/initrd/data build/initrd/expected build/initrd/etc
	cp userland/etc/* build/initrd/etc/
	cp userland/data/* build/initrd/data/
	cp userland/expected/* build/initrd/expected/
	# The toolbox commands are hard links to bin/box (tarfs reads hard links), so that
	# /bin/ls, /bin/cp ... exist. A program of its own with the same name wins (userland/sort.c).
	for name in $$(grep -o '{"[a-z]*", box_' userland/box.c | grep -o '"[a-z]*"' | tr -d '"'); do \
		[ -e userland/$$name.c ] || { rm -f build/initrd/bin/$$name; ln build/initrd/bin/box build/initrd/bin/$$name; }; \
	done
	rm -f build/initrd/bin/sh; ln build/initrd/bin/dash build/initrd/bin/sh
	tar --format=ustar --owner=0 --group=0 --numeric-owner --sort=name \
		-C build/initrd -cf $@ bin data etc expected usr

# Kernel built with -DKERNEL_SELFTEST, booted directly by QEMU (no ISO).
# QEMU exits with 33 on PASS (see kernel/test/selftest.c).
# 4.5 GiB of RAM: the 64-bit kernel uses what is above 4 GiB (test_memory_above_4g); the 32-bit one ignores it.
QEMU_TEST_FLAGS := -display none -serial stdio -no-reboot -smp 4 -m 4608 \
	-device qemu-xhci,id=xhci -device usb-ehci -device piix3-usb-uhci \
	-drive id=usbdisk,file=$(BUILD)/test/usb_test.img,format=raw,if=none -device usb-storage,bus=xhci.0,drive=usbdisk \
	-device ich9-ahci,id=ahci -drive id=sata0,file=$(BUILD)/test/ahci_test.img,format=raw,if=none \
	-device ide-hd,drive=sata0,bus=ahci.0 \
	-drive id=nvm0,file=$(BUILD)/test/nvme_test.img,format=raw,if=none -device nvme,serial=nvme0,drive=nvm0 \
	-netdev user,id=n0 -device e1000,netdev=n0 \
	-audiodev wav,id=snd0,path=$(BUILD)/test/sound.wav \
	-device intel-hda -device hda-output,audiodev=snd0 \
	-device isa-debug-exit,iobase=0xf4,iosize=0x04

# Boot module for the selftest: -initrd "<file> <name>". Fixed content, so
# the selftest can check it (see platform/pc/test/selftest_pc.c).
$(BUILD)/test/module_test.txt: Makefile
	@mkdir -p $(dir $@)
	@printf 'selftest module\n' > $@
	@truncate -s 1024 $@

# IDE disk for the selftest: 300 sectors (more than the 255 one command
# reads), byte i of sector s is (s * 13 + i) mod 256.
# A 128-sector SATA disk for the AHCI test; made again every time because the
# test writes to it.
.PHONY: FORCE
$(BUILD)/test/ahci_test.img: FORCE
	@mkdir -p $(dir $@)
	@python3 -I -c "import sys; open(sys.argv[1], 'wb').write(bytes((s * 7 + i) & 255 for s in range(128) for i in range(512)))" $@

$(BUILD)/test/nvme_test.img: FORCE
	@mkdir -p $(dir $@)
	@python3 -I -c "import sys; open(sys.argv[1], 'wb').write(bytes((s * 11 + i) & 255 for s in range(512) for i in range(512)))" $@

$(BUILD)/test/usb_test.img: FORCE
	@mkdir -p $(dir $@)
	@python3 -I -c "import sys; open(sys.argv[1], 'wb').write(bytes((s * 5 + i) & 255 for s in range(128) for i in range(512)))" $@

$(BUILD)/test/ata_test.img: Makefile
	@mkdir -p $(dir $@)
	@python3 -I -c "import sys; open(sys.argv[1], 'wb').write(bytes((s * 13 + i) & 255 for s in range(300) for i in range(512)))" $@

test-qemu: $(BUILD)/test/module_test.txt $(BUILD)/images/disk.img $(BUILD)/test/ata_test.img $(BUILD)/test/ahci_test.img $(BUILD)/test/nvme_test.img $(BUILD)/test/usb_test.img $(INITRD)
	$(MAKE) --no-print-directory BUILD=$(BUILD)/test \
		EXTRA_CFLAGS=-DKERNEL_SELFTEST $(BUILD)/test/kernel.elf
	@timeout 60 $(QEMU) -kernel $(BUILD)/test/kernel.elf \
		-initrd "$(BUILD)/test/module_test.txt testmod,$(BUILD)/images/disk.img disk,$(INITRD) initrd" \
		-drive file=$(BUILD)/test/ata_test.img,format=raw,if=ide,index=0,media=disk $(QEMU_TEST_FLAGS); \
	status=$$?; \
	if [ $$status -eq 33 ]; then \
		echo "QEMU selftest: PASS"; \
		python3 -I tools/check_wav.py $(BUILD)/test/sound.wav 440 || exit 1; \
	else \
		echo "QEMU selftest: FAIL (exit status $$status)"; \
		exit 1; \
	fi

# The same selftest with "noapic" on the kernel command line: the 8259 delivers the interrupts
# (the safe mode of the real-hardware USB image). Run after test-qemu: it reuses its kernel.
test-noapic: test-qemu
	@timeout 60 $(QEMU) -kernel $(BUILD)/test/kernel.elf -append noapic \
		-initrd "$(BUILD)/test/module_test.txt testmod,$(BUILD)/images/disk.img disk,$(INITRD) initrd" \
		-drive file=$(BUILD)/test/ata_test.img,format=raw,if=ide,index=0,media=disk $(QEMU_TEST_FLAGS); \
	status=$$?; \
	if [ $$status -eq 33 ]; then \
		echo "QEMU selftest (noapic): PASS"; \
	else \
		echo "QEMU selftest (noapic): FAIL (exit status $$status)"; \
		exit 1; \
	fi

# Boot the ISO (GRUB, with the initrd module), type shell commands and
# check what the programs print on the serial port (tools/shell_test.py).
test-shell: iso
	QEMU_ARGS="-netdev user,id=n0 -device e1000,netdev=n0 -device qemu-xhci" \
	python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"lspci=Host bridge" \
		"usb=xHCI" \
		"net=MAC:" \
		"dhcp=Address: 10.0.2.15" \
		"ping 10.0.2.2=reply from 10.0.2.2" \
		"dns localhost=address: 127.0.0.1" \
		"cpuinfo=Features" \
		"acpi=Processors" \
		"acpi=Soft off:       ACPI S5" \
		"run /bin/hello=Hello from user mode!" \
		"run /bin/args a b=argv[2]: b" \
		"run /bin/crash=before" \
		"run /bin/lua -e print(6*7)=42" \
		"run /bin/tcc -static -o /tmp/h /data/tcc_hello.c=" \
		"run /tmp/h=compiled by tcc: fib(20)=6765" \
		"run /bin/readline=Type something" \
		"hello world=you typed: hello world (11 characters)" \
		"run /bin/readline=Type something" \
		"^D=end of input" \
		"run /bin/rawkeys=raw mode" \
		"a{up}{f1}{delete}q=97 27 91 65 27 79 80 27 91 51 126 113" \
		"run /bin/readline=Type something" \
		"ab{up}c=you typed: abc (3 characters)" \
		"run /bin/readline=Type something" \
		"{ctrl-c}=^C" \
		"run /bin/hello=Hello from user mode!" \
		"cd /nonexistent=no such directory" \
		"cd /tmp=" \
		"pwd=/tmp" \
		"mkdir box=" \
		"cp /data/message.txt box/m.txt=" \
		"touch box/empty.txt=" \
		"box ls box=m.txt" \
		"box cat box/m.txt=Second line." \
		"wc -l box/m.txt=m.txt" \
		"nosuchcommand=Unknown command: nosuchcommand" \
		"box free=Mem:" \
		"uptime=up 0:" \
		"box cat /proc/cpuinfo=vendor_id" \
		"seq 20 | grep 1 | wc -l=11" \
		"dmesg=Kernel Booting" \
		"ps=PID PPID PGID S NAME" \
		"irqs=Controller: IOAPIC" \
		"seq 1000 | grep 7 | wc -l=271" \
		"run /bin/forkexec=grandchild chain: 4" \
		"run /bin/dash /data/test.sh=job finished" \
		"run /bin/make -f /data/test.mk=make built and ran" \
		"run /bin/pipes=default SIGPIPE: killed 1 by signal 13" \
		"run /bin/signals=SIGKILL: signal 9" \
		"run /bin/hog=" \
		"{ctrl-c}=[exit 130]" \
		"sleep 30=" \
		"{ctrl-c}=[exit 130]" \
		"seq 3 | sleep 30=" \
		"{ctrl-c}=[exit 130]" \
		"seq 3 > nums.txt=" \
		"echo more >> nums.txt=" \
		"wc -l < nums.txt=4" \
		"cat < nums.txt=more" \
		"run /bin/helo{left}l=Hello from user mode!" \
		"{up}=Hello from user mode!" \
		"run /bin/hell{tab}=Hello from user mode!" \
		"mkd{tab}box2=" \
		"rmdir box2=" \
		"run /bin/evtest=reading events" \
		"{mouse_move 7 3}=rel 0 7" \
		"a{up}q=key 103 1" \
		"run /bin/kilo /tmp/note.txt=HELP:" \
		"hello kilo{ctrl-s}=bytes written on disk" \
		"{ctrl-q}{ctrl-q}{ctrl-q}{ctrl-q}=" \
		"run /bin/hello=Hello from user mode!"

# GRUB's graphics mode (640x480) must still show the test pattern: nothing the
# drivers do at start-up may switch the screen off (tools/fb_check.py).
# The same ISO started by UEFI firmware (OVMF, q35): Multiboot2, the ACPI RSDP
# from the boot loader, the framebuffer console. Needs the ovmf package.
test-uefi: iso $(BUILD)/kernel-gfx1080.elf
	BUILD=$(BUILD) python3 -I tools/uefi_check.py
	SHELL_TEST_UEFI=1 python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"acpi=Local APIC" "cpuinfo=Vendor" "lspci=Host bridge"

# The Q35 chipset (PCIe, ICH9, MSI-capable e1000e) with the BIOS: APIC interrupts and the network.
test-q35: iso
	QEMU_ARGS="-machine q35 -netdev user,id=n0 -device e1000,netdev=n0" \
	python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"cpuinfo=Vendor" "acpi=Local APIC" "lspci=Host bridge" \
		"dhcp=Address: 10.0.2.15" "ping 10.0.2.2=reply from 10.0.2.2"

# USB keyboard and mouse through xHCI, with the PS/2 controller switched off (the P520 has none):
# typing, the mouse, a keyboard plugged in while the system runs, and devices behind a hub.
test-usb: iso
	QEMU_ARGS="-machine q35,i8042=off -device qemu-xhci -device usb-kbd -device usb-mouse" \
	python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"cpuinfo=Vendor" "usb=keyboard" "usb=mouse" \
		"run /bin/evtest=reading events" "{mouse_move 7 3}=rel 0 7" "a{up}q=key 103 1" \
		"run /bin/hello=Hello from user mode"
	QEMU_ARGS="-machine q35,i8042=off -device qemu-xhci" \
	python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"{hmp device_add usb-kbd,id=k1}@@" "usb=keyboard" "cpuinfo=Vendor" \
		"{hmp device_add usb-mouse,id=m1}@@" "usb=mouse" \
		"{hmp device_del m1}@@" "cpuinfo=Vendor"
	QEMU_ARGS="-machine q35,i8042=off -device qemu-xhci,id=xhci -device usb-hub,bus=xhci.0,port=1 -device usb-kbd,bus=xhci.0,port=1.1" \
	python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"cpuinfo=Vendor" "usb=keyboard" \
		"{hmp device_add usb-mouse,bus=xhci.0,port=1.2,id=m2}@@" "usb=mouse" \
		"run /bin/evtest=reading events" "{mouse_move 7 3}=rel 0 7" "a{up}q=key 103 1"

# A second root bus (pxb-pcie is a host bridge of its own, like the extra root buses of the P520): the
# root port on bus 0x20 and the card behind it on bus 0x21 must be found (hal_pci.c scans every bus).
test-pci-roots: iso
	QEMU_ARGS="-machine q35 -device pxb-pcie,id=pcie.1,bus_nr=32,bus=pcie.0 -device pcie-root-port,id=rp1,bus=pcie.1,chassis=1,slot=1 -device e1000e,bus=rp1" \
	python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso "lspci=20:00.0" "lspci=21:00.0"

# The e1000e card (82574L): DHCP, ping, DNS, with the Q35 chipset.
test-e1000e: iso
	QEMU_ARGS="-machine q35 -netdev user,id=n0 -device e1000e,netdev=n0" \
	python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"net=Card:     e1000e" "dhcp=Address: 10.0.2.15" \
		"ping 10.0.2.2=reply from 10.0.2.2" "dns localhost=address: 127.0.0.1"

# poweroff and reboot end QEMU (i440fx, Q35, UEFI); QEMU only.
test-power: iso
	python3 -I tools/power_test.py $(BUILD)/68k_HAL.iso

test-fb: all
	python3 -I tools/fb_check.py $(BUILD)/kernel-gfx.elf

# The standard VGA card's mode can be chosen: set 1280x960 from the shell and
# check a QEMU screendump (in graphics mode, GRUB's 640x480, and in text mode).
test-video: iso
	SHELL_TEST_SCREEN=1280x960 python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"video 1280 960=video: 1280x960 32 bpp"

# TCP end to end: a web server on the host (loopback), the OS fetches a page
# from it through QEMU's user-mode network, where 10.0.2.2 is the host.
test-net: iso
	@rm -rf $(BUILD)/httproot; mkdir -p $(BUILD)/httproot; \
	printf 'HELLO-FROM-THE-HOST-%s\n' 0123456789abcdef > $(BUILD)/httproot/page.txt; \
	port=$$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'); \
	python3 -m http.server $$port --bind 127.0.0.1 --directory $(BUILD)/httproot > $(BUILD)/httproot/server.log 2>&1 & \
	server=$$!; sleep 1; \
	QEMU_ARGS="-netdev user,id=n0 -device e1000,netdev=n0" \
	python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"dhcp=Address: 10.0.2.15" \
		"http 10.0.2.2 $$port /page.txt=HELLO-FROM-THE-HOST-0123456789abcdef" \
		"run /bin/httpget 10.0.2.2 $$port /page.txt=status: 200 OK" \
		"run /bin/dnsquery=localhost is 127.0.0.1"; \
	status=$$?; kill $$server 2>/dev/null; exit $$status

# The same network checks with a virtio-net card instead of the e1000.
test-virtio-net: iso
	QEMU_ARGS="-netdev user,id=n0 -device virtio-net-pci,netdev=n0" \
	python3 -I tools/shell_test.py $(BUILD)/68k_HAL.iso \
		"net=Card:     virtio-net" \
		"dhcp=Address: 10.0.2.15" \
		"ping 10.0.2.2=reply from 10.0.2.2" \
		"dns localhost=address: 127.0.0.1"

# The source tree must pass every rule. Each tests/boundary/<rule>/ is a
# fixture tree that breaks that rule, so the check must report <rule>.
test-boundary:
	@tools/check_boundary.sh
	@for fixture in tests/boundary/*/; do \
		rule=$$(basename $$fixture); \
		if ! tools/check_boundary.sh $$fixture | grep -q "^boundary: $$rule: "; then \
			echo "boundary check: FAIL ($$rule not reported for $$fixture)"; \
			exit 1; \
		fi; \
	done
	@echo "boundary check: PASS"

test: test-boundary test-host test-qemu test-noapic test-q35 test-usb test-e1000e test-shell test-video test-fb test-net test-virtio-net test-pci-roots

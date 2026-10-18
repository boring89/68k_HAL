# 68k Hardware Abstraction Layer Project

 A modern Hardware Abstraction Layer designed to bring the classic Macintosh System experience to modern x86 hardware.

> **Project Status:** Early Development

---

## Overview

68k HAL is an experimental operating system project that explores an alternative evolution of the classic Macintosh operating system.

Instead of emulating old Macintosh hardware, this project aims to build a modern Hardware Abstraction Layer (HAL) that allows the architecture and philosophy of Classic Mac OS to evolve on contemporary hardware.

The long-term vision is simple:

> **What if Classic Mac OS had continued to evolve instead of being replaced?**

---

## Features

### Current

- x86 Protected Mode boot
- GRUB Multiboot support
- VGA text console
- Interrupt handling (IDT / PIC)
- Basic keyboard input
- Modular HAL architecture
- 68k virtual machine (Musashi) and a Macintosh IIci machine model that boots a user-supplied ROM and System 7.5.3 disk image to the Finder desktop, with keyboard, mouse, sound (Apple Sound Chip), a real-time clock, a virtual NuBus video card (640x480 to 3840x2160, up to millions of colors, switched in the Monitors control panel) shown enlarged in half steps (HiDPI), and writes to an ATA disk (experimental, see [docs/mac.md](docs/mac.md) and [docs/nubus_video.md](docs/nubus_video.md))
- A disk of its own for the Mac: `make mac-disk` builds a UEFI GPT image that boots straight into the Mac desktop, with the original System image never modified (the Mac's writes go to a power-fail-safe overlay that `macsys reset` drops), an Applications volume, two kernel and two System slots, a settings file, and Shut Down / Restart / the power button mapped to ACPI ([docs/mac_disk.md](docs/mac_disk.md))
- CPU: CPUID, x87/SSE, TSC nanosecond clock, ACPI tables, starting the other processors ([docs/cpu.md](docs/cpu.md))
- Ring 3 user mode running static Linux i386 programs, with a C SDK (musl) so that C programs can be ported; Lua, SQLite, the Kilo editor, the Tiny C Compiler, dash and GNU make already run, and the OS can compile C itself; there is a terminal (ANSI, termios), a toolbox of shell commands with pipes and redirection, `/proc`, `/dev/fb0` and Linux input events ([docs/userland.md](docs/userland.md)). The text console also works in graphics mode
- Virtual file tree (initrd, RAM disk `/tmp`, `/dev`, the Mac disk's HFS volume at `/hfs`)
- PCI bus, driver framework and DMA memory ([docs/pci.md](docs/pci.md))
- Networking: Intel e1000, e1000e/I219 and virtio-net drivers, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS, BSD sockets for programs ([docs/net.md](docs/net.md))
- Storage: ATA (read/write), AHCI/SATA and NVMe drivers ([docs/storage.md](docs/storage.md))
- USB: xHCI host controller with HID keyboards and mice, hot plug ([docs/usb.md](docs/usb.md)); the other controllers are listed
- Multitasking: processes with their own address spaces, `fork`/`execve`/`wait`, pipes, signals, scheduling (cooperative in the kernel, preemptive for user code) ([docs/process.md](docs/process.md))
- Interrupts through the local APIC and I/O APIC, PCI MSI ([docs/apic.md](docs/apic.md)); Multiboot2 and UEFI boot (2560x1440 requested at boot); screen modes through the video BIOS's VBE run in an emulated CPU (x86emu) on BIOS boots ([docs/gpu.md](docs/gpu.md)); big-screen console
- Real hardware: a USB image for the Lenovo ThinkStation P520 with safe mode and diagnostics ([docs/p520.md](docs/p520.md))
- Audio: Intel HD Audio driver and a streaming sound interface ([docs/sound.md](docs/sound.md))
- Graphics: Bochs/QEMU VGA mode setting at any resolution ([docs/gpu.md](docs/gpu.md))

### Planned

- 68k compatibility layer
- Preemptive multitasking, copy-on-write fork, threads
- Modern filesystem (writable, on disk)
- USB hubs and mass storage, native GPUs
- Using the other processors (SMP scheduling)

---

## Vision

68k HAL is **not** an emulator or a Macintosh clone.

The goal is to preserve the simplicity, usability, and software philosophy of the Classic Macintosh while replacing the underlying kernel, hardware abstraction, and drivers with modern implementations.

---

## Building

### Requirements

| Tool | Debian/Ubuntu package | Used for |
|---|---|---|
| `make`, `gcc` | `build-essential` | Build the kernel (64-bit by default; `make ARCH=i386` builds the 32-bit kernel with `-m32`) and host unit tests |
| `nasm` | `nasm` | Assemble boot and interrupt stubs |
| `ld` | `binutils` | Link the kernel ELF |
| `m68k-linux-gnu-as` | `binutils-m68k-linux-gnu` | Assemble the 68k test programs in `tests/m68k/` |
| `curl`, `tar`, `sha256sum` | `curl`, `tar`, `coreutils` | Download and check the sources of musl, Lua and TinyCC (`third_party/downloads/`, not in the repository) |
| `grub-mkrescue` | `grub2-common` | Create the bootable ISO |
| GRUB BIOS modules | `grub-pc-bin` | Make the ISO bootable on BIOS (QEMU's default firmware) |
| `xorriso` | `xorriso` | Used by `grub-mkrescue` to write the ISO |
| `mformat` | `mtools` | Used by `grub-mkrescue` to build the EFI image |
| `qemu-system-i386` | `qemu-system-x86` | Run the kernel and the in-kernel self tests |
| OVMF (UEFI firmware for QEMU) | `ovmf` | Only for `make test-uefi` and `make test-usb-image` (not part of `make test`) |
| GRUB UEFI modules | `grub-efi-amd64-bin` | The UEFI half of the ISO and the USB image (`grub-mkrescue` adds it when present) |
| `python3` | `python3` | Generate the test disk images (`tools/make_*_image.py`); also the optional `tools/boot_log.py` and `tools/fb_check.py` |
| `sgdisk` | `gdisk` | Partition the 68k_HAL disk image (`tools/make_mac_disk.py`, `make test-update-script`); `grub-editenv` (`grub2-common`) and `mtools` are used there too |
| `hformat`, `hcopy`, ... | `hfsutils` | Build the HFS test volume (`tools/make_hfs_image.sh`). It keeps its "current volume" in `~/.hcwd`; the script uses a temporary `HOME` so yours is untouched |

```bash
sudo apt install build-essential nasm binutils binutils-m68k-linux-gnu grub2-common grub-pc-bin xorriso mtools qemu-system-x86 python3 hfsutils curl gdisk
```

### Commands

```bash
make            # build/kernel.elf (64-bit kernel; make ARCH=i386 builds the 32-bit one into build/i386, see docs/x86_64.md)
make iso        # build/68k_HAL.iso (BIOS and UEFI)
make usb-image  # build/68k_HAL_usb.img for a USB stick, with a boot menu (see docs/p520.md)
make run        # boot the ISO in QEMU, serial output on stdio
make test       # boundary check, host unit tests, QEMU self tests, shell and video tests
make sdk        # the C SDK (musl) in build/sdk; downloads musl on the first run
make initrd     # the example programs, Lua and tcc, packed as build/initrd.tar
make clean
```

### Running a Macintosh (needs your own files)

The repository contains no Apple software. If you own a Macintosh IIci ROM
image (checksum `368CADFE`, 512 KiB) and a raw HFS disk image with System 7.5.3,
you can boot them on the host or inside the kernel:

```bash
make build/host/mac_run_fast
build/host/mac_run_fast -n 500000000 -d System7_5_3.img -p screen.ppm "Mac IIci.ROM"

# inside the kernel under QEMU, in a window you can use; type `mac` + Enter
make run-mac MAC_ROM="Mac IIci.ROM" MAC_DISK=System7_5_3.img

# inside the kernel under QEMU (graphics mode); the paths are Makefile variables
make mac-screenshot MAC_ROM="Mac IIci.ROM" MAC_DISK=System7_5_3.img
make test-rom MAC_ROM=...        # boots the ROM on the host (skipped without a ROM)
make test-rom-qemu MAC_ROM=... MAC_DISK=...   # kernel and host must draw the same screen
```

The Mac's screen is a virtual NuBus video card: `mac` picks a resolution that
fills your screen (2560x1440: 1280x720 enlarged 2x); `mac res=1024x768
scale=1.5` chooses yourself (`depth=32` starts in millions of colors,
`cards=2` adds a second screen), Ctrl+Alt+PgUp/PgDn/Home change the enlargement
while it runs, and the Monitors control panel switches depth and resolution
([docs/nubus_video.md](docs/nubus_video.md)). Details, the list of ROM
patches and the known limits (no serial or network yet) are in
[docs/mac.md](docs/mac.md). More local tests: `make test-mac-sound`,
`make test-mac-clock`, `make test-mac-persist`, `make test-mac-perf`,
`make mac-uefi-shot`.

A whole disk for the Mac (UEFI; boots straight into the desktop):

```bash
make mac-disk MAC_ROM="Mac IIci.ROM" MAC_DISK=System7_5_3.img   # build/68k_HAL_mac.img
make test-mac-disk MAC_ROM="Mac IIci.ROM" MAC_DISK=System7_5_3.img
```

Writing it to a real disk is up to you: [docs/p520.md](docs/p520.md) section 7.
The layout, the overlay and the settings file `68k_hal.cfg` are described in
[docs/mac_disk.md](docs/mac_disk.md).

### Porting a C program

```bash
make sdk
tools/68khal-cc -O2 hello.c -o hello    # a static i386 Linux program using musl
# put it in userland/, then
make run                                # HAL> run /bin/hello
```

See [docs/userland.md](docs/userland.md) for the system calls that exist and
how to add one.

---

## Contributing

Contributions are welcome!

If you would like to contribute, please open an **Issue** first to discuss major architectural changes before submitting a **Pull Request**.

Coding guidelines and contribution documentation will be added as the project matures.

---

## License

This project is licensed under the **GNU General Public License v3.0**.

See the [LICENSE](LICENSE) file for details.

---

## Disclaimer

68k HAL is an independent open-source project.

It is **not affiliated with or endorsed by Apple Inc.**

This repository does **not** contain or distribute any Apple ROMs, System Software, or other copyrighted materials.

---

## Author

**YuanChi Hsieh**

GitHub: **[@boring89](https://github.com/boring89)**

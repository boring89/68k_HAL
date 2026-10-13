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

### Planned

- 68k compatibility layer
- Modern memory management
- Modern filesystem
- Graphics subsystem
- USB support
- Networking
- Audio subsystem
- Modern driver framework

---

## Vision

68k HAL is **not** an emulator or a Macintosh clone.

The goal is to preserve the simplicity, usability, and software philosophy of the Classic Macintosh while replacing the underlying kernel, hardware abstraction, and drivers with modern implementations.

---

## Building

### Requirements

| Tool | Debian/Ubuntu package | Used for |
|---|---|---|
| `make`, `gcc` | `build-essential` | Build the kernel (`-m32`) and host unit tests |
| `nasm` | `nasm` | Assemble boot and interrupt stubs |
| `ld` | `binutils` | Link the kernel ELF |
| `grub-mkrescue` | `grub2-common` | Create the bootable ISO |
| GRUB BIOS modules | `grub-pc-bin` | Make the ISO bootable on BIOS (QEMU's default firmware) |
| `xorriso` | `xorriso` | Used by `grub-mkrescue` to write the ISO |
| `mformat` | `mtools` | Used by `grub-mkrescue` to build the EFI image |
| `qemu-system-i386` | `qemu-system-x86` | Run the kernel and the in-kernel self tests |

```bash
sudo apt install build-essential nasm binutils grub2-common grub-pc-bin xorriso mtools qemu-system-x86
```

### Commands

```bash
make            # build/kernel.elf
make iso        # build/68k_HAL.iso
make run        # boot the ISO in QEMU, serial output on stdio
make test       # host unit tests + QEMU self tests
make clean
```

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

# 68k Hardware Abstraction Layer Project

> **Archived.** 68k_HAL ended with **v1.0** (the final release). It is kept here as the record of
> the transitional phase; development continues in a new project, **STELA**, which does not
> depend on any Apple component and keeps the Macintosh only as an optional Classic environment.
> This repository is read-only: no further changes, issues or pull requests.

68k_HAL is an experimental operating system kernel for x86-64 PCs that runs a Macintosh IIci
(a 68030 emulated with Musashi) from the user's own ROM and System 7.5.3 disk image. At v1.0 a
Lenovo ThinkStation P520 boots from its own disk straight into the Mac desktop at 2560x1440, with
the Mac's changes kept in a power-fail-safe overlay over a never-modified System image, a
journaled file system of its own (eHFS) for data, and updates that do not touch what the Mac wrote.

---

## Features (v1.0)

- x86-64 long mode kernel (the default; user programs run in i386 compatibility mode) and a 32-bit protected mode build, `make ARCH=i386` ([docs/x86_64.md](docs/x86_64.md))
- GRUB Multiboot and Multiboot2 support
- VGA text console
- Interrupt handling (IDT; 8259 PIC or local APIC and I/O APIC)
- Keyboard and mouse input (PS/2 and USB)
- Modular HAL architecture ([docs/hal.md](docs/hal.md))
- 68k virtual machine (Musashi) and a Macintosh IIci machine model that boots a user-supplied ROM and System 7.5.3 disk image to the Finder desktop, with keyboard, mouse, sound (Apple Sound Chip), a real-time clock, a virtual NuBus video card (640x480 to 3840x2160, up to millions of colors, switched in the Monitors control panel) shown enlarged in half steps (HiDPI), and writes to an ATA disk (experimental, see [docs/mac.md](docs/mac.md) and [docs/nubus_video.md](docs/nubus_video.md); measurements for a true 2x HiDPI screen in [docs/hidpi.md](docs/hidpi.md))
- A disk of its own for the Mac: `make mac-disk` builds a UEFI GPT image that boots straight into the Mac desktop, with the original System image never modified (the Mac's writes go to a power-fail-safe overlay that `macsys reset` drops), an Applications volume, two kernel and two System slots, a settings file, the Mac's parameter RAM kept between starts, and Shut Down / Restart / the power button mapped to ACPI ([docs/mac_disk.md](docs/mac_disk.md))
- eHFS, a file system of its own: journaled metadata, B-trees, Unicode names (NFC, compared without regard to case), resource forks and Finder information, extended attributes; mounted read/write at `/ehfs`, with Linux tools `mkfs.ehfs`, `fsck.ehfs` and `ehfs-cp` ([docs/ehfs.md](docs/ehfs.md)). A data partition of the 68k_HAL disk holds one (`tools/add_data_partition.sh`)
- An external file system for the Mac (System 7.5's File System Manager): the HAL's files appear on the Mac's desktop as a read-only volume "HAL", and the eHFS volume as a volume "Data" the Finder and applications write to; names are converted between UTF-8 and Mac Roman ([docs/fsm.md](docs/fsm.md))
- CPU: CPUID, x87/SSE, TSC nanosecond clock, ACPI tables, starting the other processors ([docs/cpu.md](docs/cpu.md))
- Ring 3 user mode running static Linux i386 programs, with a C SDK (musl) so that C programs can be ported; Lua, SQLite, the Kilo editor, the Tiny C Compiler, dash and GNU make already run, and the OS can compile C itself; there is a terminal (ANSI, termios), a toolbox of shell commands with pipes and redirection, `/proc`, `/dev/fb0` and Linux input events ([docs/userland.md](docs/userland.md)). The text console also works in graphics mode
- Virtual file tree (initrd, RAM disk `/tmp`, `/dev`, the Mac disk's HFS volume at `/hfs`, the eHFS volume at `/ehfs`)
- PCI bus, driver framework and DMA memory ([docs/pci.md](docs/pci.md))
- Networking: Intel e1000, e1000e/I219 and virtio-net drivers, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS, BSD sockets for programs ([docs/net.md](docs/net.md))
- Storage: ATA (read/write), AHCI/SATA and NVMe drivers ([docs/storage.md](docs/storage.md))
- USB: xHCI host controller with HID keyboards and mice, hubs, USB sticks (mass storage) and hot plug ([docs/usb.md](docs/usb.md)); the other controllers are listed
- Multitasking: processes with their own address spaces, `fork`/`execve`/`wait`, pipes, signals, scheduling (cooperative in the kernel, preemptive for user code) ([docs/process.md](docs/process.md))
- Interrupts through the local APIC and I/O APIC, PCI MSI ([docs/apic.md](docs/apic.md)); Multiboot2 and UEFI boot (2560x1440 requested at boot); screen modes through the video BIOS's VBE run in an emulated CPU (x86emu) on BIOS boots ([docs/gpu.md](docs/gpu.md)); big-screen console
- Real hardware: a USB image for the Lenovo ThinkStation P520 with safe mode and diagnostics ([docs/p520.md](docs/p520.md))
- Audio: Intel HD Audio driver and a streaming sound interface ([docs/sound.md](docs/sound.md))
- Graphics: Bochs/QEMU VGA mode setting at any resolution ([docs/gpu.md](docs/gpu.md))

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
| `qemu-system-x86_64`, `qemu-system-i386` | `qemu-system-x86` | Run the kernel (64-bit and 32-bit builds) and the in-kernel self tests |
| OVMF (UEFI firmware for QEMU) | `ovmf` | Only for `make test-uefi`, `make test-usb-image` and the local Mac disk tests (`make test-mac-disk`, `make test-mac-update`, `make mac-uefi-shot`); not part of `make test` |
| GRUB UEFI modules | `grub-efi-amd64-bin` | The UEFI half of the ISO and the USB image (`grub-mkrescue` adds it when present) |
| `python3` | `python3` | Generate the test disk images (`tools/make_*_image.py`); also the optional `tools/boot_log.py` and `tools/fb_check.py` |
| `sgdisk` | `gdisk` | Partition the 68k_HAL disk image (`tools/make_mac_disk.py`, `make test-update-script`, `make test-data-partition`, `make test-ehfs-qemu`); `grub-editenv` (`grub2-common`) and `mtools` are used there too |
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
make test       # boundary check, host unit tests, QEMU self tests, shell, video, disk and eHFS tests
make sdk        # the C SDK (musl) in build/sdk; downloads musl on the first run
make initrd     # the example programs, Lua and tcc, packed as build/initrd.tar
make ehfs-tools # mkfs.ehfs, fsck.ehfs, ehfs-cp in build/host/ehfs (eHFS images and partitions)
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
[docs/mac.md](docs/mac.md). The host runner shows a folder to the Mac as the
read-only volume "HAL" with `-A <folder>` and an eHFS image as the writable
volume "Data" with `-e <image>` ([docs/fsm.md](docs/fsm.md)). More local tests:
`make test-mac-sound`, `make test-mac-clock`, `make test-mac-persist`,
`make test-mac-perf`, `make test-mac-keys`, `make test-mac-fsm`,
`make mac-hidpi-measure`, `make mac-uefi-shot`.

A whole disk for the Mac (UEFI; boots straight into the desktop):

```bash
make mac-disk MAC_ROM="Mac IIci.ROM" MAC_DISK=System7_5_3.img   # build/68k_HAL_mac.img
make test-mac-disk MAC_ROM="Mac IIci.ROM" MAC_DISK=System7_5_3.img
make test-mac-update MAC_ROM="Mac IIci.ROM" MAC_DISK=System7_5_3.img  # updates with tools/update_68khal.sh
```

Writing it to a real disk is up to you: [docs/p520.md](docs/p520.md) section 7.
Afterwards `sudo tools/add_data_partition.sh /dev/sdX` turns the free space into
the eHFS data partition, the Mac's "Data" volume ([docs/mac_disk.md](docs/mac_disk.md) section 8.5).
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

The project is archived and does not take contributions. See STELA for the work that continues.

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

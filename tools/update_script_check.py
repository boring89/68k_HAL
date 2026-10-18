#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make test-update-script: tools/update_68khal.sh against a small synthetic 68k_HAL disk IMAGE (no Mac ROM or
# System needed; build/images/hfs.img stands in for the System). Checks that a dry run writes nothing, that
# an update writes the spare kernel slot and sets the trial, that --system writes only the spare System slot,
# that the overlay, Applications, the other slots and the free space stay byte for byte as they were, and
# that targets which are not 68k_HAL disks are refused. Never touches a device.
#
# Usage: tools/update_script_check.py <work dir> <stand-in system image>

import hashlib
import os
import shutil
import subprocess
import sys

MIB = 1024 * 1024


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def partitions(image):
    """name -> (first byte, length) from sgdisk -p."""
    out = run(["sgdisk", "-p", image]).stdout
    result = {}
    for line in out.splitlines():
        f = line.split()
        if f and f[0].isdigit() and len(f) >= 7:
            first, last = int(f[1]), int(f[2])
            result[" ".join(f[6:])] = (first * 512, (last - first + 1) * 512)
    return result


def digest(image, offset, length):
    h = hashlib.sha256()
    with open(image, "rb") as f:
        f.seek(offset)
        h.update(f.read(length))
    return h.hexdigest()


def regions(image):
    """Hashes of every partition and of the space outside them."""
    parts = partitions(image)
    hashes = {name: digest(image, off, size) for name, (off, size) in parts.items()}
    end = max(off + size for off, size in parts.values())
    start = min(off for off, _ in parts.values())
    hashes["(free space)"] = digest(image, end, os.path.getsize(image) - end)
    hashes["(partition table)"] = digest(image, 0, start)
    return hashes


def esp_file(image, offset, path, out):
    r = run(["mcopy", "-n", "-i", f"{image}@@{offset}", f"::{path}", out])
    return r.returncode == 0


def env(image, offset, work):
    path = os.path.join(work, "env")
    esp_file(image, offset, "68k_hal/grubenv", path)
    out = run(["grub-editenv", path, "list"]).stdout
    return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)


def main():
    work, system = sys.argv[1:3]
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "update_68khal.sh")
    failures = []

    def check(cond, what):
        print(f"update_script_check: {'ok' if cond else 'FAIL'}: {what}")
        if not cond:
            failures.append(what)

    shutil.rmtree(work, ignore_errors=True)
    esp = os.path.join(work, "esp", "68k_hal")
    for slot in ("slot_a", "slot_b"):
        os.makedirs(os.path.join(esp, slot))
        for name in ("kernel-gfx.elf", "kernel-gfx1080.elf", "kernel-gfx1440.elf", "initrd.tar"):
            with open(os.path.join(esp, slot, name), "w") as f:
                f.write(f"old {name}\n")
    subprocess.run(["grub-editenv", os.path.join(esp, "grubenv"), "create"], check=True)
    subprocess.run(["grub-editenv", os.path.join(esp, "grubenv"), "set", "boot_slot=a", "system_slot=a",
                    "trial="], check=True)

    new_kernel = os.path.join(work, "new")
    os.makedirs(new_kernel)
    for name in ("kernel-gfx.elf", "kernel-gfx1080.elf", "kernel-gfx1440.elf", "initrd.tar"):
        with open(os.path.join(new_kernel, name), "w") as f:
            f.write(f"new {name}\n" * 100)

    image = os.path.join(work, "disk.img")
    subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(script), "make_mac_disk.py"),
                    "--out", image, "--system", system, "--esp-dir", os.path.join(work, "esp"),
                    "--boot-mib", "40", "--system-mib", "2", "--overlay-mib", "2", "--apps-mib", "2"],
                   check=True)
    # Something in the overlay and the free space, so that a stray write would show.
    parts = partitions(image)
    with open(image, "r+b") as f:
        f.seek(parts["68k_HAL Overlay"][0])
        f.write(b"OVERLAY CONTENT" * 100)
    boot = parts["68k_HAL Boot"][0]
    before = regions(image)
    whole = digest(image, 0, os.path.getsize(image))

    # A dry run writes nothing.
    r = run([script, "--dry-run", "--kernel-dir", new_kernel, image])
    check(r.returncode == 0 and "dry run" in r.stdout, "dry run succeeds")
    check(digest(image, 0, os.path.getsize(image)) == whole, "dry run leaves the image as it was")

    # The kernel update: slot b, trial=b; nothing else changes but the Boot partition.
    r = run([script, "--kernel-dir", new_kernel, image])
    check(r.returncode == 0, "kernel update succeeds: " + r.stderr.strip())
    got = os.path.join(work, "got")
    check(esp_file(image, boot, "68k_hal/slot_b/kernel-gfx1440.elf", got)
          and open(got).read() == open(os.path.join(new_kernel, "kernel-gfx1440.elf")).read(),
          "slot_b holds the new kernel")
    check(esp_file(image, boot, "68k_hal/slot_a/kernel-gfx1440.elf", got)
          and open(got).read() == "old kernel-gfx1440.elf\n", "slot_a still holds the old kernel")
    e = env(image, boot, work)
    check(e.get("trial") == "b" and e.get("boot_slot") == "a", "grubenv: trial=b, boot_slot=a")
    after = regions(image)
    for name in before:
        if name != "68k_HAL Boot":
            check(after[name] == before[name], f"{name} unchanged by the kernel update")

    # The System update goes to the spare slot B only.
    new_system = os.path.join(work, "new_system.img")
    with open(new_system, "wb") as f:
        f.write(b"NEW SYSTEM IMAGE" * 4096)
    before = regions(image)
    r = run([script, "--no-kernel", "--system", new_system, image])
    check(r.returncode == 0 and "macsys switch b force" in r.stdout, "system update succeeds")
    off, _ = partitions(image)["68k_HAL System B"]
    check(digest(image, off, os.path.getsize(new_system)) == digest(new_system, 0, os.path.getsize(new_system)),
          "System B holds the new image")
    after = regions(image)
    for name in before:
        if name != "68k_HAL System B":
            check(after[name] == before[name], f"{name} unchanged by the system update")

    # Too big for the slot, not a 68k_HAL disk, not a file or a disk: refused, nothing written.
    big = os.path.join(work, "big.img")
    with open(big, "wb") as f:
        f.truncate(3 * MIB)
    whole = digest(image, 0, os.path.getsize(image))
    check(run([script, "--no-kernel", "--system", big, image]).returncode != 0, "an image too big is refused")
    check(digest(image, 0, os.path.getsize(image)) == whole, "and nothing is written")
    copy = os.path.join(work, "plain.img")
    with open(copy, "wb") as f:
        f.truncate(4 * MIB)
    subprocess.run(["sgdisk", "-o", "-n", "1:2048:6143", "-c", "1:Some data", copy], check=True,
                   stdout=subprocess.DEVNULL)
    r = run([script, "--kernel-dir", new_kernel, copy])
    check(r.returncode != 0 and "not a 68k_HAL disk" in r.stderr, "a GPT disk without the layout is refused")
    check(run([script, "--dry-run", "/dev/null"]).returncode != 0, "a character device is refused")

    if failures:
        sys.exit(f"update_script_check: {len(failures)} failed")
    print("update_script_check: PASS")


main()

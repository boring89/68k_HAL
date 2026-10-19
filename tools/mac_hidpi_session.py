#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A scripted Mac session for the HiDPI measurements (make mac-hidpi-measure, docs/hidpi.md): prints the
# tools/mac_run arguments (-J pointer positions, -K keys) of what a user typically does in System 7.5.3:
# open the Control Panels window and list it by name, open the Keyboard control panel and click a
# setting, drag a window, scroll, pull down a menu, open SimpleText and type a few lines, quit without
# saving, move the pointer about. Run with -C 640x480x32 (the positions were found on screenshots of
# that screen) and -F FRAMES (printed by --frames).
#
# Usage: tools/mac_hidpi_session.py [--until FRAME] [--frames]

import sys

# ADB key codes (Apple Extended Keyboard; the same table as kernel/mac/adbkeys.c).
ADB = {
    "a": 0x00, "s": 0x01, "d": 0x02, "f": 0x03, "h": 0x04, "g": 0x05, "z": 0x06, "x": 0x07, "c": 0x08,
    "v": 0x09, "b": 0x0B, "q": 0x0C, "w": 0x0D, "e": 0x0E, "r": 0x0F, "y": 0x10, "t": 0x11, "o": 0x1F,
    "u": 0x20, "i": 0x22, "p": 0x23, "l": 0x25, "j": 0x26, "k": 0x28, "n": 0x2D, "m": 0x2E, ".": 0x2F,
    ",": 0x2B, " ": 0x31, "\n": 0x24,
    "0": 0x1D, "1": 0x12, "2": 0x13, "3": 0x14, "4": 0x15, "5": 0x17, "6": 0x16, "7": 0x1A, "8": 0x1C,
    "9": 0x19,
}
CMD = 0x37
SHIFT = 0x38

args = []
frame = 0


def at(f):
    global frame
    frame = f


def wait(n):
    global frame
    frame += n


def point(x, y, down):
    args.extend(["-J", f"{x},{y},{1 if down else 0},{frame}"])


def click(x, y, double=False):
    point(x, y, True)
    wait(3)
    point(x, y, False)
    wait(3)
    if double:
        point(x, y, True)
        wait(3)
        point(x, y, False)
        wait(3)


def drag(x0, y0, x1, y1, steps):
    point(x0, y0, True)
    wait(4)
    for i in range(1, steps + 1):
        point(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps, True)
        wait(2)
    point(x1, y1, False)
    wait(4)


def key(code, down):
    args.extend(["-K", f"{code},{1 if down else 0},{frame}"])


def typed(text):
    for ch in text:
        shifted = ch.isupper()
        if shifted:
            key(SHIFT, True)
        key(ADB[ch.lower()], True)
        wait(3)
        key(ADB[ch.lower()], False)
        if shifted:
            key(SHIFT, False)
        wait(3)


def command(ch):
    key(CMD, True)
    wait(2)
    key(ADB[ch], True)
    wait(3)
    key(ADB[ch], False)
    wait(2)
    key(CMD, False)
    wait(3)


def session():
    # The Finder is up at about frame 1100.
    at(1200)
    click(20, 9)                                # Apple menu
    point(20, 9, True)
    wait(15)
    point(70, 60, True)                         # Control Panels
    wait(15)
    point(70, 60, False)
    at(2000)
    point(130, 9, True)                         # View
    wait(15)
    point(150, 59, True)                        # by Name
    wait(15)
    point(150, 59, False)
    at(2100)
    typed("ke")                                 # Keyboard
    command("o")
    at(2500)
    click(263, 88)                              # Delay Until Repeat: Long
    wait(30)
    command("w")
    at(2700)
    drag(200, 37, 300, 150, 30)                 # the Control Panels window, by its title bar
    at(2900)
    for _ in range(6):                          # scroll its list down
        click(505, 324)
        wait(6)
    at(3100)
    point(55, 9, True)                          # File menu held, then along the menu bar (each menu
    wait(30)                                    # opens in turn), let go below them: nothing chosen
    for x in (90, 130, 175, 232):
        point(x, 9, True)
        wait(20)
    point(300, 420, True)
    wait(10)
    point(300, 420, False)
    at(3300)
    click(598, 45, double=True)                 # the system disk's window
    at(3600)
    typed("si")                                 # SimpleText
    command("o")
    at(4100)
    typed("The quick brown fox jumps over the lazy dog.\n")
    typed("Mac OS on 68k HAL.\n")
    at(4600)
    command("q")                                # quit
    at(4700)
    command("d")                                # Don't Save
    at(4900)
    for i in range(20):                         # move the pointer about the desktop
        point(100 + 20 * i, 120 + (i % 5) * 40, False)
        wait(4)
    at(5100)
    return 5100


def main():
    end = session()
    until = None
    if "--until" in sys.argv:
        until = int(sys.argv[sys.argv.index("--until") + 1])
    if "--frames" in sys.argv:
        print(end if until is None else until)
        return
    out = []
    for i in range(0, len(args), 2):
        f = int(args[i + 1].rsplit(",", 1)[1])
        if until is None or f < until:
            out.extend(args[i:i + 2])
    print(" ".join(out))


main()

#!/usr/bin/env python3
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
"""Boot an ISO with the Mac modules in QEMU, run the Mac and check its clock.

Usage: tools/mac_clock_check.py ISO

Types `mac` at the shell and waits for two of the kernel's reports
("mac: 600 frames, 9984 ms, clock 3750000000", one every 600 frames). Two things
must hold: the Mac's frames follow real time (600 frames in 10 s, within 3%) and
the Mac's clock (seconds since 1904) equals the real UTC time within 5 seconds
(QEMU's emulated RTC shows the host's UTC time). Exit 0 = pass.
"""

import os
import re
import socket
import subprocess
import sys
import tempfile
import time

MAC_EPOCH_OFFSET = 2082844800


def main():
    iso = sys.argv[1]
    tmp = tempfile.mkdtemp()
    serial = os.path.join(tmp, "serial.log")
    monitor = os.path.join(tmp, "mon.sock")
    args = [os.environ.get("QEMU_SYSTEM", "qemu-system-i386"), "-cdrom", iso, "-display", "none", "-serial", "file:" + serial,
            "-monitor", "unix:%s,server,nowait" % monitor, "-m", "256"]
    if os.access("/dev/kvm", os.W_OK):
        args.insert(1, "-enable-kvm")
    qemu = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        for _ in range(100):
            if os.path.exists(monitor):
                break
            time.sleep(0.1)
        sock = socket.socket(socket.AF_UNIX)
        sock.connect(monitor)
        sock.settimeout(1)

        def send(line):
            sock.sendall((line + "\n").encode())
            time.sleep(0.05)
            try:
                sock.recv(65536)
            except socket.timeout:
                pass

        def output():
            return open(serial, errors="replace").read()

        end = time.time() + 60
        while "Shell ready" not in output() and time.time() < end:
            time.sleep(0.3)
        time.sleep(0.5)

        for key in "mac":
            send("sendkey " + key)
        send("sendkey ret")

        reports = []
        end = time.time() + 60
        seen_at = time.time()
        while len(reports) < 2 and time.time() < end:
            time.sleep(0.1)
            reports = re.findall(r"mac: (\d+) frames, (\d+) ms, clock (\d+)", output())
            seen_at = time.time()

        if len(reports) < 2:
            print("mac_clock_check: FAIL: the Mac printed no reports")
            return 1

        frames, ms, clock = (int(v) for v in reports[1])
        now = seen_at + MAC_EPOCH_OFFSET
        print("mac_clock_check: %d frames in %d ms (%.2f frames/s), clock off by %.1f s"
              % (frames, ms, frames * 1000.0 / ms, clock - now))

        if abs(frames * 1000.0 / ms - 60.1) > 60.1 * 0.03:
            print("mac_clock_check: FAIL: the Mac does not run in real time")
            return 1

        if abs(clock - now) > 5:
            print("mac_clock_check: FAIL: the Mac's clock is wrong")
            return 1

        print("mac_clock_check: PASS")
        return 0
    finally:
        qemu.kill()
        qemu.wait()


sys.exit(main())

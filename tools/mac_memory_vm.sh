#!/bin/sh
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Keyboard and mouse script for tools/mac_run that turns virtual memory on with the Memory control
# panel of System 7.5.3, as a user would, and shuts the Mac down cleanly: Apple menu > Control Panels,
# type "me" (selects Memory), Command-O, click Virtual Memory "On", Command-W twice (the panel, then the
# Control Panels window, so that the next start shows an empty desktop), Special > Shut Down.
# The choice lands in the System file (resource 'vm  ' 0, kernel/mac/sysvm.h); with -O the disk image
# keeps it. Used by make test-mac-disk to check that the kernel turns it off again.
#
# Usage: tools/mac_memory_vm.sh
#   prints the mac_run arguments; run with the default 640x480 screen and -F 2800 or more.
#
# The positions were found on screenshots of the 640x480 screen (docs/mac.md section 14).

s="-J 20,9,1,1200 -J 70,60,1,1220 -J 70,60,0,1240"                       # Apple menu > Control Panels
s="$s -K 46,1,1500 -K 46,0,1503 -K 14,1,1510 -K 14,0,1513"                # m, e: select Memory
s="$s -K 55,1,1530 -K 31,1,1533 -K 31,0,1536 -K 55,0,1539"                # Command-O
s="$s -J 174,166,1,2000 -J 174,166,0,2005"                                 # Virtual Memory: On
s="$s -K 55,1,2200 -K 13,1,2203 -K 13,0,2206 -K 55,0,2209"                # Command-W: Memory
s="$s -K 55,1,2300 -K 13,1,2303 -K 13,0,2306 -K 55,0,2309"                # Command-W: Control Panels
s="$s -J 232,9,1,2400 -J 240,138,1,2420 -J 240,138,0,2440"                # Special > Shut Down
echo "$s"

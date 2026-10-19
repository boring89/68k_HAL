#!/bin/sh
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Keyboard and mouse script for tools/mac_run: open the Keyboard control panel of System 7.5.3 (Apple
# menu > Control Panels, type "ke", Command-O). With "set", also choose Key Repeat Rate "Slow" and Delay
# Until Repeat "Off" and close the panel: both live in the parameter RAM, so a run with -X keeps them and
# the panel shows them on the next start (make test-rom; kernel/mac/pram.h).
#
# Usage: tools/mac_keyboard_panel.sh [set]
#   prints the mac_run arguments; run with the default 640x480 screen, -F 1900 (open) or -F 2400 (set).
#
# The positions were found on screenshots of the 640x480 screen.

s="-J 20,9,1,1200 -J 70,60,1,1220 -J 70,60,0,1240"                       # Apple menu > Control Panels
s="$s -K 40,1,1500 -K 40,0,1503 -K 14,1,1510 -K 14,0,1513"                # k, e: select Keyboard
s="$s -K 55,1,1530 -K 31,1,1533 -K 31,0,1536 -K 55,0,1539"                # Command-O
if [ "$1" = set ]; then
    s="$s -J 125,88,1,2000 -J 125,88,0,2004"                               # Key Repeat Rate: Slow
    s="$s -J 239,88,1,2050 -J 239,88,0,2054"                               # Delay Until Repeat: Off
    s="$s -K 55,1,2200 -K 13,1,2203 -K 13,0,2206 -K 55,0,2209"            # Command-W
fi
echo "$s"

#!/bin/sh
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Mouse script for tools/mac_run (-J x,y,button,frame) that switches the Mac's
# resolution with the Monitors control panel of System 7.5.3, as a user would:
# Apple menu > Control Panels, View > by Name, double-click Monitors,
# Options..., scroll the list of resolutions, pick one, OK.
#
# Usage: tools/mac_monitors.sh <list position 0-12>
#   prints the -J arguments; run with -C 1024x768 and at least -F 3600.
#   Position = index in emu/mac/nubus_video.c mac_card_resolutions
#   (0 = 640x480 ... 12 = 3840x2160).
#
# The positions were found on screenshots of a 1024x768 screen
# (docs/nubus_video.md); the Options dialog shows four resolutions at a time,
# rows at y = 143, 155, 167, 179, and each click on the down arrow scrolls by one.

index=${1:?usage: mac_monitors.sh <resolution index 0-12>}
s="-J 20,9,1,1200 -J 70,60,1,1220 -J 70,60,0,1240"          # Apple menu > Control Panels
s="$s -J 130,9,1,2000 -J 150,59,1,2020 -J 150,59,0,2040"     # View > by Name
s="$s -J 80,170,1,2400 -J 80,170,0,2403 -J 80,170,1,2406 -J 80,170,0,2409"   # open Monitors
s="$s -J 293,98,1,3000 -J 293,98,0,3005"                     # Options...
scroll=0
[ "$index" -gt 3 ] && scroll=$((index - 3))
f=3100
i=0
while [ $i -lt $scroll ]; do
    s="$s -J 363,180,1,$f -J 363,180,0,$((f + 2))"
    f=$((f + 15))
    i=$((i + 1))
done
row=$((143 + 12 * (index - scroll)))
f=$((f + 30))
s="$s -J 250,$row,1,$f -J 250,$row,0,$((f + 5))"               # the resolution
s="$s -J 335,81,1,$((f + 60)) -J 335,81,0,$((f + 65))"        # OK
echo "$s"

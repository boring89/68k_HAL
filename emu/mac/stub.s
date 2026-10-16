| Copyright (C) 2026 YuanChi Hsieh
| SPDX-License-Identifier: GPL-3.0-or-later
|
| Native-call stubs of the Macintosh model, mapped at MAC_STUB_BASE
| (0x4F000000). Assembled with: m68k-linux-gnu-as -m68030; the bytes are kept
| in emu/mac/stub_code.h (regenerate with tools/gen_mac_stub.py; the host test
| checks that they match this file).
|
| The $71xx words are "emulated opcodes": illegal 68k instructions that
| emu/mac/mac.c intercepts to run native code (see emu/mac/mac.h).
|
| 0x000  install      called by the patched ROM instead of its code at 0x1166
| 0x100  .Disk driver header (a DRVR as the Device Manager expects it)
| 0x1F0  name of the driver as a Pascal string
|
    .text

| ---------------------------------------------------------------------------
| install: add the .Disk driver to the unit table and open it.
| Entered with JSR from ROM offset 0x1166, where the ROM was about to load the
| serial drivers ("subq.w #4,sp; move.l #'SERD',-(sp)"); those two
| instructions are executed here before returning.
| ---------------------------------------------------------------------------
install:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    move.w  #-50,%d0                | driver reference number
    .word   0xA43D                  | _DrvrInstallRsrvMem
    tst.w   %d0
    bne.s   install_done
    movea.l 0x11C,%a0               | UTableBase: one handle per unit,
    movea.l 49*4(%a0),%a0           | index = ~refnum = 49
    movea.l (%a0),%a1               | A1 = the DCE
    .word   0x710A                  | native: fill in the DCE (driver pointer, flags)
    suba.w  #50,%sp                 | an IOParam block for _Open
    movea.l %sp,%a0
    clr.b   27(%a0)                 | ioPermssn
    lea     driver_name(%pc),%a1
    move.l  %a1,18(%a0)             | ioNamePtr
    .word   0xA000                  | _Open
    adda.w  #50,%sp
install_done:
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    movea.l (%sp)+,%a0              | return address
    subq.w  #4,%sp                  | the two displaced instructions
    move.l  #0x53455244,-(%sp)      | 'SERD'
    jmp     (%a0)

| ---------------------------------------------------------------------------
| The .Disk driver (Inside Macintosh: Devices, "Device Manager", driver header)
| ---------------------------------------------------------------------------
    .org    0x100
driver:
    .word   0x6F00                  | drvrFlags: read, write, control, status, needs lock and time
    .word   60                      | drvrDelay (ticks between periodic calls)
    .word   0                       | drvrEMask
    .word   0                       | drvrMenu
    .word   drv_open-driver
    .word   drv_prime-driver
    .word   drv_control-driver
    .word   drv_status-driver
    .word   drv_close-driver
driver_name:
    .byte   5
    .ascii  ".Disk"
    .align  2

| Open: the native part fills in the drive record and the drive queue entry
| (A0 = qLink of the record, D0 = drive number << 16 | refnum); _AddDrive
| links it into the drive queue.
drv_open:
    .word   0x7110
    .word   0xA04E                  | _AddDrive
    moveq   #0,%d0
    rts

drv_prime:
    .word   0x7111
    bra.s   drv_finish

drv_control:
    .word   0x7112
    bra.s   drv_finish

drv_status:
    .word   0x7113

| Immediate calls (ioTrap bit 9) return with RTS, queued calls through the
| Device Manager's IODone vector in low memory (0x8FC), A1 = DCE, D0 = result.
drv_finish:
    btst    #9,6(%a0)
    bne.s   drv_return
    movea.l 0x8FC,%a0
    jmp     (%a0)
drv_return:
    rts

drv_close:
    moveq   #-24,%d0                | closeErr: a disk driver is never closed
    rts

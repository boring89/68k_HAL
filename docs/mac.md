# Macintosh IIci 機器模型（emu/mac）

這份文件說明 68k_HAL 怎麼讓**真正的 Mac IIci ROM** 與 **System 7.5.3** 在模擬的 68030 上開機到 Finder 桌面。這部分是由自動排程一次做完的（P3 計畫），使用者還沒有逐步確認過，所以盡量寫清楚「是什麼、為什麼這樣做、哪裡是猜的」。

> **版權**：ROM 與磁碟映像是使用者自己的檔案，**不在 repo 裡**，也不會被 commit、不會出現在 CI。`make test` 完全不需要它們；需要它們的驗證都在本機的 `make test-rom`、`make test-rom-qemu`、`make mac-screenshot`。

## 1. 現在做到哪裡

```
 ROM 開始執行 ─► 硬體偵測（略過）─► 記憶體/插槽初始化 ─► 灰色畫面＋磁片圖示
      ─► .Disk 驅動找到磁碟 ─► "Welcome to Macintosh." ─► "Mac OS Starting up..."
      ─► Finder 桌面（選單列、時鐘、磁碟與垃圾桶圖示）
```

截圖（本機產生，在 `build/mac/`，不進 git）：

| 檔案 | 內容 |
|---|---|
| `s3_question_disk.png` | 沒有磁碟時 ROM 畫的灰色桌面與磁片圖示 |
| `s4_welcome_to_macintosh.png` | System 7.5.3 從映像開機，"Welcome to Macintosh." |
| `s6_finder_desktop.png` | Finder 桌面 |

還沒有的東西（見第 10 節）：聲音、序列埠/網路、彩色與其他解析度。鍵盤與滑鼠可以用（第 5 節的「輸入」）。

## 2. 比喻：我們替 ROM 蓋了一棟「假的機房」

Mac 的 ROM 是一本很長的開機劇本，劇本裡不斷地「去敲某個地址的門」：敲 `0x50F00000` 問 VIA 晶片、敲 `0x50F10000` 問 SCSI 晶片、敲 `0xFEFFFFFF` 看插槽裡有沒有卡……真正的機器每扇門後面都有一顆晶片會回答。

`emu/mac` 就是蓋一棟有同樣門牌的假機房：有些門後面放了小小的替身（VIA、RBV、DAC…），有些門後面什麼都沒有（回 0，並記在「未實作存取記錄」裡）。劇本裡走不通的段落，則直接**修補劇本**，改成呼叫我們自己的程式（第 4 節）。這和 Basilisk II 的作法相同（路線 B）：Toolbox 與 System 原封不動地執行，硬體交給我們。

## 3. 記憶體配置（Mac IIci，32 位元定址）

```
 0x00000000 ┌────────────────────────┐
            │ RAM 8 MiB              │  （reset 時 ROM 的「overlay」蓋在這裡）
 0x00800000 ├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
            │ （空，讀到 0）          │
 0x40000000 ├────────────────────────┤
            │ ROM 512 KiB，每 512 KiB │  ROM 在 0x40800000 起（鏡射）
            │ 重複到 0x4FFFFFFF       │
 0x4F000000 │ stub 視窗 8 KiB（可寫） │  .Disk 驅動與 ADBOp 替身的 68k 程式碼
 0x50F00000 ├────────────────────────┤  I/O：
            │ VIA1       0x50F00000  │    時鐘晶片、ADB 線、ROM overlay、60 Hz
            │ SCC        0x50F04000  │    序列埠（只有閒置的狀態位元）
            │ SCSI       0x50F10000  │    NCR 5380（匯流排上沒有裝置）
            │ ASC        0x50F14000  │    聲音（FIFO 模式；見第 11 節）
            │ IWM        0x50F16000  │    軟碟控制器（最小替身）
            │ DAC        0x50F24000  │    調色盤
            │ RBV        0x50F26000  │    VIA2 的替代品，含視訊暫存器
 0xFBB08000 ├────────────────────────┤
            │ 視訊 RAM 512 KiB        │  地址來自 ROM 的 UniversalInfo 表
 0xFE000000 │ slot $E：ROM 尾端        │  內建視訊的 declaration ROM 就是 ROM 的最後一段
            └────────────────────────┘
```

**Reset overlay**：CPU 重置後會從地址 0 讀出 SSP、地址 4 讀出 PC，但 0 是 RAM、ROM 在 `0x40800000`。所以重置時硬體讓 ROM「暫時出現在 0」（overlay）；ROM 早期的程式碼把 VIA1 port A 的 bit 4 拉低就關掉它。`mac.c` 的 `overlay` 旗標就是這件事。

出處：*Guide to the Macintosh Family Hardware*（記憶體配置與 VIA 暫存器）、Inside Macintosh: Devices（驅動結構）。**不確定的地方在程式碼註解裡明說。**

## 4. 修補 ROM（`rom_patch.c`）

我們**不改使用者的 ROM 檔**：`mac_rom_patch()` 在記憶體中的複本上改幾個位置，並且先檢查第一個 long 是 checksum `368CADFE`，不是這顆 ROM 就拒絕。位置的選擇參考 Basilisk II 的 `rom_patches.cpp`（GPL），偏移量都用反組譯（`m68k-linux-gnu-objdump -D -b binary -m m68k:68030 --adjust-vma=0x40800000`）逐一驗證過。

「模擬 opcode」：`$71xx`（`0x7100`–`0x7139`）在 68k 是非法指令，所以真的程式不會用。修補後的 ROM 在需要原生程式的地方放這種 opcode，`mac.c` 的 instruction hook 看到它就執行 C 函式，再繼續跑下一條指令。

| ROM 偏移 | 原本做什麼 | 現在 | 為什麼 |
|---|---|---|---|
| `0x8C` | 重置後的硬體偵測 | `RESET` 模擬 opcode＋`jmp 0xBA` | 查 UniversalInfo 表把 IIci 的暫存器設好、建 BootGlobs、堆疊、關 overlay |
| `0xC2`、`0xCE` | 呼叫 GetHardwareInfo、InitHW | NOP | 這些會戳真實晶片 |
| `0x2F1E` | GetHardwareInfo 的「D2=0 就探測硬體」 | `HWINFO` 模擬 opcode | 改成直接查 IIci 的表項 |
| `0xB1E4` | 與時鐘晶片通訊（RTC＋XPRAM） | `CLKNOMEM` 模擬 opcode＋`jmp (A5)` | 原生保存 256 位元組參數 RAM |
| `0x430A` | InitMMU 建 24 位元轉換表 | 比較改成永遠相等 | 不需要轉換，永遠 32 位元模式 |
| `0x4384`、`0x438C` | `pmove (A3),tc` 開啟轉換 | NOP | 同上（Musashi 的 PMMU 也不支援那種表） |
| `0x4BD8` | `_SwapMMUMode` | `moveq #1,D0; rts` | 永遠回報 32 位元模式 |
| `0x10E` | 從 frame 取 MemTop/BufPtr | `PATCH_BOOT_GLOBS` | 見下 |
| `0x1166` | 載入序列驅動（SERD） | `jsr install`（stub） | 加入 `.Disk` 驅動；跳過序列驅動（沒有 SCC 中斷） |
| `0x7460` | SCSI Manager 入口 | `SCSI_DISPATCH` 模擬 opcode | 沒有裝置：Select 回 scCommErr，其餘成功 |
| `0x7860` | 等目標回應 BSY（逾時要幾分鐘） | `moveq #2,D0; rts` | 立刻逾時 |
| `0xA3DC` | `_ADBOp` | `jmp` 到 stub | 每個 ADB 命令立刻回「沒有資料」 |
| `0xA8E6` | 等 ADB 重新初始化的狀態機 | NOP | 沒有 ADB 硬體 |
| `0x36D0`、`0x3710` | UniversalInfo 的 FPU 旗標（bit 28） | 清除 | 否則 System 會執行 FNOP 偵測而炸掉 |

**BootGlobs 與 BufPtr**：ROM 用實際探測記憶體得到的值算出 MemTop 與 BufPtr；我們的 RAM 是一整塊，那些值會是亂數。`PATCH_BOOT_GLOBS` 把 MemTop 設成 RAM 大小（8 MiB），BufPtr 保持 ROM 算出的距離但**對齊 4 位元組**。不對齊的話，ROM 把堆疊與 heap 的分界切在奇數位址，後來 boot block 被載到奇數位址，執行它時就發生 address error（指令位址一定要是偶數）。這是整個過程中最難找的 bug 之一。

## 5. 硬體替身

**VIA1**（`via.c`）：MOS 6522。兩個 8 位元埠、兩個計時器、移位暫存器、中斷旗標（IFR）與遮罩（IER）。比喻：IFR 是一排門鈴按鈕，IER 是每顆按鈕旁的靜音開關，CPU 只聽「有沒有任何一顆沒被靜音的按鈕被按著」。VIA1 CA1 腳是 60.15 Hz 的 Ticks 中斷（Level 1）。我們以 CPU 25 MHz、VIA 時脈 = CPU / 32 換算。ROM overlay 是 port A 的 bit 4（輸出且為低就關）。

**RBV**（`video.c`）：IIci 用它取代 VIA2。暫存器（ROM 的視訊驅動與 slot 中斷程式碼，加上 Linux `mac_via.h` 的名稱）：

| 偏移 | 作用 |
|---|---|
| `0x02` | slot 中斷旗標，**低電位有效**；bit 6 = 內建視訊垂直消隱（驅動輪詢它：1→0 就是消隱開始） |
| `0x03` | 中斷旗標：bit 1 = 有 slot 中斷、bit 4 = 聲音晶片；寫入無作用（旗標跟著來源） |
| `0x10` | bit 0–2 深度、bit 3–5 螢幕感測線（13" RGB = 6，640x480） |
| `0x12` | slot 中斷遮罩（bit 7 設定/清除，bit 6 = 內建視訊）；再寫 bit 6 就重新武裝視訊中斷 |
| `0x13` | 中斷遮罩 |

**踩過的坑**：一開始把垂直消隱當成一般的 IFR 旗標，ROM 的 level-2 處理程式看 SIFR&SIER=0 就返回，卻沒有人清掉旗標，結果 level-2 一直被重新取用，Level 1（Ticks）永遠輪不到，系統停在「等 Ticks」的迴圈。現在的做法：垂直消隱先「鎖存」，只在遮罩都打開時變成中斷，視訊 ISR 寫 `0x12` 重新武裝時清掉。

**DAC 與視訊 RAM**：視訊 RAM 在 `0xFBB08000`（ROM 的 UniversalInfo 視訊表給的），DAC 在 `0x50F24000`：位址暫存器在偏移 0，資料在偏移 4，連寫 R、G、B 三次後索引加 1。`mac_render` 依 RBV 的深度（1/2/4/8 bpp）與調色盤轉成 `0x00RRGGBB`。比喻：視訊 RAM 是一張「號碼格紙」，DAC 是「號碼對照色表」。

**IWM（軟碟）**：只讓 ROM 確認「有控制器在」：八個「開關」，寫模式暫存器、從狀態暫存器讀回，感測線永遠高（沒有磁片）。

**SCSI**：NCR 5380 的最小替身，仲裁一定成功、選擇一定逾時（匯流排上沒有人）。磁碟不走 SCSI（見第 6 節）。

**SCC**：只回報閒置狀態（Tx 緩衝區空、hunt、Tx underrun）。LocalTalk 驅動啟動時只檢查這些位元；這是「讓 System 7 不要卡住」的最小做法，不能真的收發。

**時鐘晶片與參數 RAM**（`CLKNOMEM`）：命令在 D1（bit 7 = 讀，bit 6–2 = 暫存器；`0x38` 開頭是 extended 命令，位址 = `((cmd&7)<<5)|((cmd>>10)&0x1F)`），資料在 D2。暫存器 0–7 回傳秒數計數器；其他存取 256 位元組的 XPRAM。XPRAM `0x8A` 永遠回報 32 位元定址（同 Basilisk II）。全新的 XPRAM 會被 ROM 填預設值。

**輸入（鍵盤與滑鼠）**：沒有 ADB 硬體，所以不走硬體。`mac_mouse_move()`、`mac_key()` 把 ADB 裝置「會送出的封包」放進佇列（滑鼠：byte 1 = 放開位元(bit 7)＋dy，byte 2 = dx；每包最多 ±63；鍵盤：碼與按下/放開），在 CPU 可以接受 level 1 中斷（supervisor 模式、遮罩 0）的下一條指令，模型推一個 format 0 例外框，跳到 stub 裡的 `inject`；它用 ROM 的 ADB 裝置表（`ADBBase` = `$CF8`，每筆 12 位元組，+2 位址、+4 處理常式、+8 資料區）找到系統註冊的處理常式，用 A0 = 封包、A1 = 處理常式、A2 = 資料區、D0 = Talk R0 命令呼叫它，最後 `rte`。這就是 ADB 中斷會做的事，所以游標加速、按鍵轉譯（KeyTrans）、mouseDown/keyDown 事件都由 ROM 與 System 自己處理。驗證：host 的 `-M dx,dy,按鈕,幀` 與 `-K 碼,按下,幀`（選單 Label 在按下滑鼠時拉出；按 T 選取垃圾桶）。kernel 端：PS/2 事件經 `kernel/mac/adbkeys.c` 轉成 ADB 鍵碼；按 ` 鍵停止。

## 6. `.Disk` 驅動（`stub.s`、`disk.c`）

為什麼不用 SCSI：ROM 內建的 SCSI 磁碟驅動會去磁碟上找 `'ER'` 分割表和 Apple 的 SCSI 驅動程式（在磁碟上，不在 ROM），我們的映像沒有分割表（開頭是 `LK` boot block，HFS 在 1024）。所以照 Basilisk II 的作法：寫一個原生驅動，再放進 Device Manager。

```
 File Manager ──PBRead──► Device Manager ──Prime──► .Disk 驅動（stub 視窗裡的 68k 程式碼）
                                                         │ 執行 $7111 模擬 opcode
                                                         ▼
                                                   disk.c: mac_disk_prime()
                                                         │ 回呼
                                                         ▼
                                              host: 記憶體中的映像 / kernel: 模組＋寫入覆蓋層
```

- **install**（`stub.s`）：在 InitDrivers 載入序列驅動前被呼叫。`_DrvrInstallRsrvMem`（refnum -50）建 DCE，從 unit table 取出，`$710A` 填 dCtlDriver/flags，再 `_Open` 它。
- **驅動標頭**：drvrFlags `0x6F00`（讀寫控制狀態、需要鎖定與定時）、五個入口。每個入口一個模擬 opcode；立即呼叫（ioTrap bit 9）用 `rts` 返回，否則 `jmp` 到 IODone（低記憶體 `0x8FC`）。
- **Open**：填 22 位元組的 DrvSts（`diskInPlace = 8` 固定磁碟、`dQDrive = 8`、`dQRefNum = -50`、大小拆成低/高 16 位元），然後 `_AddDrive`（D0 = 磁碟機號<<16 | refnum，A0 = DrvSts 裡的 qLink）。
- **Prime**：位置在 DCE 的 `dCtlPosition`（+16），PB 的 ioBuffer(+32)、ioReqCount(+36)；ioTrap 低位元組 2 = 讀、3 = 寫。必須是 512 的倍數。成功後 ioActCount、dCtlPosition 都前進。
- **Control**：1、65、5、7 成功；6 看是否唯讀；24 回區塊數；其他 controlErr。**Status**：8 回 DrvSts 的 22 位元組。
- **stub 視窗**：`0x4F000000` 的 8 KiB 可寫記憶體。程式碼由 `emu/mac/stub.s` 組譯，位元組放在 `stub_code.h`（`tools/gen_mac_stub.py` 產生；`make test` 的 `test-mac-stub` 驗證兩者一致）。同一個視窗還放 `adbop`（ADBOp 替身）與 DrvSts 記錄。

## 7. 診斷工具

`make build/host/mac_run_fast`（無 sanitizer，約 20M 指令/秒；`mac_run` 是有 ASan 的版本）。用法見 `tools/mac_run.c` 檔頭，常用的：

| 選項 | 作用 |
|---|---|
| `-n N` | 最多跑 N 條指令 |
| `-t K -u` | 結尾印最後 K 條指令的反組譯，`-u` 把迴圈收成「… N repeated」 |
| `-b A[-B]` | 執行到 PC 進入該位址（範圍）就停；`-s N` 之後再單步 N 條 |
| `-c` | 依「第一次執行」的順序列出跑過的 ROM 區塊（64 位元組） |
| `-m ADDR` | 印出 guest 記憶體 16 位元組 |
| `-x ADDR LEN FILE` | 把 guest 記憶體存檔（再用 objdump 反組譯 RAM 裡的程式；**只放在 /tmp 或 build/**） |
| `-r` | 印 VIA、RBV、時鐘晶片命令記錄與 XPRAM |
| `-a` | 列出未實作的硬體存取（地址、PC、次數） |
| `-d IMG` | 掛上磁碟映像（讀進記憶體，**不寫回檔案**）；`-R` 唯讀 |
| `-p FILE` | 結束時把畫面寫成 PPM |
| `-F N` | 跑 N 個整幀並印畫面的 CRC-32（和 kernel 的結果比較） |

一般的卡關流程：`-t 40 -u` 看 PC 在哪個迴圈 → `-m` 看它讀哪個旗標 → 用 objdump 反組譯 ROM 或用 `-x` 存 RAM 看是誰該清這個旗標 → 補上硬體行為或修補。

## 8. 接到 kernel（`kernel/mac/`）

```
 GRUB / QEMU -initrd  ──►  boot module "mac_rom"、"mac_disk"
                                │
 shell 指令 mac ──► mac_machine_run()
                      ├─ ROM 複本（模組唯讀）→ mac_rom_patch
                      ├─ Mac RAM 8 MiB：重用 kernel/m68k/guest（PMM frame 映射在 0x10000000）
                      ├─ 視訊 RAM、畫面緩衝：kmalloc
                      ├─ 磁碟：模組唯讀 + 64 KiB 為單位的寫入覆蓋層（最多 4 MiB 的變更）
                      └─ 每幀 416000 個 CPU 週期（約 1/60 s Mac 時間）；每 6 幀把畫面畫到 framebuffer
```

- 需要圖形模式（640x480x32 framebuffer）才看得到畫面；沒有 framebuffer 時照樣執行，只是沒畫面。按 ` 鍵停止。
- 磁碟寫入只存在覆蓋層（記憶體），不會改到模組，關機就沒了。Finder 啟動時要建 Desktop 檔案，所以覆蓋層是必要的：唯讀時會跳出「the desktop folder on the startup disk could not be created. Try unlocking the disk.」。
- 模組以「名稱的最後一個字」辨識（QEMU 傳 `"<檔案> <參數>"`）：`-initrd "rom.bin mac_rom,disk.img mac_disk"`。
- **決定性**：同樣的 ROM、磁碟、幀數，host 與 kernel 畫出一模一樣的畫面，所以 `make test-rom-qemu` 比較兩邊的畫面 CRC-32（預設 600 幀；KVM 約幾秒，TCG 會慢很多）。
- `make run-mac`：互動執行。建好 ISO（ROM 與磁碟是複本，在 `build/mac/play.iso`）後開啟 QEMU 視窗（有 KVM 就用）；在視窗中輸入 `mac` + Enter，shell 的輸出在終端機，`` ` `` 停止，`Ctrl+Alt+G` 放開滑鼠。
- `make mac-screenshot`：用 `grub-mkrescue` 建只在本機使用的 ISO（模組是複本），QEMU 圖形模式執行後用 monitor 的 `screendump` 存 `build/mac/qemu_screen.ppm`。

GRUB 手動使用：在 `grub.cfg` 加一個項目

```
menuentry "mac" {
    multiboot /boot/kernel-gfx.elf
    module /mac_rom mac_rom
    module /mac_disk mac_disk
    boot
}
```

再把兩個檔案放進 ISO 根目錄，開機後在 shell 輸入 `mac`。

## 9. 一個 bug 的故事（BufPtr 的最低兩個位元）

症狀：ROM 載入 boot block 後，`jsr (2,A6)` 跳到 `0x400E53`，`SysError` 的畫面出現 "error type 3"（address error）。追法：看堆疊——`A7` 是奇數；往回看 `BufPtr`（`$10C`）= `0x7FFCA2`，也不是 4 的倍數；再往回看 `InitMMU` 提早返回的路徑留下的 frame。ROM 用「`BufPtr` 與 `SysZone` 的中點」切 heap 與堆疊，中點跟著 `BufPtr` 的低位元一起歪掉。這就是 `PATCH_BOOT_GLOBS` 把 `BufPtr` 對齊的原因。

## 10. 已知問題與限制

- **輸入**：ADB 命令（Talk/Listen/Flush）全部回「沒有資料」，所以不能改鍵盤 LED、不能 Listen 設定；按鍵與滑鼠只靠上面的封包注入。沒有 Command/Option/Control 鍵（PS/2 只有 Shift 有對應）、沒有滑鼠右鍵。
- **聲音只有 ASC 的 FIFO 模式**（System beep 與一般取樣聲音夠用）；沒有 wave table 模式、沒有立體聲分離、音量暫存器被忽略；取樣率暫存器 1–3 的數值是推測。見第 11 節。
- **沒有序列埠與網路**：序列驅動（SERD）不載入；LocalTalk 靠一個閒置的 SCC 蒙混過去。AppleTalk 不能用。
- **只有 13" RGB 感測（640x480）**，ROM 預設 1 bpp 時桌面是黑白；其他深度與顯示器沒測。
- **只支援 ROM `368CADFE`**（Mac IIci）；其他機型的偏移量不同。
- **Time Manager** 用的是 ROM 原本的程式，靠 VIA 計時器；目前能開機到桌面，長時間執行與計時器密集的程式沒測。
- **速度**：host 約 20M 指令/秒，Finder 桌面約需 5 億條指令（25 秒）；kernel 在 QEMU/KVM 內差不多，TCG 會慢 5–10 倍；沒有實時節流，Mac 的時鐘跑得比牆上時間快。
- **磁碟寫入不會保存**（kernel 的覆蓋層在記憶體；host 的映像只在記憶體裡改）。
- 這些修補與替身只涵蓋「開機到桌面」所碰到的路徑，沒有測過的程式（尤其是會直接戳硬體的）可能還需要更多。

## 11. 參考

- *Guide to the Macintosh Family Hardware*, 2nd ed.（記憶體配置、VIA/RBV、IWM、SCSI）
- *Inside Macintosh: Devices*（Device Manager、驅動標頭、DCE）；*Inside Macintosh: Files*（Drive Queue）
- Basilisk II 的 `rom_patches.cpp`、`emul_op.cpp`、`disk.cpp`、`slot_rom.cpp`（GPL；只參考作法，程式碼自己寫，相關註解註明出處）
- Linux `arch/m68k/include/asm/mac_via.h`（RBV 暫存器名稱）
- MOS 6522 data sheet；NCR 5380 data sheet；Zilog 8530 data sheet


## 11. 聲音（ASC，Apple Sound Chip）

`emu/mac/asc.c`、`kernel/mac/machine.c`（接到 `kernel/sound`）

比喻：ASC 是一個「水龍頭＋兩個小水桶」：System 把 8 位元取樣一個個倒進兩個 FIFO（先進先出的桶，各 1024 位元組，A 左 B 右），晶片以固定速率（約 22254 Hz）從桶底取走並播放；桶快空時（剩不到一半）打鈴（中斷）叫 System 再倒 512 個。

暫存器（偏移 0x000–0x80F）：

| 偏移 | 內容 |
|---|---|
| 0x000–0x3FF／0x400–0x7FF | FIFO A／FIFO B（FIFO 模式下寫入即推入） |
| 0x800 | 版本；原版 ASC 讀 0（ROM 開機與 Sound Manager 都讀這個） |
| 0x801 | 模式：0 關、1 FIFO、2 wave table |
| 0x802 | 控制（bit 1 立體聲） |
| 0x803 | FIFO 模式：bit 7 清空兩個 FIFO |
| 0x804 | FIFO 狀態（讀取即清除）：bit 0 A 半空、bit 1 A 滿、bit 2 B 半空、bit 3 B 滿 |
| 0x806／0x807 | 音量／取樣率 |

**怎麼找出它要什麼**：Sound Manager 的 FIFO 填充迴圈（在 System 7.5.3 的 RAM 碼，用 `-x` 傾印後反組譯）是「寫 B、寫 A、讀 0x804、`and #8`、狀態 bit 3 還沒出現就繼續」——所以「滿」旗標在 FIFO 填滿時要出現。中斷只由「半空」位元產生（RBV 的中斷旗標 bit 4，要在 IER 啟用）。這些是由反組譯與 MAME 的 `apple/asc.cpp` 排版推得，**沒有硬體文件確認**。

**輸出**：`mac_t.audio` 回呼收到「晶片這段時間播了哪些 8 位元無號取樣＋取樣率」（FIFO 空時是 0x80 靜音）。kernel 把一個 frame 的取樣收集起來，用 `sound_resample_u8`（帶狀態，frame 與 frame 之間不會有咔聲）轉成 48 kHz 16 位元立體聲，丟進 `sound_write`（HDA 串流）。聲音佇列滿了就丟掉：模擬不會為了聲音變慢。

**測試**：
- host：`tests/host/test_mac_asc.c`（FIFO 播放、旗標、中斷、RBV 的聲音中斷）。
- `mac_run -B <frame> -W out.wav`：在執行中的系統裡呼叫 `SysBeep`（`mac_sysbeep()`：存下 CPU 狀態，在 stub 視窗跑 `move.w #3,-(sp); _SysBeep`，回來再還原），並把晶片播出的聲音存成 WAV。結果：0.32 秒、約 439 Hz 的嗶聲（振幅 ±28 / 128，音量偏小，System 自己決定）。
- `make test-mac-sound`（本機專用，需要 ROM 與磁碟）：在 QEMU 的 kernel 裡跑 Mac，第 1200 個 frame 呼叫 `mac_sysbeep`，QEMU 把 HDA 輸出錄成 WAV，`tools/check_wav.py` 確認有約 440 Hz 的聲音。

## 12. 時間、磁碟寫入與測試工具（P6 Q7）

**一秒中斷與時鐘**：模型現在每數 60 個 60 Hz tick 就讓 `rtc_seconds` 加 1，並在 VIA1 的 CA2（時鐘晶片的「每秒中斷」，Guide to the Macintosh Family Hardware）設旗標。System 的 `Time` 全域變數靠這個中斷前進，所以選單列的時鐘會走（以前時鐘是凍結的）。

**實時節流**（`kernel/mac/machine.c` 的 `pace`）：互動執行（`mac` 指令，不是測試）時，每個 frame（416000 個 CPU 週期 = 16.64 ms 的 Mac 時間）結束後等到實際時間也過了 16.64 ms 才跑下一個 frame；所以 Mac 的 60 Hz、一秒中斷、聲音速度都和真實時間一致（實測 600 frames = 9984 ms）。主機太慢時就全速跑、不追趕；停頓超過 100 ms 就重新對時。互動執行也用真實的 UTC 時間當 Mac 的起始時間（Mac 的紀元是 1904-01-01，與 Unix 紀元差 2082844800 秒）；測試仍用固定日期，結果可重複。每 600 個 frame 在 serial 印 `mac: 600 frames, 9984 ms`。

**ATA 寫入**：`block_write` 與 ATA 驅動的 WRITE SECTORS（PIO）＋FLUSH CACHE。`mac` 指令的磁碟選擇：
- 有 boot module `mac_disk`：記憶體覆蓋層（改動只留在記憶體，原本的行為）。
- 沒有 `mac_disk` 但有 ATA 磁碟（`ata0`）：Mac 直接讀寫它，改動會留下。QEMU 用 `-drive file=複本.img,format=raw,if=ide`。**只用複本**，不要把你的原始磁碟映像掛進去。

System 7 不會立刻把變動寫到磁碟（HFS 的 catalog 有快取，90 秒後仍沒寫）；要在「特殊 > 關機」時才會 flush。所以 `make test-mac-persist`（本機專用）的腳本是：File > New Folder，之後 Special > Shut Down，再用 hfsutils 檢查映像裡的 `Desktop Folder` 有 `untitled folder`。

**測試用的滑鼠定位**：用相對移動封包要定位到絕對座標很不準（Mac 有滑鼠加速）。`mac_mouse_warp(x, y)` 直接把游標位置寫進低位址記憶體的 `RawMouse`、`MTemp`、`Mouse`，並設 `CrsrNew`，馬上到位。`tools/mac_run -J x,y,按鈕,frame` 與 kernel 的測試腳本都用它；選單項目的座標是用開啟選單的截圖找出來的。其他工具：`-O 檔案`（結束時把磁碟映像寫出）、`-W 檔案.wav`、`-B frame`（呼叫 SysBeep）。

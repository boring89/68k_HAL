# 68k_HAL 磁碟：開機直接進入 Mac、可保存的系統碟（P10）

P10（自動排程一次做完，**使用者尚未審核**；只在 QEMU 與 host 驗證，沒有實機驗證）。

## 1. 要解決什麼

以前 Mac 的系統碟是 GRUB 載入的 boot module（記憶體裡的複本，關機就不見），或整顆 ATA 硬碟直接寫。現在：

- 開機**直接進入 Mac 桌面**（不打字）。
- 一顆磁碟（使用者的 `sda`，1 TB SATA SSD）整顆給 Mac：開機分割區、兩個系統槽、疊加層、Applications，剩下的空間留給之後的 eHFS。
- Mac 系統的**原始映像永遠不修改**；Mac 對系統碟的寫入（偏好設定、桌面資料庫、系統更新）都存在**疊加層**，關機後還在，`macsys reset` 就回到原始狀態。

比喻：原始映像是一本印好的書，疊加層是一疊便利貼。Mac 改哪一頁，就在便利貼上寫新的內容貼在那一頁上；讀的時候有便利貼就看便利貼，沒有就看書。撕掉所有便利貼（reset）書就恢復原樣。

## 2. 磁碟配置（GPT）

```
sda（GPT，512 位元組的區塊）
├─ 1. 68k_HAL Boot      FAT32（EFI 系統分割區），1 GiB
│      EFI/BOOT/BOOTX64.EFI    GRUB（grub-mkstandalone，x86_64-efi）
│      68k_hal/grub.cfg        選單（repo 的 grub-mac.cfg）
│      68k_hal/grubenv         GRUB 的環境區塊（1024 位元組）：boot_slot、system_slot、trial
│      68k_hal/68k_hal.cfg     設定檔（第 5 節）
│      68k_hal/mac_rom         使用者的 Mac IIci ROM（複本）
│      68k_hal/slot_a/         kernel-gfx.elf、kernel-gfx1080.elf、kernel-gfx1440.elf、initrd.tar
│      68k_hal/slot_b/         同上（更新寫到沒在用的槽；第 8 節）
├─ 2. 68k_HAL System A  Apple HFS 類型，1 GiB：Mac 系統映像（使用者的 System7_5_3.img 原樣放在開頭），永遠不修改
├─ 3. 68k_HAL System B  Apple HFS 類型，1 GiB：另一個系統槽（一開始是 A 的複本）
├─ 4. 68k_HAL Overlay   我們的類型，4 GiB：Mac 對系統碟的寫入
├─ 5. Applications      Apple HFS 類型，2 GiB：一顆空的 HFS（暫時的；第二階段由 eHFS 取代）
└─ 剩餘空間             不建立分割區，預留給第二階段的 eHFS
```

- 分割區類型 GUID：ESP `C12A7328-F81F-11D2-BA4B-00A0C93EC93B`；System 與 Applications 用 Apple HFS `48465300-0000-11AA-AA11-00306543ECAC`；疊加層用我們自己定義的 `68AC0001-4F56-4C59-8D1E-68A1D5C0DE01`。kernel 以**分割區名稱**找（`kernel/fs/gpt.h` 的 `GPT_NAME_*`）。
- 系統映像本身沒有分割表（開頭是 `LK` 開機區塊，HFS 在 1024，見 `docs/mac.md` 第 6 節）；放進 1 GiB 的分割區後，Mac 看到的磁碟大小是 1 GiB，但 HFS 卷宗仍是映像原本的大小（25 MB）。要更大的系統卷宗需要另做一個大的映像。
- GPT 的解析：`kernel/fs/gpt.c`（UEFI Specification 2.10 第 5.3 節：LBA 1 的標頭、標頭與 entry array 的 CRC-32 都檢查）；`gpt_view_register` 把一個分割區變成一個區塊裝置。
- `kernel/fs/macdisk.c` 在開機時找第一顆有 `68k_HAL System A` 與 `68k_HAL Overlay` 的 GPT 磁碟，建立區塊裝置 `boot`、`sys_a`、`sys_b`、`ovl_store`、`apps`，並開啟疊加層，得到區塊裝置 **`system`**（Mac 的系統碟）。只有這顆磁碟會被寫入，其他磁碟不碰。

## 3. 疊加層（`kernel/block/overlay.c`）

區塊層級的寫入時複製（copy-on-write）。疊加層分割區（store）的格式，區塊大小與 System 分割區相同（512）：

```
區塊 0               標頭
  0  magic "68KOL101"         8 位元組
  8  flags                    bit 0 = RESETTING（重置進行中）
 12  slot                     屬於哪個 System 槽（0 = A，1 = B）
 16  base_blocks              該槽的區塊數（uint64）
 24  base_crc                 該槽的 identity（見下）
 28  block_size
 32  map_blocks               對照表的區塊數
 36  data_blocks              資料區的區塊數
 40  header CRC-32            前 40 位元組
區塊 1 .. map_blocks  對照表：System 的每個區塊一個 uint32（小端序）；0 = 沒改過，n = 資料區第 n-1 塊
其餘                  資料區
```

- **讀**：對照表是 0 的區塊從 System 分割區讀，否則從資料區讀（連續的區塊合併成一次讀取）。**寫**：一律寫疊加層；System 分割區從來不寫。
- **identity**：System 分割區的區塊數加上它**頭尾各 1 MiB 的 CRC-32**。開機時重算並與標頭比對；不符合（換了系統映像、或疊加層屬於另一個槽）就**不套用**，開機訊息與 `dmesg` 有警告，`macsys status` 顯示 `mismatch`，Mac 不會啟動（以免把疊加層套到錯的映像上），`macsys reset` 清空並綁定到現在的映像。為什麼不算整個 1 GiB：每次開機要多讀 1 GiB；換映像幾乎一定改到頭（開機區塊、HFS 的 MDB）或尾（備份 MDB）。
- **斷電安全**（選擇：不用日誌，靠單一區塊的寫入是原子的）：
  - 已經改過的區塊：原地覆寫資料區的那一塊（一次單區塊寫入）。
  - 新的區塊：**先**寫資料到下一個空的資料區塊，**再**寫含有那個對照表項目的對照表區塊。在兩者之間斷電，只是多了一個沒人用的資料區塊，開機時重算「下一個空的」會再用到它；對照表永遠不會指向沒寫好的資料。
  - 重置：先寫帶 RESETTING 的標頭，再清空對照表，最後寫正常的標頭。中途斷電，下次開機看到 RESETTING 就把清空做完。
  - host 單元測試（`tests/host/test_overlay.c`）在每一個寫入點模擬斷電，檢查每個區塊不是舊值就是新值、成功的寫入是完整的。
- 對照表在開機時整個讀進記憶體（1 GiB 的系統 = 8 MiB），寫入時同時更新記憶體與磁碟。資料區用完時寫入失敗（Mac 看到寫入錯誤）；4 GiB 的疊加層可以容納 System 槽的每個區塊各改四次以上，實際上不會滿。
- 第一次開機時疊加層分割區是空的（全 0），kernel 自動格式化並綁定到目前的 System 槽。

## 4. 開機流程

```
UEFI 韌體 ── EFI/BOOT/BOOTX64.EFI（GRUB）
               └─ 內嵌的小設定：insmod part_gpt fat；search --file /68k_hal/grub.cfg；configfile
                    └─ /68k_hal/grub.cfg：load_env（grubenv）、選單（3 秒）
                         multiboot2 /68k_hal/slot_<槽>/kernel-gfx1440.elf  kernel_slot=… system_slot=… autostart=mac
                         module2 initrd.tar、mac_rom、68k_hal.cfg（module 名稱 config）
kernel ── 設定（第 5 節）── macdisk：GPT → 分割區 → 疊加層 → "system"、"apps"
       └─ autostart=mac → 執行 mac（Mac 停止（` 鍵）後回到 shell）
```

- 選單（`grub-mac.cfg`）：預設「68k_HAL (Mac)」；「maintenance shell」（autostart=shell）；「hardware diagnostics」；「keep the firmware's resolution」（kernel-gfx.elf，不要求解析度）；「the other kernel slot」。
- **只支援 UEFI**（GPT 上的 BIOS 開機要在裝置上寫 BIOS boot 分割區與 core.img，不做；CSM 仍可用 `make usb-image` 的隨身碟）。Secure Boot 要關（GRUB 沒簽章）。
- Mac 的磁碟：磁碟機 8 = `system`（疊加層），磁碟機 9 = `apps`（直接讀寫分割區）。.Disk 驅動支援 4 台（`docs/mac.md` 第 6 節）；ROM 只掛開機碟，其他的在 Finder 啟動後由模型注入 `_MountVol` 掛上。
- 沒有 68k_HAL 磁碟時照舊：GRUB module `mac_disk`（記憶體中的複本），或 ATA 的 `ata0`。

## 5. 設定檔 `68k_hal.cfg`

位置：Boot 分割區的 `/68k_hal/68k_hal.cfg`（在 Ubuntu 掛上 FAT32 分割區直接編輯，或用 `mcopy`）。GRUB 把它當成 boot module `config` 載入，kernel 不需要 FAT 驅動。格式：每行 `key = value`，`#` 之後是註解，前後空白忽略，後面的行覆蓋前面的。**kernel 命令列的 `key=value` 優先**（GRUB 選單就是這樣設定 `autostart` 與槽）。shell 的 `config` 指令列出目前的設定。

| key | 值 | 預設 | 說明 |
|---|---|---|---|
| `autostart` | `mac`、`shell` | `shell` | 開機後執行什麼（選單項目會設定） |
| `mac_res` | `WxH`、`auto` | 上次結束時的模式／填滿螢幕的模式 | Mac 的解析度（`mac res=`） |
| `mac_depth` | 1、2、4、8、16、32 | 上次結束時 | 色深 |
| `mac_scale` | `auto`、0.5、1、1.5、2 … | `auto` | 放大倍率（HiDPI） |
| `mac_cards` | 1、2 | 1 | NuBus 顯示卡數量 |
| `mac_video` | `card`、`builtin` | `card` | `builtin` = IIci 內建顯示（640x480） |
| `mac_ram` | MB，8–1024 | 128 | Mac 的記憶體（32 位元 kernel 最多 256） |
| `mac_disk` | `auto`、區塊裝置名稱 | `auto` | `auto` = 68k_HAL 磁碟的 System + Applications；裝置名稱（`disks` 列出）= 整顆當成唯一的磁碟 |
| `system_slot` | `a`、`b` | `a` | 用哪個 System 槽（GRUB 從 grubenv 傳入；第 8 節） |
| `kernel_slot` | `a`、`b` | — | 這次開機用的 kernel 槽（GRUB 傳入，只供顯示與第 8 節的確認） |
| `kernel_confirm` | `0`、`1` | `0` | GRUB 傳入：這次是試開，成功就確認（第 8 節） |

## 6. 重置與狀態

- `macsys status`：目前的 System 槽、疊加層狀態（ok／empty／mismatch／error）、已寫入與剩餘的區塊。
- `macsys reset`：清空疊加層（Mac 的系統碟回到原始映像），並綁定到目前的槽。Applications 不受影響。在 shell 執行（Mac 沒有在跑的時候）。
- `mount <裝置>` + `ls`／`cat`：用 HAL 的 HFS 讀取器看 `system`、`apps`、`sys_a` 等的內容。

## 7. 電源

- Mac 的「Shut Down」：ROM 的關機程序清除 RBV（VIA2 port B）bit 2（v2PowerOff）→ 模型停止 → kernel `hal_power_off()`（ACPI S5）。Mac 的「Restart」：ROM 重新開始 → kernel `hal_power_reboot()`。Mac 的寫入在 Mac 執行時就已經同步寫到磁碟。
- 主機的**電源鍵**（短按）：ACPI 的 fixed event（PM1 狀態暫存器的 PWRBTN_STS）。Mac 執行中 → 送 ADB 鍵盤的電源鍵 → System 7.5 顯示「Are you sure you want to shut down your computer now?」（Return = Shut Down）；在 shell 中 → 直接關機。長按 4 秒仍是硬體強制關機。細節：`platform/pc/power/hal_power.c`。
- 按 ` 停止 Mac：回到 shell（不關機）。

## 8. 非破壞性的系統升級（P10 D11）

比喻：兩個抽屜（A、B）各放一份 kernel，一張紙條（grubenv）寫著「現在用哪個抽屜」。更新時把新 kernel 放進沒在用的抽屜，紙條上加一行「下次試一次另一個抽屜」；試成功才把「現在用」改過去，試失敗就還是用舊抽屜。

### 8.1 kernel 的 A/B 槽

```
grubenv:  boot_slot=a   system_slot=a   trial=
                │              │            └─ 有值時：下一次開機試這個 kernel 槽一次
                │              └─ Mac 用哪個 System 槽（GRUB 傳給 kernel：system_slot=）
                └─ 確認過的 kernel 槽

GRUB（grub-mac.cfg）：
  trial 有值 → 這次用 trial 的槽，kernel_confirm=1，並且「先」把 trial 清空（save_env）
  預設項目載入失敗（檔案壞掉）→ fallback 項目：boot_slot 的 kernel
kernel：開機成功（設定、磁碟都好了，進入 Mac 或 shell 之前）且 kernel_confirm=1
  → 把 grubenv 的 boot_slot 改成這個槽
```

- 新 kernel 當掉或卡住：使用者按 reset，下次開機 trial 已經是空的 → 用舊的 boot_slot。
- 新 kernel 根本載入不了：GRUB 自己 fallback 到確認過的槽（`Falling back to '68k_HAL (Mac) - the confirmed kernel slot'`）。
- 選單的「the other kernel slot」只試一次，不會變成確認過的槽。
- kernel 寫 grubenv 的方法：`kernel/fs/fat.c` 在 FAT32 裡找到 `68k_hal/grubenv` 的磁區，`kernel/fs/grubenv.c` 改內容後**原地覆寫那兩個磁區**（檔案大小固定 1024 位元組，不配置新的叢集，所以不需要完整的 FAT32 寫入）。GRUB 自己的 `save_env` 也是這樣寫。
- `macsys status` 顯示這次的 kernel 槽、確認過的槽與待試的槽。

### 8.2 System 的 A/B 槽

- `macsys promote`：把「目前的 System 槽＋疊加層」（也就是 Mac 現在看到的系統碟）整個複製到另一個槽，讀回來比對 CRC-32，切換過去（grubenv `system_slot`），清空疊加層。舊槽不動。適合「系統設定與更新都弄好了，定版」。
- `macsys switch a|b`：改用另一個槽，疊加層清空並綁到新槽。疊加層裡有 Mac 的改動時會拒絕（`promote` 保留它們；`switch a force` 丟掉它們）——Mac 開機就會寫桌面資料庫等，所以通常需要 `force`。
- 兩者都在 shell 執行（Mac 停止後）；grubenv 寫不了時（例如沒有 Boot 分割區）這次開機已經切換，但下次開機還是舊槽，會印出說明。

### 8.3 Ubuntu 端的更新腳本 `tools/update_68khal.sh`

```bash
make all initrd                                         # 新的 kernel 與程式
sudo tools/update_68khal.sh --dry-run /dev/sda          # 先看會寫什麼
sudo tools/update_68khal.sh /dev/sda                    # kernel 寫到沒在用的槽，下次開機試
sudo tools/update_68khal.sh --no-kernel --system new_system.img /dev/sda   # 新的 Mac 系統放進沒在用的 System 槽
```

- 只寫 Boot 分割區裡備用槽的四個檔案與 grubenv（mtools 以分割區的位移存取 FAT32，不掛載），以及（`--system`）備用的 System 分割區。**疊加層、Applications、預留空間絕不寫。**
- 寫入前列出 SHA-256，寫入後讀回比對；`--dry-run` 什麼都不寫。目標必須是 68k_HAL 磁碟（五個分割區名稱都在），有分割區被掛載時拒絕。
- 目標也可以是映像檔（`build/68k_HAL_mac.img`）：自動測試只用這種方式（`make test-mac-update`）。

### 8.4 各種情境會保留什麼

| 情境 | 怎麼做 | Mac 的系統設定（疊加層） | Applications | 退回 |
|---|---|---|---|---|
| 更新 kernel | `update_68khal.sh` | 保留 | 保留 | 自動（試開失敗）或選單「the other kernel slot」 |
| 系統弄好了，定版 | `macsys promote` | 併進新槽（疊加層清空，內容在新槽裡） | 保留 | `macsys switch <舊槽> force` |
| 換新版 Mac 系統 | `update_68khal.sh --system`，再 `macsys switch <新槽> force` | 丟棄（屬於舊系統） | 保留 | `macsys switch <舊槽> force` |
| 套用 Apple 的更新程式 | 在 Mac 裡執行（寫進疊加層），滿意後 `macsys promote` | 更新在疊加層裡；promote 後在新槽 | 保留 | promote 前：`macsys reset`；promote 後：switch 回舊槽 |
| 重置系統 | `macsys reset` | 丟棄 | 保留 | — |

測試：`make test-mac-update`（本機，約 8 分鐘）：腳本更新 kernel 槽 b → 試開成功並確認、建兩個資料夾 → 腳本放壞的 kernel 到槽 a → GRUB fallback 到 b、資料夾都在 → `macsys promote` → 用 System B 開機、資料夾在 → `macsys switch a`（拒絕）、`switch a force` → System A 開得起來（沒有那個資料夾：它從來沒被寫過）。

## 9. 與第二階段（eHFS）的關係

- 剩餘空間不分割：之後加 eHFS 分割區時，在 GPT 加一個 entry 就好，不必移動現有的分割區。
- 磁碟層（`block_device_t`、`gpt_view_register`）與 .Disk 驅動（4 台磁碟機）都能再加磁碟。
- Applications 是暫時的 HFS；eHFS 透過 File System Manager 的外部檔案系統外掛給 Mac 用之後，Applications 可以保留當保險或搬過去。

## 10. 產生映像與測試

```bash
make mac-disk     # build/68k_HAL_mac.img（約 9 GiB 稀疏檔，實際約 63 MB）；MAC_ROM、MAC_DISK 指向你的檔案
make test-mac-disk   # 本機：小一號的同樣配置，在 QEMU（q35 + OVMF）跑兩次開機（第 7 節的流程），約 4 分鐘
make test-mac-update # 本機：更新、試開、退回、promote、switch（第 8 節），約 8 分鐘
make test-macdisk    # make test 裡：合成的小 GPT 映像（沒有 Mac 系統）接 AHCI，檢查分割區與疊加層
```

- `tools/make_mac_disk.py`：用 `sgdisk` 對**映像檔**建 GPT、`mformat`／`mcopy` 做 FAT32、`hformat` 做 Applications；拒絕寫入不是一般檔案的目標（例如 `/dev/sda`）。分割區大小可改：`make mac-disk MAC_DISK_SIZES="--overlay-mib 8192"`。
- `tools/mac_disk_check.py`：在 QEMU 裡開機、打字、按電源鍵、截圖的腳本工具。
- 寫入 `sda` 的步驟：`docs/p520.md` 第 7 節。

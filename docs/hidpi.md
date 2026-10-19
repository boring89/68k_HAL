# Mac 的 2x HiDPI：可行性量測（P11 K2）

只有量測，**沒有實作 2x 繪圖，也沒有做架構決定**（協作模式：架構由使用者決定，見最後一節）。

## 1. 要回答什麼

使用者 2026-10-18 選了「真正的 2x HiDPI」：QuickDraw 以兩倍精度輸出（文字用外框字型、線條銳利），外觀維持經典樣式。單純放大（現在的 `fb_blit_halves`）與像素畫濾鏡（Scale2x、xBRZ）都不是最終目標。

主對話提議的做法叫**影子畫面**（shadow screen）：

```
Mac 的程式 ──► QuickDraw trap ──┬─► ROM 的 QuickDraw 照畫 1x 畫面（相容性：還是 Apple 的程式）
                                  └─► 原生的 x86-64 繪圖程式看著同一個呼叫，另外畫一份 2x
螢幕上每個 32x32 的 tile：2x 有效 → 顯示 2x；QuickDraw 以外的寫入讓它失效 → 顯示放大的 1x
```

比喻：一位畫家（ROM）照原樣畫一張小圖，旁邊的助手（x86-64）看著他每一筆的指令，在大畫布上用細筆重畫一次。助手沒看到的筆畫（不是透過指令畫的），那一塊就只能把小圖放大貼上。最差的情況就是現在的畫質。

這個做法可不可行，要看三件事（另加一件與字型有關的）：

1. Mac OS 實際呼叫哪些 QuickDraw trap、各幾次。
2. 有多少畫面寫入**不在** QuickDraw trap 裡（助手看不到的筆畫）。
3. ROM 自己呼叫 QuickDraw 時，是經過 trap 分派程式，還是直接 `JSR`（直接呼叫的話，在 trap 這一層攔不到）。
4. Finder 與一般程式用了哪些字型與大小，有沒有外框（TrueType）版本。

## 2. 怎麼量

全部在 host 上（`tools/mac_run -H`，程式在 `tools/mac_measure.c`；kernel 不受影響，模型只多一個可選的 VRAM 寫入 hook `mac_t.vram_hook`）。

- **trap 分派**：Mac IIci ROM（368CADFE）的 A-line 例外處理程式在 ROM `0x408099B0`。Toolbox trap 從 `0x408099D6` 的 `RTS`（auto-pop 的是 `0x408099EC` 的 `RTD #4`）離開分派程式，OS trap 從 `0x40809A04`／`0x40809A22` 的 `JSR` 離開：下一個 PC 就是那個 trap 的程式（反組譯 ROM 得到：`mac_run -D 408099B0,70`）。從分派開始，到 PC 回到返回位址為止，算「在這個 trap 裡」。
- **VRAM 寫入**：NuBus 卡（slot `$C`）的每一次寫入分成四組：在 QuickDraw trap 裡（中斷等級 0）；在別的 trap 裡；中斷等級大於 0（游標由 VBL 中斷畫）；不在任何 trap 裡。每組也算「每幀碰到幾個 32x32 的 tile」。
- **繞過分派程式**：第一個 Toolbox trap 時讀 trap 表（`$0E00`，每個 trap 4 位元組），記下 QuickDraw 程式在 ROM 裡的進入點。之後只要有 `JSR`、`JMP`、`BSR`、`RTS`、`RTD` 跳到這些位址、而且不是從分派程式來的，就算一次「直接進入」，並依呼叫者在 ROM 或 RAM 分開計算。
- **字型**：DrawChar、DrawString、DrawText、StdText 被呼叫時，讀目前的 GrafPort（`thePort` = `(A5)` 指到的長字）的 `txFont`（偏移 68）、`txFace`（70）、`txSize`（74）（Inside Macintosh: Imaging With QuickDraw，GrafPort／CGrafPort 的欄位）。
- **哪些 trap 算 QuickDraw、trap 的名稱**：`tools/mac_traps.h`，由 Multiversal Interfaces（Retro68 的介面定義，MIT 授權，原授權聲明保留在檔案開頭）的 `QuickDraw.yaml`、`CQuickDraw.yaml` 產生。QuickDraw 不是一段連續的 trap 號碼（例如 `$A81F` 是 Resource Manager 的 Get1Resource，`$AA12` 是 Color QuickDraw 的 FillCRgn）。

**操作腳本**（`tools/mac_hidpi_session.py`，640x480、Millions，5100 幀約 85 秒）：開機到 Finder → Apple 選單 > Control Panels → View > by Name → 打 `ke` 開 Keyboard 控制台、點一個選項、關掉 → 拖動 Control Panels 視窗、捲動它的清單 → 按住 File 選單，沿著選單列滑過其他選單，在外面放開 → 打開系統碟的視窗 → 打 `si` 開 SimpleText，打兩行字 → Command-Q，Don't Save → 在桌面上移動游標。

重跑：`make mac-hidpi-measure`（本機，需要 `MAC_ROM`、`MAC_DISK`），結果在 `build/mac/hidpi_report.txt`。

## 3. 結果（2026-10-19，System 7.5.3）

整段：3.48 億個指令，257 萬次 trap 分派，其中 QuickDraw 29.4 萬次。

### 3.1 VRAM 寫入（19.2 MB）

| 分組 | 位元組 | 比例 | 平均 tile／幀 | 單幀最多 tile |
|---|---|---|---|---|
| 在 QuickDraw trap 裡 | 17,899,852 | 92.99% | 2.33 | 116 |
| 在別的 trap 裡 | 0 | 0% | — | — |
| 中斷等級 > 0 | 117,196 | 0.61% | 0.05 | 6 |
| 不在任何 trap 裡 | 1,231,940 | 6.40% | 0.19 | 40 |

（640x480 的畫面有 20 × 15 = 300 個 tile。）

- 「不在任何 trap 裡」的寫入**全部發生在第 7–41 幀**（開機的前 0.7 秒，ROM `0x4082F25E` 在畫開機畫面）。**Finder 啟動之後是 0**。
- 「中斷等級 > 0」是游標：ROM `0x4082EA18`、`0x4082E9AA`，在 VBL 中斷裡存回與畫上游標，每幀最多 6 個 tile。
- 所以開機之後，**除了游標，每一個寫到畫面的位元組都發生在一個經過分派程式的 QuickDraw trap 裡**。

寫入最多的 QuickDraw trap（最內層的那個）：

| trap | 位元組 | 性質 |
|---|---|---|
| EraseRect `$A8A3` | 4,607,360 | 以圖樣或顏色填滿 |
| FillCRgn `$AA12` | 3,120,116 | 以彩色圖樣填滿區域（例如桌面） |
| EraseRgn `$A8D4` | 3,070,296 | 填滿 |
| CopyBits `$A8EC` | 2,156,364 | **複製點陣圖**（圖示、離屏畫好的內容） |
| FillRgn `$A8D6` | 1,228,608 | 填滿 |
| DrawString `$A884` | 878,808 | 文字 |
| DrawPicture `$A8F6` | 545,224 | 播放 PICT（裡面又是各種操作） |
| FillRect、FrameRect、PaintRgn | 約 0.4 MB 各 | 填滿、框線 |
| ScrollRect `$A8EF` | 238,068 | **把畫面上的像素搬動** |
| CopyMask `$A817` | 163,472 | 複製點陣圖（加遮罩：圖示） |
| DrawText `$A885`、LineTo `$A891` | 137,660、99,472 | 文字、線 |

大致上：填滿與框線約 77%，複製點陣圖（CopyBits、CopyMask、ScrollRect）約 14%，文字約 6%，PICT 約 3%。

### 3.2 繞過分派程式

ROM 的程式直接進入 QuickDraw 程式 28,245 次，RAM 的程式 134,291 次（同一段時間 QuickDraw 分派 293,982 次）。

- **從 RAM 進入**的幾乎都是 System 的 trap 修補呼叫 ROM 原本的程式（例如 SetPort：分派 97,223 次，從 RAM 直接進入 ROM 的 SetPort 97,218 次）：這些呼叫已經在分派時看到了。
- **從 ROM 進入**的多半是工具函式：EmptyRect 8,421、Color2Index 4,594、StdTxMeas 2,695、SetRectRgn 2,355、RectRgn 1,920、CopyRgn 1,574、EqualRgn 1,159、TextWidth 1,005、PenPat 554。會畫東西的：LineTo 32 次、StdRect 5、StdRRect 2、StdText、StdBits、EraseRgn 各 1。
- 這是**下限**：只認得 trap 表裡的進入點。StdRect、StdText、StdBits 這些 bottleneck 從來沒有被分派過（0 次），卻只被直接進入了幾次，表示 QuickDraw 內部大多呼叫的是另一個進入點（例如跳過開頭的那一段），量不到。
- 與 3.1 合起來看：這些直接呼叫都發生在**另一個已經分派的 QuickDraw trap 裡**（開機後沒有任何 QuickDraw 以外的寫入）。所以**在「最外層的 QuickDraw trap」這一層攔截，看得到全部的繪圖**；但如果只攔個別的基本操作（例如只攔 LineTo 這個 trap），會漏掉 ROM 內部直接呼叫的那些。

### 3.3 字型

| 字型（txFont） | 大小（0 = 預設 12） | 樣式 | 次數 | 誰在用 |
|---|---|---|---|---|
| 0 Chicago（系統字型） | 0 | — | 298 | 選單、標題列、對話框 |
| 3 Geneva | 9 | — | 161 | Finder 的清單 |
| 3 Geneva | 12 | — | 69 | SimpleText 的內文 |
| 1（應用程式字型 = Geneva） | 9 | — | 10 | |
| 0 Chicago | 12 | — | 8 | |
| 3 Geneva | 9 | 底線 | 3 | 清單的欄位標題 |

使用者的 System 7.5.3 的 Fonts 資料夾裡，**每一套字型都同時有 TrueType（`sfnt`）與點陣字（`NFNT`）**：Chicago（sfnt 1、NFNT 1）、Geneva（1、8）、Monaco（1、2）、New York（1、7）、Courier、Helvetica、Palatino、Symbol、Times。QuickDraw 在有對應大小的點陣字時用點陣字（1x 畫面上看到的是點陣字）；2x 的繪圖程式可以在執行期讀使用者的 `sfnt`（**不能複製進 repo**），用兩倍的大小點陣化。

## 4. 對影子畫面的意義

- **可行**：開機後，所有寫到畫面的內容（游標除外）都在經過分派程式的 QuickDraw trap 裡。在 trap 分派這一層看得到全部；不需要修改 ROM 的 QuickDraw。
- **攔截的層級要選對**：要在最外層的 QuickDraw trap（或 GrafPort 的 bottleneck，`grafProcs`）攔，不能只攔個別基本操作（3.2）。
- **游標要另外處理**：游標在 VBL 中斷裡直接寫 VRAM（每幀最多 6 個 tile）。2x 的游標可以完全由 host 畫在最上層，不經過 Mac 的畫面。
- **難的是點陣圖**：約 14% 的寫入是複製點陣圖。來源如果是離屏畫好的內容（GWorld、離屏的 GrafPort），2x 版本要有那張離屏圖的 2x 影子才畫得清楚；來源如果是圖示（CopyMask、PlotIcon），1x 的圖示放大就只是放大（2x 的圖示要另外的原創圖，不能重畫 Apple 的圖示）。
- **ScrollRect** 搬動畫面上的像素：2x 的緩衝區要做同樣的搬動，否則捲動後的內容就失效（退回放大的 1x）。
- **工作量**：平均每幀只有 2–3 個 tile 改變，最多 116 個（開視窗、捲動）。tile 的有效／失效記錄很便宜。

## 5. 限制與還沒量到的

- 只量了 Finder、控制台、SimpleText。**直接寫 VRAM 的程式**（遊戲、影片播放、某些繪圖程式）沒有量到；它們會讓 tile 失效（退回放大的 1x），不會出錯。
- 只量了 640x480、Millions。其他色深走的 QuickDraw 程式不同（例如 1 位元走非彩色的路徑），但分派的結構一樣。
- 「在 trap 裡」用返回位址判斷；一個 trap 用別的方式離開時，以堆疊指標高過進入時為準（這次執行沒有留下這種情形：`stale frames dropped 0`）。
- 離屏繪圖（寫到不是 VRAM 的 PixMap）沒有分析：量到的只有寫進卡的 VRAM 的內容。
- trap 名稱來自 Multiversal Interfaces，不是 Apple 的 MPW 標頭。

## 6. 待決定（架構，使用者決定）

| # | 問題 | 選項 | 備註 |
|---|---|---|---|
| H1 | 在哪一層攔截 | 最外層的 QuickDraw trap／GrafPort 的 `grafProcs`（bottleneck）／個別 trap | 3.2：個別 trap 會漏掉 ROM 內部的直接呼叫 |
| H2 | 離屏繪圖 | 每個離屏 PixMap 都有 2x 影子／只追蹤畫面、離屏來的 CopyBits 一律退回 1x 放大 | 影響 14% 的點陣圖複製 |
| H3 | 文字 | 用使用者 System 的 `sfnt` 以 2x 點陣化（需要 TrueType 點陣化程式：自己寫或移植非 GPL-2.0-only 的實作）／先用 2x 放大的點陣字 | 所有字型都有 `sfnt` |
| H4 | 游標 | host 直接畫 2x 游標／照現在放大 | 游標在 VBL 中斷裡寫 VRAM |
| H5 | 圖示 | 放大／之後做原創的 2x 圖示 | 不能重畫 Apple 的圖示 |

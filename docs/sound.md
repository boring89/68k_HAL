# 聲音：Intel HD Audio

`drivers/hda/`、`kernel/sound/`

## 1. 概念

比喻：電腦裡的聲音是一條水管。**程式**把數字（取樣，sample）倒進水桶（記憶體緩衝區）；**HDA 控制器**像抽水機，用 DMA 自己把桶裡的數字一個一個抽走，送到晶片（**codec**，裡面的 DAC 把數字變成電壓），再經過音量旋鈕（amp）到插孔（pin）。

```
 記憶體環形緩衝區 ──DMA──► 控制器的 stream ──HDA 連結──► codec: DAC ─► (混音器) ─► pin ─► 喇叭
        ▲                       │ LPIB = 目前讀到哪
        └─ 驅動隨時補新資料       └─ RUN 位元啟動／停止
```

- **取樣率與格式**：固定 48000 Hz、16 位元有號、雙聲道（`SOUND_RATE`）。格式字（stream format）= `0x0011`（HDA 規格 3.7.1：基礎 48 kHz，16 位元，2 聲道）。
- **codec 的樹狀結構**：根節點 0 → function group（音訊功能）→ widget 們（audio output = DAC、pin complex = 插孔、mixer、selector…）。每個 widget 有 NID（編號），用「verb」（32 位元命令）詢問或設定。`GET_PARAMETER`（0xF00）取得種類、數量、能力；`GET_CONNECTION_LIST`（0xF02）知道某個 widget 的訊號來自誰。

## 2. 驅動的步驟（`drivers/hda/hda.c`）

資料來源：Intel High Definition Audio Specification 1.0a。

1. PCI 比對：class 4／subclass 3。開啟記憶體解碼與 bus master，映射 BAR0。
2. **重置控制器**：`GCTL.CRST` 設 0、等待、再設 1；等 codec 報到（`STATESTS` 位元 n = codec n 在線）。
3. **命令通道**：兩種：
   - CORB（命令環）／RIRB（回應環）：兩個 DMA 環，驅動寫 CORB 寫指標，控制器把命令送給 codec，回應寫進 RIRB。
   - 立即命令暫存器（`ICOI`/`ICII`/`ICIS`）：寫 verb、設 busy、等 valid、讀回應，一次一個。
   QEMU 的控制器透過環只回應第一個命令（之後不再取命令，原因未查明；我們有清除 RIRB 狀態旗標），所以驅動先試環、第二個命令逾時就改用立即命令。
4. **找輸出路徑**：掃 function group 的 widget，找 pin 設定預設值（`GET_CONFIG_DEFAULT`）表示有接線的 line out／speaker／headphone，順著 connection list 找到 audio output（直接或經一層 mixer／selector）。
5. **設定路徑**：電源 D0；各 widget 的輸出／輸入 amp 取消靜音並設 0 dB（`SET_AMP`）；DAC 設 stream tag 1、channel 0、格式；pin 開輸出（`SET_PIN_CONTROL` 0x40）與 EAPD。
6. **輸出 stream**：stream descriptor 位址 = 0x80 + 輸入 stream 數 × 0x20。重置（SRST）、寫 stream tag、環形緩衝區長度（CBL）、最後有效索引（LVI）、格式、BDL（buffer descriptor list）位址。BDL 是 8 個 16 KiB 的項目，指向同一塊 128 KiB 的 DMA 記憶體。
7. **播放**（`hda_play`）：先把前 128 KiB 填進環，開 RUN；持續讀 `LPIB`（硬體讀到的位置），把剛被讀完的區域用後面的資料補上；全部資料被讀走後停止。

## 3. kernel 端（`kernel/sound/`）

- `sound_register`／`sound_get`：驅動註冊 `sound_device_t{play}`。
- `sound_beep(Hz, ms, volume)`：產生方波（5 ms 淡入淡出避免爆音），純函式 `sound_make_tone`。
- `sound_resample_u8_mono`：把 8 位元無號單聲道（Mac 的聲音格式）轉成 48 kHz 16 位元雙聲道。
- shell 指令 `beep [Hz [毫秒]]`。

## 4. 測試

- host：`tests/host/test_sound.c`（頻率、音量、淡入淡出、重新取樣）。
- `make test-qemu`：QEMU 帶 `-audiodev wav,id=snd0,path=build/test/sound.wav -device intel-hda -device hda-output,audiodev=snd0`；selftest 播 440 Hz 500 ms；結束後 `tools/check_wav.py` 檢查錄下的檔案不是靜音、頻率約 440 Hz。

## 5. 限制

- 只有輸出，沒有錄音；只用第一個 codec 的第一個輸出路徑；不用中斷（輪詢）。
- 播放是阻塞的（播完才返回）。
- 沒有音量控制（固定 0 dB amp；音量在產生樣本時乘上去）。

## 6. 串流與 Mac 的聲音

`sound_device_t.write(samples, frames)` 是非阻塞的串流介面：把取樣排在目前播放內容的後面，立刻返回，回傳實際接受的 frame 數（佇列滿就少於要求）。HDA 的環形緩衝區（128 KiB，約 0.68 秒）用「已播放位元組數」與「已寫入位元組數」兩個絕對計數管理：每次呼叫先讀 `LPIB` 更新已播放數，並把剛播完的區域清成 0（驅動來不及時播靜音而不是重播舊聲音）；來不及時（控制器超過了寫入點）寫入點跳到播放點之後 21 ms。第一次呼叫時啟動串流，之後一直播（沒資料就是靜音）。`hda_play`（阻塞）就是用 `write` 加上等待。

Mac 的 ASC 輸出透過這個介面播出，見 `docs/mac.md` 第 11 節。

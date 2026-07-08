# Firmware archive

Prebuilt, flashable firmware images for the **ESP32-C3 Super Mini** build of
Clawd Mochi. Each version folder holds the binaries needed to flash that exact
build without recompiling, so you can switch between versions at any time.

> All builds target board `ESP32C3 Dev Module` with **USB CDC On Boot = Enabled**
> (full FQBN: `esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600`).

## Versions

| Version | Source tag | What's in it |
| ------- | ---------- | ------------ |
| `v1.0.0-base` | `v1.0.0` | Original upstream firmware + **ST7789 SPI mode 3** display fix (this panel shows a black screen on the default mode 0). Two eye views (Normal, Squish), animate once on selection. |
| `v1.1.0-emotions` | `v1.1.0` | **10 live looping eye emotions** (Normal, Squish, Sleepy, Angry, Surprised, Sad, Wink, Love, Dizzy, Suspicious), non-blocking animation engine, and a **15 s auto-cycle** mode. Web controller gets an emotion grid + auto-cycle toggle. |
| `limits-v1.0.0` | `limits-v1.0.0` | **Claude usage-limits display** (a different firmware, not the eyes): shows session (5h) + weekly % with a coloured bar and reset countdown, fed over USB by the [claude-limits](https://github.com/Antony-A-tech/claude-limits) browser extension + a small local helper. After ~90 s with no data it dims and cycles the v1.0.0 eyes. Source: [`../clawd_limits/`](../clawd_limits). |
| `limits-v1.1.0` | `limits-v1.1.0` | Limits display **+ desktop-lock eyes** (lock your PC with Win+L → the crab shows the v1.0.0 eyes until you unlock, detected by the helper via the WTS session flag) **+ a hang-watchdog**: an independent `esp_timer` reboots the board if `loop()` stalls >10 s, auto-recovering from a rare silent USB-CDC freeze, with the last reset cause shown tiny on screen. Firmware version now shown on the waiting screen. Source: [`../clawd_limits/`](../clawd_limits). |
| `limits-v1.1.0-panelfix` | `limits-v1.1.0-panelfix` | **Hardware-specific offshoot of `limits-v1.1.0`** — *not* a successor, a variant for units whose **1.3" ST7789 panel intermittently freezes** (the panel stops responding to SPI while the CPU keeps running fine — a bad-panel fault; the standard 1.54" build is unaffected and does **not** need this). Adds: SPI clock lowered **40→20 MHz**; a **`REINIT`** command that re-initialises a frozen panel *without* a reboot (the helper's "Reconnect / fix display" menu sends it); a loop-task watchdog + hard-reset backup + on-screen/NVS reset forensics. Run the helper with **`--auto-reinit`** for an automatic hourly re-init. Source: [`../clawd_limits/`](../clawd_limits). |

Each folder contains the four flashable images. The eye firmwares use
`clawd_mochi.ino.*`; the limits firmware uses `clawd_limits.ino.*` (plus the shared `boot_app0.bin`).

## How to flash a version

Connect the board over USB-C, find its port (`arduino-cli board list` → e.g. `COM6`), then:

```sh
arduino-cli upload \
  -b esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600 \
  -p COM6 \
  --input-dir firmware/v1.1.0-emotions
```

Swap the `--input-dir` path for whichever version you want. After flashing, the
board hard-resets into that firmware.

> ℹ️ The display panel on this build **requires SPI mode 3** — every archived
> version already includes that fix. Don't flash a pre-`v1.0.0` build or the
> screen will stay black.

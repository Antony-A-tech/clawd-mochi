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

Each folder contains: `clawd_mochi.ino.bin` (app), `clawd_mochi.ino.bootloader.bin`,
`clawd_mochi.ino.partitions.bin`, `boot_app0.bin`.

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

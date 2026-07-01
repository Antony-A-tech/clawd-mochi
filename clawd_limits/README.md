# Clawd Mochi — Claude limits display

A separate firmware for the same ESP32-C3 + ST7789 crab: instead of the animated
eyes, it shows your **Claude usage limits** — session (5h) and weekly utilization
with a big number, a coloured bar and a reset countdown.

```
claude.ai (your session)  ──▶  browser extension  ──▶  http://127.0.0.1:7654 (helper)  ──▶  USB ──▶  🦀
```

The crab is a "dumb" display: it just draws whatever CSV line arrives over USB serial.
All the data comes from the **[claude-limits browser extension](https://github.com/Antony-A-tech/claude-limits)**,
relayed by the small local `helper/helper.py`.

## What it shows

- **Boot:** the v1.0.0 "Clawd Mochi" logo reveal, then `waiting for PC data…`
- **Live:** `SESSION 5H` big %, a level-coloured bar, `week %`, and `reset …`
- **Offline** (no data for ~90 s): the screen dims and the crab "comes alive" — it
  cycles `offline → v1.0.0 normal eyes → squish eyes → offline …` until data returns.

Rendering uses an offscreen `GFXcanvas16` buffer pushed in one blit, so nothing flickers.

## Serial protocol

One line per update, newline-terminated:

```
<session%>,<weekly%>,<reset>\n     e.g.   14,12,4h55m
```

## Build & flash (Arduino CLI)

Board `ESP32C3 Dev Module`, **USB CDC On Boot = Enabled**:

```
FQBN=esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600
arduino-cli compile -b $FQBN -u -p <COMx> clawd_limits.ino
```

Or flash the prebuilt binaries from [`../firmware/limits-v1.0.0/`](../firmware) without recompiling.

> This panel needs **`SPI_MODE3`** (already set in `setup()`); on the default mode 0 it stays black.

## Use it

1. Flash this firmware to the crab and plug it into the PC over USB.
2. Install the **claude-limits** browser extension and sign in to claude.ai.
3. Run the helper: double-click `helper/start-helper.vbs` (tray app), or
   `pythonw helper/helper.py COM6`. It bridges the extension to the crab and
   re-sends the last value every 30 s.

/*
 * Claude limits display — Clawd Mochi (ESP32-C3 + ST7789)
 *
 * Boot: the v1.0.0 "Clawd Mochi" splash + crab logo reveal (logo_boot.h),
 * then "waiting for data" until the PC helper sends limits over USB serial.
 *
 * Serial line (CSV):  <sessionUsed%>,<weeklyUsed%>,<reset>\n   e.g.  12,8,2h41m
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include "logo_boot.h"
#include "esp_timer.h"
#include "esp_system.h"

#define TFT_CS  4
#define TFT_DC  1
#define TFT_RST 2
#define TFT_BLK 3
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

#define DISP_W 240
#define DISP_H 240

#define FW_VERSION "1.1.0"            // limits firmware: lock-mode eyes + hang-watchdog

uint16_t C_ORANGE, C_BLACK_, C_TRACK, C_BOOT;
uint16_t C_GRAY_BG, C_GRAY_FG, C_GRAY_FILL;
uint16_t FG;                          // current text colour (clay vs offline-grey)
int    sPct = -1, wPct = -1;          // -1 = no data yet
String resetTxt = "--";
bool   haveData = false;
String buf;

unsigned long lastDataMs = 0;         // millis() of the last received line
bool   offline = false;               // no fresh data for a while -> show stale
const unsigned long STALE_MS = 90000; // 90 s = 3 missed 30 s re-sends

// Offline "alive" cycle: offline screen -> v1.0.0 eyes -> squish -> repeat
uint8_t  offlinePhase   = 0;          // 0 = offline screen, 1 = normal eyes, 2 = squish
unsigned long offlinePhaseMs = 0;
const unsigned long PHASE_MS = 10000; // 10 s per phase

// Desktop-locked mode: eyes only (normal <-> squish), driven by the PC helper
bool     locked = false;
uint8_t  lockEye = 0;                 // 0 = normal eyes, 1 = squish
unsigned long lockPhaseMs = 0;

// v1.0.0 eye geometry (ported verbatim)
#define EYE_W   30
#define EYE_H   60
#define EYE_GAP 120
#define EYE_OX  0
#define EYE_OY  40

// ── hang-watchdog + crash forensics ──────────────────────────────
// Breadcrumb of WHERE loop() was; RTC memory survives a WD/SW reset (not power-off).
RTC_NOINIT_ATTR uint32_t g_phase;     // last phase loop() ran (see PH_*)
RTC_NOINIT_ATTR uint32_t g_magic;     // PH_MAGIC once booted -> RTC vars are valid
RTC_NOINIT_ATTR uint32_t g_resets;    // reboots since the last real power-on
#define PH_MAGIC  0xC1A0D0EE
#define PH_IDLE   1                   // between polls
#define PH_SERIAL 2                   // reading/parsing USB-CDC
#define PH_BLIT   5                   // pushing a frame over SPI to the panel
#define PH_ANIM   6                   // offline eye animation
volatile uint32_t g_beat = 0;         // loop heartbeat; watched by the esp_timer WD
String bootInfo = "";                 // "<reason> ph:<n> #<resets>" after a reset, else ""

// Offscreen buffer — draw the whole frame here, push it in one SPI blit.
// Kills the redraw flicker (no fillScreen flash on the live display).
GFXcanvas16 *cv = nullptr;
void blit() { g_phase = PH_BLIT; tft.drawRGBBitmap(0, 0, cv->getBuffer(), DISP_W, DISP_H); }

// Independent hang-watchdog: this runs in the esp_timer task, so it still fires
// even when loop() is wedged (a stuck USB-CDC or SPI call). No heartbeat for
// ~10 s -> reboot. Unlike the panic-based task WDT, esp_restart() can't get
// stuck printing a backtrace to a dead console.
void wdCheck(void*) {
  static uint32_t last = 0; static uint8_t stalls = 0;
  if (g_beat == last) { if (++stalls >= 5) esp_restart(); }   // 5 x 2 s
  else { stalls = 0; last = g_beat; }
}

uint16_t levelColor(int p) {
  if (p < 50) return tft.color565(60, 200, 100);   // green
  if (p < 75) return tft.color565(235, 205, 60);    // yellow
  if (p < 90) return tft.color565(240, 150, 40);    // amber
  return tft.color565(235, 60, 55);                 // red
}

// label helper — built-in 5x7 font (smaller than 9pt mono, fits the case window).
// y is given as a baseline (like the big number), so shift up for top-left font.
void label(int x, int y, const String& s) {
  cv->setFont(&FreeMonoBold12pt7b);
  cv->setTextSize(1);                 // reset multiplier (boot logo left it at 2/3!)
  cv->setTextColor(FG);
  cv->setCursor(x, y);
  cv->print(s);
}

// tiny diagnostic line (built-in 5x7) — shows the last reset cause+phase after a reboot
void diag() {
  if (!bootInfo.length()) return;
  cv->setFont(NULL);
  cv->setTextSize(1);
  cv->setTextColor(FG);
  cv->setCursor(2, 2);
  cv->print(bootInfo);
}

void drawWaiting() {
  FG = C_BLACK_;
  cv->fillScreen(C_ORANGE);
  label(14,36, "claude limits");
  label(14,120, "waiting for");
  label(14,152, "PC data...");
  diag();
  // firmware version, tiny, bottom-left
  cv->setFont(NULL); cv->setTextSize(1); cv->setTextColor(C_BLACK_);
  cv->setCursor(2, DISP_H - 10); cv->print("v" FW_VERSION);
  blit();
}

void drawStats() {
  FG = offline ? C_GRAY_FG : C_BLACK_;
  cv->fillScreen(offline ? C_GRAY_BG : C_ORANGE);
  label(14,30, "session 5h");

  // big centred percentage (monospace)
  char big[8]; snprintf(big, sizeof(big), "%d%%", sPct);
  cv->setTextSize(1);                  // reset multiplier before the custom font
  cv->setFont(&FreeMonoBold24pt7b);
  int16_t bx1, by1; uint16_t bw, bh;
  cv->getTextBounds(big, 0, 0, &bx1, &by1, &bw, &bh);
  int bx = (DISP_W - (int)bw) / 2 - bx1;
  cv->setTextColor(FG);
  cv->setCursor(bx, 104);
  cv->print(big);

  // usage bar on a dark track
  const int tx = 16, ty = 126, tw = DISP_W - 32, th = 32;
  cv->fillRoundRect(tx, ty, tw, th, 7, C_TRACK);
  int p = sPct < 0 ? 0 : (sPct > 100 ? 100 : sPct);
  int fw = (tw - 6) * p / 100;
  if (fw > 0) cv->fillRoundRect(tx + 3, ty + 3, fw, th - 6, 4, offline ? C_GRAY_FILL : levelColor(sPct));

  // footer: weekly + reset (or offline notice when data is stale)
  char w[20]; snprintf(w, sizeof(w), "week %d%%", wPct);
  label(14,190, w);
  label(14,222, offline ? String("offline") : ("reset " + resetTxt));
  diag();
  blit();
}

// ── v1.0.0 eye animation, ported verbatim (eyes BLACK on the boot red,
//    drawn to the offscreen buffer so there is no flicker) ──
inline int16_t eyeLX(int16_t ox) { return (DISP_W - (EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox; }
inline int16_t eyeRX(int16_t ox) { return eyeLX(ox) + EYE_W + EYE_GAP; }
inline int16_t eyeY()            { return (DISP_H - EYE_H) / 2 - EYE_OY; }
inline int16_t eyeCY()           { return eyeY() + EYE_H / 2; }

void drawNormalEyes(int16_t ox, bool blink) {
  cv->fillScreen(C_BOOT);
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeY();
  if (!blink) {
    cv->fillRect(lx, ey, EYE_W, EYE_H, C_BLACK_);
    cv->fillRect(rx, ey, EYE_W, EYE_H, C_BLACK_);
  } else {
    cv->fillRect(lx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK_);
    cv->fillRect(rx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK_);
  }
  blit();
}

void drawChevron(int16_t cx, int16_t cy, int16_t arm, int16_t reach,
                 uint8_t thk, bool rightFacing, uint16_t col) {
  for (int8_t t = -(int8_t)thk; t <= (int8_t)thk; t++) {
    if (rightFacing) {
      cv->drawLine(cx - reach/2, cy - arm + t, cx + reach/2, cy + t,       col);
      cv->drawLine(cx + reach/2, cy + t,       cx - reach/2, cy + arm + t, col);
    } else {
      cv->drawLine(cx + reach/2, cy - arm + t, cx - reach/2, cy + t,       col);
      cv->drawLine(cx - reach/2, cy + t,       cx + reach/2, cy + arm + t, col);
    }
  }
}

void drawSquishEyes(bool closed) {
  cv->fillScreen(C_BOOT);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  const int16_t arm = EYE_H / 2, reach = EYE_W / 2;
  const int16_t lcx = lx + EYE_W / 2, rcx = rx + EYE_W / 2;
  if (!closed) {
    drawChevron(lcx, cy, arm, reach, 10, true,  C_BLACK_);
    drawChevron(rcx, cy, arm, reach, 10, false, C_BLACK_);
  } else {
    cv->fillRect(lx, cy - 5, EYE_W, 10, C_BLACK_);
    cv->fillRect(rx, cy - 5, EYE_W, 10, C_BLACK_);
  }
  blit();
}

// v1.0.0 default speed = "slow" (animSpeed 1 -> every delay doubled)
void animNormalEyes() {
  const int16_t offs[] = {-16, 16, -16, 16, 0};
  for (uint8_t i = 0; i < 5; i++) { drawNormalEyes(offs[i], false); delay(160); }
  drawNormalEyes(0, true);  delay(200);
  drawNormalEyes(0, false); delay(140);
  drawNormalEyes(0, true);  delay(140);
  drawNormalEyes(0, false);
}

void animSquishEyes() {
  for (uint8_t i = 0; i < 3; i++) {
    drawSquishEyes(false); delay(320);
    drawSquishEyes(true);  delay(200);
  }
  drawSquishEyes(false);
}

void parseLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  if (line == "LOCK") {                            // PC desktop locked -> eyes-only mode
    if (!locked) { locked = true; lockEye = 0; lockPhaseMs = millis(); }
    return;
  }
  int c1 = line.indexOf(',');
  int c2 = line.indexOf(',', c1 + 1);
  if (c1 <= 0 || c2 <= c1) return;                 // ignore malformed line (no serial TX)
  sPct = line.substring(0, c1).toInt();
  wPct = line.substring(c1 + 1, c2).toInt();
  resetTxt = line.substring(c2 + 1); resetTxt.trim();
  haveData = true;
  lastDataMs = millis();
  offline = false;
  locked = false;                       // any data line unlocks (PC is back)
  drawStats();
}

void setup() {
  // ── forensics: why did we last reset, and where was loop() stuck? ──
  esp_reset_reason_t rr = esp_reset_reason();
  if (g_magic == PH_MAGIC && rr != ESP_RST_POWERON) {
    const char* r;
    switch (rr) {
      case ESP_RST_TASK_WDT: r = "TWDT";  break;   // task watchdog
      case ESP_RST_INT_WDT:  r = "IWDT";  break;   // interrupt watchdog
      case ESP_RST_WDT:      r = "WDT";   break;   // other watchdog
      case ESP_RST_PANIC:    r = "PANIC"; break;   // crash / exception
      case ESP_RST_BROWNOUT: r = "BROWN"; break;   // voltage sag (power/hw!)
      case ESP_RST_SW:       r = "SW";    break;   // our esp_restart() (hang WD)
      default:               r = "rst?";  break;
    }
    g_resets++;
    bootInfo = String(r) + " ph:" + String(g_phase) + " #" + String(g_resets);
  } else {
    g_resets = 0;                                  // fresh power-on
  }
  g_magic = PH_MAGIC;
  g_phase = PH_IDLE;

  Serial.begin(115200);
  pinMode(TFT_BLK, OUTPUT); digitalWrite(TFT_BLK, HIGH);
  SPI.begin(8, -1, 10, TFT_CS);
  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(40000000);
  tft.setRotation(1);
  tft.setTextWrap(false);
  C_ORANGE = tft.color565(217, 119, 87);   // Claude clay (limits screen)
  C_BLACK_ = tft.color565(20, 16, 14);      // near-black
  C_TRACK  = tft.color565(34, 24, 18);      // dark bar track
  C_BOOT   = tft.color565(218, 17, 0);      // v1.0.0 boot orange
  C_GRAY_BG   = tft.color565(46, 42, 39);   // offline: dimmed background
  C_GRAY_FG   = tft.color565(150, 142, 135);// offline: muted text
  C_GRAY_FILL = tft.color565(95, 90, 85);   // offline: grey bar
  FG = C_BLACK_;

  cv = new GFXcanvas16(DISP_W, DISP_H);     // offscreen frame buffer (anti-flicker)
  cv->setTextWrap(false);

  // ── Boot splash (as in v1.0.0) ──
  tft.fillScreen(C_BOOT);
  tft.setFont(); tft.setTextColor(0xFFFF); tft.setTextSize(3);
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 - 22); tft.print("Clawd");
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 + 14); tft.print("Mochi");
  delay(1200);

  // ── Logo reveal (from firmware v1.0.0) ──
  animLogoReveal(tft, C_BOOT, 0xFFFF);

  // ── Then wait for real data ──
  drawWaiting();

  // Start the independent hang-watchdog (fires from the esp_timer task even if loop() wedges)
  static esp_timer_handle_t wdTimer;
  esp_timer_create_args_t wdArgs = { .callback = &wdCheck, .arg = nullptr,
                                     .dispatch_method = ESP_TIMER_TASK, .name = "hangwd" };
  esp_timer_create(&wdArgs, &wdTimer);
  esp_timer_start_periodic(wdTimer, 2000000);   // check every 2 s; reboot after ~10 s stall
}

void loop() {
  g_beat++;                                         // heartbeat for the hang-watchdog
  g_phase = PH_SERIAL;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { parseLine(buf); buf = ""; }
    else if (c != '\r' && buf.length() < 120) { buf += c; }
  }
  g_phase = PH_IDLE;
  // Desktop locked -> eyes only (normal <-> squish), no offline text, until data returns
  if (locked) {
    if (millis() - lockPhaseMs >= PHASE_MS) { lockEye ^= 1; lockPhaseMs = millis(); }
    g_phase = PH_ANIM;
    if (lockEye == 0) animNormalEyes(); else animSquishEyes();
    delay(600);
    return;
  }
  // No fresh data for STALE_MS -> enter the offline "alive" cycle
  if (haveData && !offline && (millis() - lastDataMs > STALE_MS)) {
    offline = true;
    offlinePhase = 0;
    offlinePhaseMs = millis();
    drawStats();                         // phase 0: dimmed offline screen
  }
  // While offline: offline screen (10s) -> normal eyes (10s) -> squish (10s) -> loop
  if (offline) {
    if (millis() - offlinePhaseMs >= PHASE_MS) {
      offlinePhase = (offlinePhase + 1) % 3;
      offlinePhaseMs = millis();
      if (offlinePhase == 0) drawStats();
    }
    if (offlinePhase == 1)      { g_phase = PH_ANIM; animNormalEyes(); delay(600); }
    else if (offlinePhase == 2) { g_phase = PH_ANIM; animSquishEyes(); delay(600); }
  }
}

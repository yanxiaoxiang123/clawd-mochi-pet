/*
 * ╔══════════════════════════════════════════════════════════════╗
 *   CLAWD MOCHI — No-WiFi Demo Mode (6 expressions)
 *
 *   Same hardware as clawd_mochi.ino (ESP32-C3 Super Mini + ST7789
 *   1.54" 240x240). WiFi / WebServer / HTML stripped.
 *
 *   Cycles forever through 6 cute expressions:
 *     0. Normal eyes    — square eyes, wiggle + blink
 *     1. Squish eyes    — > <  chevron, open / close
 *     2. Heart eyes     — ♥ ♥ grow + pulse
 *     3. Sparkle eyes   — ★ ★ appear + twinkle
 *     4. Wink           — ◡ ‿  left eye closes
 *     5. Sleepy         — — —  eyes close + Z floats up
 *
 *   Wiring: identical to the WiFi version (see clawd_mochi.ino).
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <math.h>

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS   4
#define TFT_DC   1
#define TFT_RST  2
#define TFT_BLK  3

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ── Display ───────────────────────────────────────────────────
#define DISP_W 240
#define DISP_H 240

// ── Eye constants ─────────────────────────────────────────────
#define EYE_W   30
#define EYE_H   60
#define EYE_GAP 120
#define EYE_OX  0
#define EYE_OY  40

// ── Colours ───────────────────────────────────────────────────
uint16_t C_ORANGE, C_DARKBG, C_MUTED, C_GREEN;
#define C_WHITE ST77XX_WHITE
#define C_BLACK ST77XX_BLACK

// ── Demo state ────────────────────────────────────────────────
#define DEMO_NORMAL  0
#define DEMO_SQUISH  1
#define DEMO_HEART   2
#define DEMO_SPARKLE 3
#define DEMO_WINK    4
#define DEMO_SLEEPY  5
#define DEMO_COUNT   6

#include "dynamics.h"
#include "dynamics-speech.h"

uint16_t animBgColor = 0;
uint16_t drawBgColor = 0;
uint8_t  animSpeed   = 1;   // 1=slow, 2=normal, 3=fast

// ═════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════

int speedMs(int ms) {
  if (animSpeed == 3) return ms / 2;
  if (animSpeed == 1) return ms * 2;
  return ms;
}

void setBacklight(bool on) {
  digitalWrite(TFT_BLK, on ? HIGH : LOW);
}

void initColours() {
  C_ORANGE = tft.color565(218, 17, 0);
  C_DARKBG = tft.color565(10,  12,  16);
  C_MUTED  = tft.color565(90,  88,  86);
  C_GREEN  = tft.color565(80, 220, 130);
  animBgColor = C_ORANGE;
  drawBgColor = C_ORANGE;
}

inline int16_t eyeLX(int16_t ox) {
  return (DISP_W - (EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox;
}
inline int16_t eyeRX(int16_t ox) { return eyeLX(ox) + EYE_W + EYE_GAP; }
inline int16_t eyeY()            { return (DISP_H - EYE_H) / 2 - EYE_OY; }
inline int16_t eyeCY()           { return eyeY() + EYE_H / 2; }

// ═════════════════════════════════════════════════════════════
//  1. NORMAL EYES  (square black blocks)
// ═════════════════════════════════════════════════════════════

void drawNormalEyes(int16_t ox = 0, bool blink = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeY();
  if (!blink) {
    tft.fillRect(lx, ey, EYE_W, EYE_H, C_BLACK);
    tft.fillRect(rx, ey, EYE_W, EYE_H, C_BLACK);
  } else {
    tft.fillRect(lx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
    tft.fillRect(rx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
  }
}

// ═════════════════════════════════════════════════════════════
//  2. SQUISH EYES  (> <  chevron)
// ═════════════════════════════════════════════════════════════

void drawChevron(int16_t cx, int16_t cy, int16_t arm, int16_t reach,
                 uint8_t thk, bool rightFacing, uint16_t col) {
  for (int8_t t = -(int8_t)thk; t <= (int8_t)thk; t++) {
    if (rightFacing) {
      tft.drawLine(cx - reach/2, cy - arm + t, cx + reach/2, cy + t,      col);
      tft.drawLine(cx + reach/2, cy + t,       cx - reach/2, cy + arm + t, col);
    } else {
      tft.drawLine(cx + reach/2, cy - arm + t, cx - reach/2, cy + t,      col);
      tft.drawLine(cx - reach/2, cy + t,       cx + reach/2, cy + arm + t, col);
    }
  }
}

void drawSquishEyes(bool closed = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  const int16_t arm   = EYE_H / 2;
  const int16_t reach = EYE_W / 2;
  const int16_t lcx   = lx + EYE_W / 2;
  const int16_t rcx   = rx + EYE_W / 2;
  if (!closed) {
    drawChevron(lcx, cy, arm, reach, 10, true,  C_BLACK);
    drawChevron(rcx, cy, arm, reach, 10, false, C_BLACK);
  } else {
    tft.fillRect(lx, cy - 5, EYE_W, 10, C_BLACK);
    tft.fillRect(rx, cy - 5, EYE_W, 10, C_BLACK);
  }
}

// ═════════════════════════════════════════════════════════════
//  3. HEART EYES  (♥ ♥)  — two circles + triangle per heart
// ═════════════════════════════════════════════════════════════

void drawHeartEyeAt(int16_t cx, int16_t cy, uint16_t col, float scale) {
  // scale = 0.0 → invisible, 1.0 → full size, 1.15 → pulse
  // Sized to fill ~40×40 px at scale=1 (bigger than normal eyes for impact)
  if (scale <= 0.01) return;
  int r     = (int)roundf(9  * scale);   // bump radius (was 5)
  int dx    = (int)roundf(9  * scale);   // bump offset (was 5)
  int dyOff = (int)roundf(5  * scale);   // bump vertical offset (was 3)
  int halfW = (int)roundf(17 * scale);   // triangle half-width (was 10)
  int halfH = (int)roundf(20 * scale);   // triangle height (was 12)
  if (r < 1) r = 1;
  // Two bumps
  tft.fillCircle(cx - dx, cy - dyOff, r, col);
  tft.fillCircle(cx + dx, cy - dyOff, r, col);
  // Bottom triangle — raised start so bumps and triangle overlap nicely
  tft.fillTriangle(cx - halfW, cy - 2, cx + halfW, cy - 2, cx, cy + halfH, col);
}

void drawHeartEyes(float scale) {
  tft.fillScreen(animBgColor);
  drawHeartEyeAt(eyeLX(0) + EYE_W / 2, eyeCY(), C_BLACK, scale);
  drawHeartEyeAt(eyeRX(0) + EYE_W / 2, eyeCY(), C_BLACK, scale);
}

// ═════════════════════════════════════════════════════════════
//  4. SPARKLE EYES  (★ ★)  — plus / X cross
// ═════════════════════════════════════════════════════════════

void drawSparkleAt(int16_t cx, int16_t cy, uint16_t col, float scale, bool rotated) {
  if (scale <= 0.01) return;
  int arm   = (int)roundf(18 * scale);   // arm length (was 12)
  int thick = 4;                         // bar thickness (was 3)
  if (arm < 2) arm = 2;
  if (!rotated) {
    // + shape
    tft.fillRect(cx - thick/2, cy - arm,     thick, arm * 2, col);
    tft.fillRect(cx - arm,     cy - thick/2, arm * 2, thick, col);
    // small diamond tips to make it look like a 4-point star, not a plus
    tft.fillTriangle(cx - thick/2, cy - arm,         cx + thick/2, cy - arm,         cx,         cy - arm - 4, col);
    tft.fillTriangle(cx - thick/2, cy + arm - 1,     cx + thick/2, cy + arm - 1,     cx,         cy + arm + 3, col);
    tft.fillTriangle(cx - arm,     cy - thick/2,     cx - arm,     cy + thick/2,     cx - arm - 4, cy,         col);
    tft.fillTriangle(cx + arm - 1, cy - thick/2,     cx + arm - 1, cy + thick/2,     cx + arm + 3, cy,         col);
  } else {
    // X shape (4-point star with diagonal arms)
    for (int i = -arm + thick; i <= arm - thick; i++) {
      tft.drawPixel(cx + i, cy + i, col);
      tft.drawPixel(cx + i, cy - i, col);
    }
    // fat pixels at center for visual weight
    tft.fillRect(cx - thick/2, cy - thick/2, thick, thick, col);
  }
}

void drawSparkleEyes(float scale, bool rotated) {
  tft.fillScreen(animBgColor);
  drawSparkleAt(eyeLX(0) + EYE_W / 2, eyeCY(), C_BLACK, scale, rotated);
  drawSparkleAt(eyeRX(0) + EYE_W / 2, eyeCY(), C_BLACK, scale, rotated);
}

// ═════════════════════════════════════════════════════════════
//  5. WINK  (left eye closed, right eye small)
// ═════════════════════════════════════════════════════════════

void drawWinkEyes(float progress) {
  // progress: 0 = both eyes normal, 1 = full wink
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  // Left eye shrinks from EYE_H to 6 as progress → 1
  int16_t leftH = (int16_t)roundf(EYE_H * (1.0 - progress));
  if (leftH < 6) leftH = 6;
  tft.fillRect(lx, cy - leftH / 2, EYE_W, leftH, C_BLACK);
  // Right eye: bigger open dot — sized to read as an eye, not a smudge
  const int16_t rW = EYE_W - 6;    // 24 px wide (was 14)
  const int16_t rH = 34;           // 34 px tall (was 20)
  tft.fillRect(rx + 3, cy - rH / 2, rW, rH, C_BLACK);
}

// ═════════════════════════════════════════════════════════════
//  6. SLEEPY  (— —  +  Z)
// ═════════════════════════════════════════════════════════════

void drawZAt(int16_t x, int16_t y, uint16_t col) {
  // Bigger Z — 22x30 px so it reads clearly at 240x240
  const int w = 22, h = 30, t = 4;
  // Top bar
  tft.fillRect(x, y, w, t, col);
  // Diagonal — stair-stepped for chunky look on small display
  for (int i = 0; i < h - t * 2; i++) {
    tft.drawLine(x + w - t - i,     y + t + i,
                 x + w - t - i + 1, y + t + i + 1, col);
    if (i < h - t * 2 - 1) {
      tft.drawLine(x + w - t - i,     y + t + i + 1,
                   x + w - t - i + 1, y + t + i + 2, col);
    }
  }
  // Bottom bar
  tft.fillRect(x, y + h - t, w, t, col);
}

void drawSleepyEyes(int16_t eyeH, int8_t zFrame) {
  // eyeH = current eye-opening height (animates EYE_H → 6)
  // zFrame = -1 → no Z, 0/1/2 → which Z to draw at upper-right
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  tft.fillRect(lx, cy - eyeH / 2, EYE_W, eyeH, C_BLACK);
  tft.fillRect(rx, cy - eyeH / 2, EYE_W, eyeH, C_BLACK);
  if (zFrame >= 0) {
    // Three Z's stacked upper-right, drifting up; choose which to show
    // Spacing matched to new Z size (22 wide × 30 tall)
    const int16_t baseX = 155;
    const int16_t baseY = 110;
    if (zFrame == 0) drawZAt(baseX,      baseY,      C_BLACK);
    if (zFrame == 1) drawZAt(baseX + 16, baseY - 18, C_BLACK);
    if (zFrame == 2) drawZAt(baseX + 32, baseY - 36, C_BLACK);
  }
}

// ═════════════════════════════════════════════════════════════
//  7. SURPRISE  (hidden 7th expression — used by easter egg)
// ═════════════════════════════════════════════════════════════

void drawSurpriseEyes() {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0) + EYE_W / 2;
  const int16_t rx = eyeRX(0) + EYE_W / 2;
  const int16_t cy = eyeCY();
  // Open-ring eyes: black circle with orange dot inside
  tft.drawCircle(lx, cy, 14, C_BLACK);
  tft.drawCircle(rx, cy, 14, C_BLACK);
  tft.fillCircle(lx, cy, 5, animBgColor);
  tft.fillCircle(rx, cy, 5, animBgColor);
}

// ═════════════════════════════════════════════════════════════
//  ANIMATIONS
// ═════════════════════════════════════════════════════════════

void animNormalEyes() {
  const int16_t offs[] = {-16, 16, -16, 16, 0};
  for (uint8_t i = 0; i < 5; i++) { drawNormalEyes(offs[i]); delay(speedMs(80)); }
  drawNormalEyes(0, true);  delay(speedMs(100));
  drawNormalEyes(0, false); delay(speedMs(70));
  drawNormalEyes(0, true);  delay(speedMs(70));
  drawNormalEyes(0, false);
  delay(2500);   // hold final frame
}

void animSquishEyes() {
  for (uint8_t i = 0; i < 3; i++) {
    drawSquishEyes(false); delay(speedMs(160));
    drawSquishEyes(true);  delay(speedMs(100));
  }
  drawSquishEyes(false);
  delay(2000);
}

void animHeartEyes() {
  // Grow from 0 → 1
  for (int s = 0; s <= 10; s++) {
    drawHeartEyes(s / 10.0);
    delay(speedMs(50));
  }
  // Pulse twice
  drawHeartEyes(1.15); delay(speedMs(120));
  drawHeartEyes(1.00); delay(speedMs(120));
  drawHeartEyes(1.15); delay(speedMs(120));
  drawHeartEyes(1.00);
  delay(2000);
}

void animSparkleEyes() {
  // Grow in
  for (int s = 0; s <= 10; s++) {
    drawSparkleEyes(s / 10.0, false);
    delay(speedMs(50));
  }
  // Twinkle (X shape) twice
  drawSparkleEyes(1.0, true);  delay(speedMs(150));
  drawSparkleEyes(1.0, false); delay(speedMs(150));
  drawSparkleEyes(1.0, true);  delay(speedMs(150));
  drawSparkleEyes(1.0, false);
  delay(1500);
}

void animWink() {
  // Slowly close left eye, right eye stays small throughout
  for (int p = 0; p <= 10; p++) {
    drawWinkEyes(p / 10.0);
    delay(speedMs(60));
  }
  delay(2500);
}

void animSleepy() {
  // 1. Eyes slowly close
  for (int h = EYE_H; h >= 6; h -= 6) {
    drawSleepyEyes(h, -1);
    delay(speedMs(80));
  }
  drawSleepyEyes(6, -1);
  delay(600);
  // 2. Z's float up one by one
  for (int z = 0; z <= 2; z++) {
    drawSleepyEyes(6, z);
    delay(speedMs(700));
  }
  delay(800);
}

// ═════════════════════════════════════════════════════════════
//  DEMO STEPS
// ═════════════════════════════════════════════════════════════

void demoNormal()  { animNormalEyes();  }
void demoSquish()  { animSquishEyes();  }
void demoHeart()   { animHeartEyes();   }
void demoSparkle() { animSparkleEyes(); }
void demoWink()    { animWink();        }
void demoSleepy()  { animSleepy();      }

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(TFT_BLK, OUTPUT);
  setBacklight(true);

  SPI.begin(8, -1, 10, TFT_CS);   // SCK=8, MISO=-1, MOSI=10, CS=TFT_CS
  tft.init(240, 240);
  tft.setSPISpeed(40000000);
  tft.setRotation(1);
  initColours();

  // ── Boot splash ────────────────────────────────────────────
  tft.fillScreen(animBgColor);
  tft.setTextColor(C_WHITE); tft.setTextSize(3);
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 - 22); tft.print("Clawd");
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 + 14); tft.print("Mochi");
  delay(1200);

  Serial.println("[BOOT] No-WiFi demo — cycling 6 expressions forever.");
  setupDynamics();
  setupSpeech();
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════


void loop() {
  static uint8_t step = 0;
  uint8_t lastDemo = step;

  // 0. Clear speech bubble only if it has expired (timing-based, not loop-based)
  if (shouldExpireQuip()) {
    clearQuipArea();
  }

  // 1. Mood check (may transition, which plays a quick "aha" animation)
  updateMood();

  // 2. Main expression
  switch (lastDemo) {
    case DEMO_NORMAL:  animNormalEyes();  break;
    case DEMO_SQUISH:  animSquishEyes();  break;
    case DEMO_HEART:   animHeartEyes();   break;
    case DEMO_SPARKLE: animSparkleEyes(); break;
    case DEMO_WINK:    animWink();        break;
    case DEMO_SLEEPY:  animSleepy();      break;
  }

  // 3. Reaction chain (~30-80% probability depending on trigger)
  reactAfter(lastDemo);

  // 4. Idle micro-animation (~60% probability)
  if (random(100) < 60) {
    playRandomIdle();
  }

  // 5. Easter egg (mood-dependent probability)
  if (shouldEasterEgg()) {
    playEasterEgg();
  }

  // 6. Random speech bubble (mood-dependent probability, with 8s cooldown)
  if (shouldSpeak()) {
    showQuip(pickQuip());
  }

  step = (step + 1) % DEMO_COUNT;
}
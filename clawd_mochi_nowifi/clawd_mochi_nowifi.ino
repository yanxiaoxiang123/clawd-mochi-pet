/*
 * ╔══════════════════════════════════════════════════════════════╗
 *   CLAWD MOCHI — Smart Desk Pet Edition (Zero Extra Hardware)
 *
 *   Hardware: ESP32-C3 Super Mini + ST7789 1.54" 240x240 SPI TFT
 *
 *   Upgrades:
 *     - 15 pure-geometric pixel expressions (zero bitmap memory)
 *     - Natural circadian & 3-min nap/wake rhythm + PWM backlight
 *     - NVS persistent memory (boot count, companion hours, affection)
 *     - BLE near-field "Tap-to-Pet" (Apple Watch / iPhone proximity)
 *     - Onboard silicon temperature sensing + Mini-Theater monologues
 *     - USB Serial Mac & AI coding agent live companion + 3px Pomodoro
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <esp_arduino_version.h>
#include <Preferences.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 4
  #include <BLE.h>
#else
  #include <BLEDevice.h>
#endif
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <math.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "mochi_types.h"

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS   4
#define TFT_DC   1
#define TFT_RST  2
#define TFT_BLK  3

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ── Display & Eye Geometry ────────────────────────────────────
#define DISP_W  240
#define DISP_H  240
#define EYE_W   30
#define EYE_H   60
#define EYE_GAP 120
#define EYE_OX  0
#define EYE_OY  40

// ── Colours ───────────────────────────────────────────────────
uint16_t C_ORANGE, C_DARKBG, C_MUTED, C_GREEN, C_TEAR, C_BUBBLE_BORDER;
uint16_t C_CYBER, C_SAKURA, C_AMBER, C_MINT, C_GOLD, C_CAP_BLUE, C_RED_BAND;
#define C_WHITE ST77XX_WHITE
#define C_BLACK ST77XX_BLACK

uint16_t   animBgColor       = 0;
uint8_t    animSpeed         = 2;   // 1=slow, 2=normal, 3=fast
uint8_t    currentExprId     = DEMO_HAPPY_ARC;
uint8_t    currentBrightness = 255;
uint8_t    targetBrightness  = 255;
uint32_t   lastBrightTickMs  = 0;
uint8_t    themeSetting      = THEME_AUTO;
uint8_t    hatSetting        = HAT_AUTO;
uint8_t    lastAppliedTheme  = 255;
uint8_t    lastAppliedHat    = 255;

MochiEvent pendingEvent      = EVT_NONE;
uint8_t    pendingCustomExpr = 0;
uint8_t    pendingSubId      = 0;
AiMode     aiActiveMode      = AI_MODE_NONE;
uint32_t   aiModeSinceMs     = 0;

// Forward helpers needed by headers
int speedMs(int ms) {
  if (animSpeed == 3) return (ms * 3) / 5;
  if (animSpeed == 1) return (ms * 7) / 5;
  return ms;
}

void setTargetBrightness(uint8_t level) {
  targetBrightness = level;
}

#include "dynamics-sensors.h"
#include "dynamics-speech.h"
#include "dynamics-link.h"
#include "dynamics.h"

// ═════════════════════════════════════════════════════════════
//  PWM BACKLIGHT & INTERRUPTIBLE SMART DELAY
// ═════════════════════════════════════════════════════════════

void tickBacklight() {
  uint32_t now = millis();
  if (now - lastBrightTickMs < 25) return;
  lastBrightTickMs = now;

  uint8_t goal = targetBrightness;
  // Subtle cozy breathing wave while napping!
  if (currentMood == MOOD_SLEEPY && isOwnerPresent()) {
    float wave = sinf((float)now * 0.0022f);  // ~2.8s gentle breath cycle
    int breath = (int)goal + (int)(wave * 22.0f);
    goal = (uint8_t)constrain(breath, 35, 130);
  }

  if (currentBrightness < goal) {
    uint8_t step = (goal - currentBrightness > 8) ? 5 : 1;
    currentBrightness += step;
    analogWrite(TFT_BLK, currentBrightness);
  } else if (currentBrightness > goal) {
    uint8_t step = (currentBrightness - goal > 8) ? 4 : 1;
    currentBrightness -= step;
    analogWrite(TFT_BLK, currentBrightness);
  }
}

static void serviceBackgroundTasks() {
  pollSerialLink();
  pollSensors();
  tickSpeech();
  tickBacklight();
  applyThemeAndRedraw(false);
}

// Short animation-step wait; aborts early if an interactive event arrives
bool frameDelay(int ms) {
  if (ms <= 0) {
    serviceBackgroundTasks();
    return pendingEvent != EVT_NONE;
  }
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < (uint32_t)ms) {
    serviceBackgroundTasks();
    if (pendingEvent != EVT_NONE) return true;
    delay(12);
  }
  return pendingEvent != EVT_NONE;
}

// Expression hold wait; breaks out immediately (<15ms) on any event
bool holdDelay(uint32_t ms) {
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < ms) {
    serviceBackgroundTasks();
    if (pendingEvent != EVT_NONE) return true;
    delay(15);
  }
  return false;
}

void initColours() {
  C_ORANGE        = tft.color565(218,  17,   0);
  C_DARKBG        = tft.color565( 35,  10,   8);
  C_MUTED         = tft.color565( 90,  88,  86);
  C_GREEN         = tft.color565( 95, 235, 140);
  C_TEAR          = tft.color565( 95, 205, 255);
  C_BUBBLE_BORDER = tft.color565(255, 224, 195);
  C_CYBER         = tft.color565(  0, 184, 212);
  C_SAKURA        = tft.color565(245, 108, 158);
  C_AMBER         = tft.color565(184, 112,  16);
  C_MINT          = tft.color565( 36, 196, 126);
  C_GOLD          = tft.color565(255, 214,  40);
  C_CAP_BLUE      = tft.color565( 58, 130, 246);
  C_RED_BAND      = tft.color565(225,  32,  42);
  animBgColor     = C_ORANGE;
}

uint8_t getEffectiveTheme() {
  if (themeSetting != THEME_AUTO && themeSetting < THEME_COUNT) return themeSetting;
  if (aiActiveMode == AI_MODE_THINKING) return THEME_CYBER;
  if (isLateNightHours() || currentMood == MOOD_SLEEPY) return THEME_AMBER;
  if (pomoActive) return THEME_MINT;
  return THEME_CORAL;
}

uint8_t getEffectiveHat() {
  if (hatSetting != HAT_AUTO && hatSetting < HAT_COUNT) return hatSetting;
  if (aiActiveMode == AI_MODE_THINKING) return HAT_HEADPHONES;
  if (pomoActive) return HAT_HEADBAND;
  if (currentMood == MOOD_SLEEPY || isLateNightHours()) return HAT_SLEEP_CAP;
  if (getAffectionLevel() >= 5) return HAT_CROWN;
  return HAT_NONE;
}

uint16_t colorForTheme(uint8_t t) {
  switch (t) {
    case THEME_CYBER:  return C_CYBER;
    case THEME_SAKURA: return C_SAKURA;
    case THEME_AMBER:  return C_AMBER;
    case THEME_MINT:   return C_MINT;
    case THEME_CORAL:
    default:           return C_ORANGE;
  }
}

void drawActiveAccessory() {
  uint8_t h = getEffectiveHat();
  if (h == HAT_NONE) return;

  if (h == HAT_CROWN) {
    // Minimalist golden pixel crown centered above eyes (y=10..31)
    if (currentExprId == DEMO_THINKING) return;
    int16_t cx = 120, by = 31;
    // Black outline
    tft.fillRect(cx - 22, by - 12, 44, 14, C_BLACK);
    tft.fillTriangle(cx - 22, by - 12, cx - 22, by - 22, cx - 10, by - 12, C_BLACK);
    tft.fillTriangle(cx - 8,  by - 12, cx,      by - 25, cx + 8,  by - 12, C_BLACK);
    tft.fillTriangle(cx + 10, by - 12, cx + 22, by - 22, cx + 22, by - 12, C_BLACK);
    // Gold fill
    tft.fillRect(cx - 19, by - 10, 38, 10, C_GOLD);
    tft.fillTriangle(cx - 19, by - 10, cx - 19, by - 18, cx - 10, by - 10, C_GOLD);
    tft.fillTriangle(cx - 6,  by - 10, cx,      by - 21, cx + 6,  by - 10, C_GOLD);
    tft.fillTriangle(cx + 10, by - 10, cx + 19, by - 18, cx + 19, by - 10, C_GOLD);
    // Tiny gem highlights
    tft.fillRect(cx - 2, by - 7, 4, 4, C_RED_BAND);
    tft.fillRect(cx - 13, by - 6, 3, 3, C_WHITE);
    tft.fillRect(cx + 10, by - 6, 3, 3, C_WHITE);
  } else if (h == HAT_HEADPHONES) {
    // Sleek cyber pixel headphones framing the top & sides (never overlaps eyes or thinking dots!)
    tft.fillRect(14, 7, 212, 6, C_BLACK);
    tft.fillRect(20, 9, 200, 2, C_WHITE);
    tft.fillRect(10, 12, 6, 48, C_BLACK);
    tft.fillRect(224, 12, 6, 48, C_BLACK);
    // Left & right earcups
    tft.fillRoundRect(4,   56, 16, 44, 5, C_BLACK);
    tft.fillRoundRect(220, 56, 16, 44, 5, C_BLACK);
    tft.fillRect(8,   64, 5, 28, C_WHITE);
    tft.fillRect(227, 64, 5, 28, C_WHITE);
  } else if (h == HAT_SLEEP_CAP) {
    if (currentExprId == DEMO_THINKING) return;
    // Cozy tilted pixel sleep cap with white brim & pom-pom
    tft.fillTriangle(88, 28, 152, 28, 134, 7, C_CAP_BLUE);
    tft.drawTriangle(88, 28, 152, 28, 134, 7, C_BLACK);
    tft.fillRoundRect(84, 24, 72, 9, 4, C_WHITE);
    tft.drawRoundRect(84, 24, 72, 9, 4, C_BLACK);
    tft.fillCircle(138, 10, 6, C_WHITE);
    tft.drawCircle(138, 10, 6, C_BLACK);
  } else if (h == HAT_HEADBAND) {
    if (currentExprId == DEMO_THINKING) return;
    // Focus headband across forehead with red knot & fluttering tail
    tft.fillRect(16, 23, 208, 9, C_WHITE);
    tft.drawRect(16, 23, 208, 9, C_BLACK);
    tft.fillRect(106, 24, 28, 7, C_RED_BAND);
    // Fluttering ribbon tails on right edge
    tft.fillTriangle(222, 26, 236, 20, 234, 27, C_WHITE);
    tft.fillTriangle(222, 29, 238, 34, 232, 38, C_WHITE);
  }
}

void applyThemeAndRedraw(bool forceRedraw) {
  uint8_t et = getEffectiveTheme();
  uint8_t eh = getEffectiveHat();
  if (!forceRedraw && et == lastAppliedTheme && eh == lastAppliedHat) return;
  bool bgChanged = (et != lastAppliedTheme) || forceRedraw;
  lastAppliedTheme = et;
  lastAppliedHat   = eh;
  animBgColor      = colorForTheme(et);
  if (bgChanged) {
    tft.fillScreen(animBgColor);
    redrawQuipIfActive();
    redrawPomodoroBar(true);
  }
}

void setThemeSetting(uint8_t t) {
  if (t >= THEME_COUNT) return;
  themeSetting = t;
  mochiPrefs.putUChar("theme", themeSetting);
  applyThemeAndRedraw(true);
  pendingCustomExpr = currentExprId;
  pendingEvent      = EVT_CUSTOM_EXPR;
}

void setHatSetting(uint8_t h) {
  if (h >= HAT_COUNT) return;
  hatSetting = h;
  mochiPrefs.putUChar("hat", hatSetting);
  applyThemeAndRedraw(true);
  pendingCustomExpr = currentExprId;
  pendingEvent      = EVT_CUSTOM_EXPR;
}

inline int16_t eyeLX(int16_t ox) {
  return (DISP_W - (EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox;
}
inline int16_t eyeRX(int16_t ox) { return eyeLX(ox) + EYE_W + EYE_GAP; }
inline int16_t eyeY()            { return (DISP_H - EYE_H) / 2 - EYE_OY; }
inline int16_t eyeCY()           { return eyeY() + EYE_H / 2; }
inline void    clearEyeCanvas()  {
  tft.fillRect(0, 0, DISP_W, 195, animBgColor);
  drawActiveAccessory();
}

// ═════════════════════════════════════════════════════════════
//  15 PURE-GEOMETRIC EXPRESSIONS
// ═════════════════════════════════════════════════════════════

// 0. NORMAL EYES (square blocks)
void drawNormalEyesOffset(int16_t ox, int16_t oy, bool blink) {
  currentExprId = DEMO_NORMAL;
  clearEyeCanvas();
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeY() + oy;
  if (!blink) {
    tft.fillRect(lx, ey, EYE_W, EYE_H, C_BLACK);
    tft.fillRect(rx, ey, EYE_W, EYE_H, C_BLACK);
  } else {
    tft.fillRect(lx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
    tft.fillRect(rx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
  }
}

void drawNormalEyes(int16_t ox = 0, bool blink = false) {
  drawNormalEyesOffset(ox, 0, blink);
}

// 1. SQUISH EYES (> < happy chevron)
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
  currentExprId = DEMO_SQUISH;
  clearEyeCanvas();
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  if (!closed) {
    drawChevron(lx + EYE_W / 2, cy, EYE_H / 2, EYE_W / 2, 10, true,  C_BLACK);
    drawChevron(rx + EYE_W / 2, cy, EYE_H / 2, EYE_W / 2, 10, false, C_BLACK);
  } else {
    tft.fillRect(lx, cy - 5, EYE_W, 10, C_BLACK);
    tft.fillRect(rx, cy - 5, EYE_W, 10, C_BLACK);
  }
}

// 2. HEART EYES (♥ ♥)
void drawHeartEyeAt(int16_t cx, int16_t cy, uint16_t col, float scale) {
  if (scale <= 0.01f) return;
  int r     = (int)roundf(9  * scale);
  int dx    = (int)roundf(9  * scale);
  int dyOff = (int)roundf(5  * scale);
  int halfW = (int)roundf(17 * scale);
  int halfH = (int)roundf(20 * scale);
  if (r < 1) r = 1;
  tft.fillCircle(cx - dx, cy - dyOff, r, col);
  tft.fillCircle(cx + dx, cy - dyOff, r, col);
  tft.fillTriangle(cx - halfW, cy - 2, cx + halfW, cy - 2, cx, cy + halfH, col);
}

void drawHeartEyes(float scale) {
  currentExprId = DEMO_HEART;
  clearEyeCanvas();
  drawHeartEyeAt(eyeLX(0) + EYE_W / 2, eyeCY(), C_BLACK, scale);
  drawHeartEyeAt(eyeRX(0) + EYE_W / 2, eyeCY(), C_BLACK, scale);
}

// 3. SPARKLE EYES (★ ★ bold 4-point star)
void drawSparkleAt(int16_t cx, int16_t cy, uint16_t col, float scale, bool rotated) {
  if (scale <= 0.01f) return;
  int arm   = (int)roundf(18 * scale);
  int thick = 4;
  if (arm < 2) arm = 2;
  if (!rotated) {
    tft.fillRect(cx - thick/2, cy - arm,     thick, arm * 2, col);
    tft.fillRect(cx - arm,     cy - thick/2, arm * 2, thick, col);
    tft.fillTriangle(cx - thick/2, cy - arm,     cx + thick/2, cy - arm,     cx,           cy - arm - 5, col);
    tft.fillTriangle(cx - thick/2, cy + arm - 1, cx + thick/2, cy + arm - 1, cx,           cy + arm + 4, col);
    tft.fillTriangle(cx - arm,     cy - thick/2, cx - arm,     cy + thick/2, cx - arm - 5, cy,           col);
    tft.fillTriangle(cx + arm - 1, cy - thick/2, cx + arm - 1, cy + thick/2, cx + arm + 4, cy,           col);
  } else {
    for (int t = -2; t <= 2; t++) {
      tft.drawLine(cx - arm + 3, cy - arm + 3 + t, cx + arm - 3, cy + arm - 3 + t, col);
      tft.drawLine(cx - arm + 3, cy + arm - 3 + t, cx + arm - 3, cy - arm + 3 + t, col);
    }
    tft.fillCircle(cx, cy, 4, col);
  }
}

void drawSparkleEyes(float scale, bool rotated) {
  currentExprId = DEMO_SPARKLE;
  clearEyeCanvas();
  drawSparkleAt(eyeLX(0) + EYE_W / 2, eyeCY(), C_BLACK, scale, rotated);
  drawSparkleAt(eyeRX(0) + EYE_W / 2, eyeCY(), C_BLACK, scale, rotated);
}

// 4. WINK
void drawWinkEyes(float progress) {
  currentExprId = DEMO_WINK;
  clearEyeCanvas();
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  int16_t leftH = (int16_t)roundf(EYE_H * (1.0f - progress));
  if (leftH < 6) leftH = 6;
  tft.fillRect(lx, cy - leftH / 2, EYE_W, leftH, C_BLACK);
  const int16_t rW = EYE_W - 4;
  const int16_t rH = 38;
  tft.fillRect(rx + 2, cy - rH / 2, rW, rH, C_BLACK);
}

// 5. SLEEPY (— — + Z)
void drawZAt(int16_t x, int16_t y, uint16_t col) {
  const int w = 22, h = 30, t = 4;
  tft.fillRect(x, y, w, t, col);
  for (int i = 0; i < h - t * 2; i++) {
    tft.drawLine(x + w - t - i, y + t + i, x + w - t - i + 2, y + t + i, col);
  }
  tft.fillRect(x, y + h - t, w, t, col);
}

void drawSleepyEyes(int16_t eyeH, int8_t zFrame) {
  currentExprId = DEMO_SLEEPY;
  clearEyeCanvas();
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  tft.fillRect(lx, cy - eyeH / 2, EYE_W, eyeH, C_BLACK);
  tft.fillRect(rx, cy - eyeH / 2, EYE_W, eyeH, C_BLACK);
  if (zFrame >= 0) {
    const int16_t baseX = 155, baseY = 108;
    if (zFrame == 0) drawZAt(baseX,      baseY,      C_BLACK);
    if (zFrame == 1) drawZAt(baseX + 16, baseY - 18, C_BLACK);
    if (zFrame == 2) drawZAt(baseX + 32, baseY - 36, C_BLACK);
  }
}

// 6. SURPRISE (⊙ ⊙ wide double ring)
void drawSurpriseEyesJitter(int16_t px = 0, int16_t py = 0) {
  currentExprId = DEMO_SURPRISE;
  clearEyeCanvas();
  const int16_t lcx = eyeLX(0) + EYE_W / 2;
  const int16_t rcx = eyeRX(0) + EYE_W / 2;
  const int16_t cy  = eyeCY();
  tft.fillCircle(lcx, cy, 20, C_BLACK);
  tft.fillCircle(rcx, cy, 20, C_BLACK);
  tft.fillCircle(lcx, cy, 12, animBgColor);
  tft.fillCircle(rcx, cy, 12, animBgColor);
  tft.fillCircle(lcx + px, cy + py, 5, C_BLACK);
  tft.fillCircle(rcx + px, cy + py, 5, C_BLACK);
}

void drawSurpriseEyes() {
  drawSurpriseEyesJitter(0, 0);
}

// 7. HAPPY ARC (^ ^ warm smiling crescent eyes)
void drawHappyArcAt(int16_t cx, int16_t cy) {
  tft.fillCircle(cx, cy + 6, 20, C_BLACK);
  tft.fillCircle(cx, cy + 12, 14, animBgColor);
  tft.fillRect(cx - 22, cy + 8, 44, 24, animBgColor);
}

void drawHappyArcEyes(int16_t bounceY = 0) {
  currentExprId = DEMO_HAPPY_ARC;
  clearEyeCanvas();
  const int16_t cy = eyeCY() + bounceY;
  drawHappyArcAt(eyeLX(0) + EYE_W / 2, cy);
  drawHappyArcAt(eyeRX(0) + EYE_W / 2, cy);
}

// 8. THINKING / AI CODING (scanning gaze + animated 3 thinking dots above)
void drawThinkingEyes(int16_t ox, uint8_t dotStep) {
  currentExprId = DEMO_THINKING;
  clearEyeCanvas();
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), cy = eyeCY() + 4;
  const int16_t h  = 42;
  tft.fillRect(lx, cy - h / 2, EYE_W, h, C_BLACK);
  tft.fillRect(rx, cy - h / 2, EYE_W, h, C_BLACK);

  // Three animated thinking dots centered near top
  for (uint8_t i = 0; i < 3; i++) {
    int16_t dx = DISP_W / 2 - 22 + i * 22;
    int16_t dy = 26 - ((dotStep % 3 == i) ? 6 : 0);
    int16_t r  = (dotStep % 3 == i) ? 5 : 3;
    tft.fillCircle(dx, dy, r, (dotStep % 3 == i) ? C_WHITE : C_BLACK);
  }
}

// 9. ANGRY / TSUNDERE (\ / slanted brows + cute comic vein)
void drawAngryEyes(int16_t shakeX = 0) {
  currentExprId = DEMO_ANGRY;
  clearEyeCanvas();
  const int16_t lx = eyeLX(shakeX), rx = eyeRX(shakeX), ey = eyeY() + 4;
  tft.fillRect(lx, ey, EYE_W, 50, C_BLACK);
  tft.fillRect(rx, ey, EYE_W, 50, C_BLACK);
  // Mask inner-top triangles to create sharp \ / angry slant
  tft.fillTriangle(lx - 2, ey - 2, lx + EYE_W + 2, ey - 2, lx + EYE_W + 2, ey + 22, animBgColor);
  tft.fillTriangle(rx - 2, ey - 2, rx + EYE_W + 2, ey - 2, rx - 2,         ey + 22, animBgColor);
  // Cute pixel anger mark at top-right
  int16_t vx = 198, vy = 28;
  tft.fillRect(vx - 8, vy - 2, 6, 3, C_WHITE);
  tft.fillRect(vx - 4, vy - 8, 3, 6, C_WHITE);
  tft.fillRect(vx + 3, vy - 2, 6, 3, C_WHITE);
  tft.fillRect(vx + 2, vy + 3, 3, 6, C_WHITE);
}

// 10. DIZZY / OVERLOAD (X X cross eyes + sweat drop)
void drawDizzyEyes(bool altPhase) {
  currentExprId = DEMO_DIZZY;
  clearEyeCanvas();
  const int16_t lcx = eyeLX(0) + EYE_W / 2;
  const int16_t rcx = eyeRX(0) + EYE_W / 2;
  const int16_t cy  = eyeCY();
  int16_t arm = altPhase ? 19 : 16;
  for (int8_t t = -4; t <= 4; t++) {
    tft.drawLine(lcx - arm, cy - arm + t, lcx + arm, cy + arm + t, C_BLACK);
    tft.drawLine(lcx - arm, cy + arm + t, lcx + arm, cy - arm + t, C_BLACK);
    tft.drawLine(rcx - arm, cy - arm + t, rcx + arm, cy + arm + t, C_BLACK);
    tft.drawLine(rcx - arm, cy + arm + t, rcx + arm, cy - arm + t, C_BLACK);
  }
  // Sweat droplet on upper right
  int16_t sy = altPhase ? 40 : 46;
  tft.fillCircle(206, sy + 6, 6, C_TEAR);
  tft.fillTriangle(200, sy + 5, 212, sy + 5, 206, sy - 6, C_TEAR);
}

// 11. CRY / MISSED YOU (T T droopy eyes + falling blue tear)
void drawCryEyes(int16_t tearDropY) {
  currentExprId = DEMO_CRY;
  clearEyeCanvas();
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  // Sad horizontal lids
  tft.fillRect(lx - 4, cy - 10, EYE_W + 8, 12, C_BLACK);
  tft.fillRect(rx - 4, cy - 10, EYE_W + 8, 12, C_BLACK);
  // Falling teardrops beneath eyes
  tft.fillCircle(lx + 8,          cy + 8 + tearDropY, 5, C_TEAR);
  tft.fillCircle(rx + EYE_W - 8,  cy + 8 + ((tearDropY + 12) % 26), 5, C_TEAR);
}

// 12. CURIOUS / QUESTION (o O asymmetric eyes + pixel ?)
void drawCuriousEyes(int16_t tiltOx, bool showQ) {
  currentExprId = DEMO_CURIOUS;
  clearEyeCanvas();
  const int16_t lx = eyeLX(tiltOx), rx = eyeRX(tiltOx), cy = eyeCY();
  // Left eye smaller & raised brow, right eye big & curious
  tft.fillRect(lx, cy - 14, EYE_W, 28, C_BLACK);
  tft.fillRect(rx, cy - 32, EYE_W + 4, 64, C_BLACK);
  if (showQ) {
    int16_t qx = 196, qy = 22;
    tft.fillRect(qx,      qy,      14, 4, C_WHITE);
    tft.fillRect(qx + 10, qy + 4,   4, 8, C_WHITE);
    tft.fillRect(qx + 4,  qy + 10, 10, 4, C_WHITE);
    tft.fillRect(qx + 4,  qy + 14,  4, 6, C_WHITE);
    tft.fillRect(qx + 4,  qy + 24,  4, 4, C_WHITE);
  }
}

// 13. YAWN / STRETCH (eyes squeeze up high then relax)
void drawYawnEyes(int16_t oy, int16_t eyeH) {
  currentExprId = DEMO_YAWN;
  clearEyeCanvas();
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY() + oy;
  tft.fillRect(lx - 2, cy - eyeH / 2, EYE_W + 4, eyeH, C_BLACK);
  tft.fillRect(rx - 2, cy - eyeH / 2, EYE_W + 4, eyeH, C_BLACK);
}

// 14. FOCUS / LASER (determined narrow visor eyes)
void drawFocusEyes(int16_t ox = 0) {
  currentExprId = DEMO_FOCUS;
  clearEyeCanvas();
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), cy = eyeCY();
  tft.fillRect(lx - 3, cy - 11, EYE_W + 6, 22, C_BLACK);
  tft.fillRect(rx - 3, cy - 11, EYE_W + 6, 22, C_BLACK);
  // Crisp highlight glint inside each focused eye
  tft.fillRect(lx + 4, cy - 6, 6, 6, C_WHITE);
  tft.fillRect(rx + 4, cy - 6, 6, 6, C_WHITE);
}

// ═════════════════════════════════════════════════════════════
//  15 ANIMATION SEQUENCES
// ═════════════════════════════════════════════════════════════

void animNormalEyes() {
  const int16_t offs[] = {-16, 16, -16, 16, 0};
  for (uint8_t i = 0; i < 5; i++) {
    drawNormalEyes(offs[i]);
    if (frameDelay(speedMs(80))) return;
  }
  drawNormalEyes(0, true);  if (frameDelay(speedMs(100))) return;
  drawNormalEyes(0, false); if (frameDelay(speedMs(70)))  return;
  drawNormalEyes(0, true);  if (frameDelay(speedMs(70)))  return;
  drawNormalEyes(0, false);
  holdDelay(2300);
}

void animSquishEyes() {
  for (uint8_t i = 0; i < 3; i++) {
    drawSquishEyes(false); if (frameDelay(speedMs(150))) return;
    drawSquishEyes(true);  if (frameDelay(speedMs(95)))  return;
  }
  drawSquishEyes(false);
  holdDelay(1900);
}

void animHeartEyes() {
  for (int s = 2; s <= 10; s += 2) {
    drawHeartEyes(s / 10.0f);
    if (frameDelay(speedMs(45))) return;
  }
  for (uint8_t i = 0; i < 2; i++) {
    drawHeartEyes(1.18f); if (frameDelay(speedMs(115))) return;
    drawHeartEyes(1.00f); if (frameDelay(speedMs(115))) return;
  }
  holdDelay(1900);
}

void animSparkleEyes() {
  for (int s = 2; s <= 10; s += 2) {
    drawSparkleEyes(s / 10.0f, false);
    if (frameDelay(speedMs(45))) return;
  }
  for (uint8_t i = 0; i < 2; i++) {
    drawSparkleEyes(1.0f, true);  if (frameDelay(speedMs(140))) return;
    drawSparkleEyes(1.0f, false); if (frameDelay(speedMs(140))) return;
  }
  holdDelay(1500);
}

void animWink() {
  for (int p = 0; p <= 10; p += 2) {
    drawWinkEyes(p / 10.0f);
    if (frameDelay(speedMs(55))) return;
  }
  holdDelay(2100);
}

void animSleepy() {
  for (int h = EYE_H; h >= 6; h -= 9) {
    drawSleepyEyes(h, -1);
    if (frameDelay(speedMs(75))) return;
  }
  drawSleepyEyes(6, -1);
  if (frameDelay(450)) return;
  for (int z = 0; z <= 2; z++) {
    drawSleepyEyes(6, z);
    if (frameDelay(speedMs(650))) return;
  }
  holdDelay(900);
}

void animSurpriseEyes() {
  drawSurpriseEyesJitter(-3, 0); if (frameDelay(80)) return;
  drawSurpriseEyesJitter( 3, 0); if (frameDelay(80)) return;
  drawSurpriseEyesJitter( 0, -2); if (frameDelay(80)) return;
  drawSurpriseEyesJitter( 0, 0);
  holdDelay(1500);
}

void animHappyArcEyes() {
  const int16_t bounce[] = {0, -10, 0, -8, 0};
  for (uint8_t i = 0; i < 5; i++) {
    drawHappyArcEyes(bounce[i]);
    if (frameDelay(speedMs(95))) return;
  }
  holdDelay(2000);
}

void animThinkingEyes() {
  const int16_t gaze[] = {-12, -12, 0, 12, 12, 0};
  for (uint8_t i = 0; i < 6; i++) {
    drawThinkingEyes(gaze[i], i);
    if (frameDelay(220)) return;
  }
  holdDelay(900);
}

void animAngryEyes() {
  for (uint8_t i = 0; i < 4; i++) {
    drawAngryEyes((i % 2 == 0) ? -4 : 4);
    if (frameDelay(90)) return;
  }
  drawAngryEyes(0);
  holdDelay(1800);
}

void animDizzyEyes() {
  for (uint8_t i = 0; i < 6; i++) {
    drawDizzyEyes(i % 2 == 0);
    if (frameDelay(150)) return;
  }
  holdDelay(1400);
}

void animCryEyes() {
  for (uint8_t i = 0; i < 8; i++) {
    drawCryEyes((i * 4) % 24);
    if (frameDelay(140)) return;
  }
  holdDelay(900);
}

void animCuriousEyes() {
  drawCuriousEyes(0, false);  if (frameDelay(speedMs(140))) return;
  drawCuriousEyes(6, true);   if (frameDelay(speedMs(220))) return;
  drawCuriousEyes(-4, true);  if (frameDelay(speedMs(180))) return;
  drawCuriousEyes(4, true);
  holdDelay(1800);
}

void animYawnStretch() {
  // Stretch upwards with tightly squeezed eyes, then open wide!
  for (int i = 0; i <= 5; i++) {
    drawYawnEyes(-i * 3, 24 - i * 3);
    if (frameDelay(85)) return;
  }
  if (frameDelay(350)) return;
  for (int i = 5; i >= 0; i--) {
    drawYawnEyes(-i * 2, EYE_H - i * 6);
    if (frameDelay(70)) return;
  }
  drawNormalEyes(0, false);
}

void animFocusEyes() {
  drawFocusEyes(-8); if (frameDelay(speedMs(180))) return;
  drawFocusEyes( 8); if (frameDelay(speedMs(180))) return;
  drawFocusEyes( 0);
  holdDelay(1800);
}

void playExpressionById(uint8_t expr) {
  switch (expr) {
    case DEMO_NORMAL:    animNormalEyes();    break;
    case DEMO_SQUISH:    animSquishEyes();    break;
    case DEMO_HEART:     animHeartEyes();     break;
    case DEMO_SPARKLE:   animSparkleEyes();   break;
    case DEMO_WINK:      animWink();          break;
    case DEMO_SLEEPY:    animSleepy();        break;
    case DEMO_SURPRISE:  animSurpriseEyes();  break;
    case DEMO_HAPPY_ARC: animHappyArcEyes();  break;
    case DEMO_THINKING:  animThinkingEyes();  break;
    case DEMO_ANGRY:     animAngryEyes();     break;
    case DEMO_DIZZY:     animDizzyEyes();     break;
    case DEMO_CRY:       animCryEyes();       break;
    case DEMO_CURIOUS:   animCuriousEyes();   break;
    case DEMO_YAWN:      animYawnStretch();   break;
    case DEMO_FOCUS:     animFocusEyes();     break;
    default:             animNormalEyes();    break;
  }
}

// ═════════════════════════════════════════════════════════════
//  IMMEDIATE INTERACTIVE EVENT DISPATCHER
// ═════════════════════════════════════════════════════════════

void handlePendingEvent() {
  if (pendingEvent == EVT_NONE) return;
  MochiEvent ev = pendingEvent;
  pendingEvent  = EVT_NONE;

  switch (ev) {
    case EVT_PET: {
      bool wokeUp = (currentMood == MOOD_SLEEPY);
      addAffection(1);
      energizeCycle(true);
      if (wokeUp) {
        showQuip("唔..被你贴贴叫醒啦~", 3200);
        animYawnStretch();
        animHappyArcEyes();
      } else {
        showQuip(pickPetQuip(), 3200);
        if (random(2) == 0) animHeartEyes();
        else                animHappyArcEyes();
      }
      break;
    }

    case EVT_WELCOME_BACK: {
      energizeCycle(true);
      showQuip("你回来啦! 欢迎回座~", 3400);
      animHappyArcEyes();
      break;
    }

    case EVT_LONG_AWAY: {
      energizeCycle(true);
      showQuip("你去哪了嘛..好想你!", 3500);
      animCryEyes();
      showQuip("回来就好啦~ <3", 3000);
      animHeartEyes();
      break;
    }

    case EVT_AI_THINK: {
      if (currentMood == MOOD_SLEEPY) energizeCycle(false);
      animThinkingEyes();
      break;
    }

    case EVT_AI_DONE: {
      energizeCycle(false);
      eggFlash();
      animHappyArcEyes();
      animSparkleEyes();
      break;
    }

    case EVT_AI_ASK: {
      energizeCycle(false);
      animSurpriseEyes();
      animCuriousEyes();
      break;
    }

    case EVT_POMO_START: {
      energizeCycle(false);
      redrawPomodoroBar(true);
      animFocusEyes();
      break;
    }

    case EVT_POMO_DONE: {
      energizeCycle(true);
      eggFlash();
      showQuip("专注达成! 起来歇会儿~", 4200);
      animSparkleEyes();
      animHappyArcEyes();
      break;
    }

    case EVT_CPU_HIGH: {
      animDizzyEyes();
      break;
    }

    case EVT_SITTING_LONG: {
      if (currentMood == MOOD_SLEEPY) energizeCycle(false);
      animAngryEyes();
      break;
    }

    case EVT_GIT_COMMIT: {
      energizeCycle(false);
      animSparkleEyes();
      animHappyArcEyes();
      break;
    }

    case EVT_CUSTOM_EXPR: {
      playExpressionById(pendingCustomExpr);
      break;
    }

    case EVT_CUSTOM_EGG: {
      if (pendingSubId == 0)      eggZoom();
      else if (pendingSubId == 1) { showQuip("全表情串烧热舞~!", 2400); eggDance(); }
      else if (pendingSubId == 2) eggFlash();
      else                        { showQuip("被你发现了!", 2200); eggSecret(); }
      break;
    }

    case EVT_CUSTOM_IDLE: {
      if (pendingSubId == 0)      idleBlink();
      else if (pendingSubId == 1) idleLookLeft();
      else if (pendingSubId == 2) idleLookRight();
      else if (pendingSubId == 3) idleLookUp();
      else if (pendingSubId == 4) idleBreathe();
      else                        idleShiver();
      break;
    }

    case EVT_NAP_DEMO: {
      currentMood = MOOD_SLEEPY;
      setTargetBrightness(80);
      showQuip("有点困..小憩一下~", 2800);
      animSleepy();
      energizeCycle(false);
      showQuip("睡饱啦! 满血复活~", 3000);
      animYawnStretch();
      animHappyArcEyes();
      break;
    }

    case EVT_BOND_SUCCESS: {
      addAffection(5);
      energizeCycle(true);
      eggFlash();
      showQuip("认主成功! 只认你啦 <3", 4500);
      animHeartEyes();
      animSparkleEyes();
      break;
    }

    case EVT_STRANGER: {
      if (currentMood == MOOD_SLEEPY) energizeCycle(false);
      showQuip("哼! 你不是我主人!", 3500);
      animAngryEyes();
      break;
    }

    case EVT_FEED_FISH: {
      addAffection(2);
      if (currentMood == MOOD_SLEEPY) energizeCycle(false);
      animSquishEyes();
      animHappyArcEyes();
      break;
    }

    case EVT_FEED_COFFEE: {
      bool wokeUp = (currentMood == MOOD_SLEEPY);
      addAffection(2);
      energizeCycle(true);
      if (wokeUp) animYawnStretch();
      animSparkleEyes();
      animHappyArcEyes();
      break;
    }

    case EVT_FEED_MOCHI: {
      addAffection(4);
      energizeCycle(true);
      eggFlash();
      animHeartEyes();
      break;
    }

    case EVT_FEED_TEA: {
      addAffection(2);
      setCalmMood();
      animHappyArcEyes();
      break;
    }

    default:
      break;
  }
}

// ═════════════════════════════════════════════════════════════
//  SETUP & MAIN LOOP
// ═════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(150);

  pinMode(TFT_BLK, OUTPUT);
  analogWrite(TFT_BLK, 255);

  SPI.begin(8, -1, 10, TFT_CS);
  tft.init(240, 240);
  tft.setSPISpeed(40000000);
  tft.setRotation(1);
  initColours();
  u8g2.begin(tft);

  // Initialize NVS memory, onboard temp & BLE radar
  setupSensorsAndMemory();
  setupDynamics();
  setupSpeech();
  applyThemeAndRedraw(true);

  // Clean boot splash + personalized NVS greeting
  tft.fillScreen(animBgColor);
  drawHappyArcEyes(0);

  char greet[48];
  if (getBootCount() <= 1) {
    snprintf(greet, sizeof(greet), "初次见面! 请多关照~");
  } else {
    snprintf(greet, sizeof(greet), "第 %lu 次见面! Lv.%u <3",
             (unsigned long)getBootCount(), getAffectionLevel());
  }
  showQuip(greet, 3600);
  holdDelay(1400);

  Serial.printf("[BOOT] Clawd Mochi Smart Pet ready (boots=%lu, mins=%lu, lv=%u, temp=%.1fC)\n",
                (unsigned long)getBootCount(),
                (unsigned long)getTotalMinutes(),
                getAffectionLevel(),
                getChipTempC());
}

void loop() {
  static uint8_t lastExpr = DEMO_NORMAL;

  // 0. Service serial, BLE, temperature, bubble expiry & instant events
  serviceBackgroundTasks();
  if (pendingEvent != EVT_NONE) {
    handlePendingEvent();
    return;
  }

  // 1. Update natural circadian / nap mood & smooth backlight target
  updateMood();
  if (pendingEvent != EVT_NONE) {
    handlePendingEvent();
    return;
  }

  // 2. Pick and play mood-weighted expression (out of 15 expressions!)
  uint8_t nextExpr = pickNextExpression(lastExpr);
  lastExpr = nextExpr;
  playExpressionById(nextExpr);
  if (pendingEvent != EVT_NONE) {
    handlePendingEvent();
    return;
  }
  redrawQuipIfActive();
  redrawPomodoroBar(false);

  // 3. Follow-up reaction chain
  reactAfter(lastExpr);
  if (pendingEvent != EVT_NONE) {
    handlePendingEvent();
    return;
  }
  redrawQuipIfActive();

  // 4. Weighted idle micro-animation
  if (currentMood != MOOD_SLEEPY && random(100) < 58) {
    playRandomIdle();
    if (pendingEvent != EVT_NONE) {
      handlePendingEvent();
      return;
    }
    redrawQuipIfActive();
  }

  // 5. Occasional Easter egg
  if (shouldEasterEgg()) {
    playEasterEgg();
    if (pendingEvent != EVT_NONE) {
      handlePendingEvent();
      return;
    }
    redrawQuipIfActive();
  }

  // 6. Speech bubble or 2-3 step Mini-Theater monologue
  if (shouldSpeak()) {
    if (currentMood != MOOD_SLEEPY && aiActiveMode == AI_MODE_NONE && random(100) < 12) {
      startMiniTheater(-1);
    } else {
      showQuip(pickDynamicOrSituationalQuip());
    }
  }
}

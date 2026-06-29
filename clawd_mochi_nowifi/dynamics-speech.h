// dynamics-speech.h — Random quips/bubbles for Clawd Mochi
// Companion to dynamics.h. Adds non-blocking speech bubbles
// that drift up over time, with mood-aware text content.
//
// Design notes:
//   - Text is short (<= 14 chars) to fit on 240px wide display
//   - 4 mood groups of quips, picked 80% from current mood / 20% from default
//   - Trigger probability per loop depends on mood (HYPER talks more)
//   - 8 second minimum gap to avoid spam
//   - Bubble is non-blocking: drawn now, cleared at top of next loop
//   - Default quipSet is randomized at boot, so the "voice" varies per power cycle
//
// Time strategy: we use millis() (boot-relative). Drifts ~1% per day.
// Do not reference specific clock times in quip text — use relative language.

#ifndef DYNAMICS_SPEECH_H
#define DYNAMICS_SPEECH_H

#include <Arduino.h>
#include <U8g2_for_Adafruit_GFX.h>

// u8g2 instance defined in main sketch (initialized after tft)
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

// ── Externs we need from main sketch / dynamics.h ─────────────
extern Mood      currentMood;
extern uint8_t   animSpeed;
extern uint16_t  animBgColor;
extern uint16_t  C_ORANGE;
extern uint16_t  C_DARKBG;
#define C_WHITE ST77XX_WHITE
extern int  speedMs(int ms);

// ── Quip pools ──
// Note: Arduino IDE compiles .ino/.h files as UTF-8 by default.
// Chinese characters display fine on the ST7789 as long as the file
// is saved as UTF-8 (it is, by default in Arduino IDE 2.x).

// HYPER — high energy, lots of exclamation marks
const char* QUIPS_HYPER[] = {
  "Let's gooo!",
  "新的一天!",
  "太激动了",
  "今天做啥?",
  "I'm awake!",
  "冲冲冲~",
  "能量满满",
  "GO GO GO!",
};
const uint8_t QUIPS_HYPER_N = sizeof(QUIPS_HYPER) / sizeof(QUIPS_HYPER[0]);

// HAPPY — default mood, mixed casual
const char* QUIPS_HAPPY[] = {
  "吃了吗?",
  "想你呀",
  "摸鱼快乐~",
  "加油龙!",
  "今天也很棒",
  "Hello~",
  "在忙啥呀?",
  "(•ᴗ•)✧",
  "(=^..^=)",
};
const uint8_t QUIPS_HAPPY_N = sizeof(QUIPS_HAPPY) / sizeof(QUIPS_HAPPY[0]);

// CALM — gentle, periods
const char* QUIPS_CALM[] = {
  "慢慢来~",
  "歇会儿吧",
  "喝口水",
  "深呼吸...",
  "(发呆中)",
  "窗外看看",
  "安静宁和",
  "...",
};
const uint8_t QUIPS_CALM_N = sizeof(QUIPS_CALM) / sizeof(QUIPS_CALM[0]);

// SLEEPY — short, low-energy
const char* QUIPS_SLEEPY[] = {
  "zzz...",
  "好困呀",
  "再 5 分钟...",
  "(揉眼睛)",
  "夜深了哦",
  "困毙了",
  "(╯-╰)",
  "mrmm...",
};
const uint8_t QUIPS_SLEEPY_N = sizeof(QUIPS_SLEEPY) / sizeof(QUIPS_SLEEPY[0]);

// ── Speech state ─────────────────────────────────────────────
uint8_t  quipSet        = 0;       // default quip set index (0-3), randomized at boot
uint32_t lastQuipMs     = 0;       // millis() of last shown quip (for cooldown)
uint32_t quipDrawnMs    = 0;       // when the current bubble was drawn (for expire)
uint16_t quipDurationMs = 0;       // how long the current bubble should stay
char     currentQuip[32] = "";     // currently-displayed quip text
bool     quipActive     = false;   // is a bubble currently on screen?
bool     quipNeedsRedraw= false;   // eye animation fillScreen'd over us; redraw at end

// Per-mood trigger probability (out of 100, evaluated per loop)
// Roughly 1 quip every 15-30s in HAPPY, faster in HYPER, slower in CALM/SLEEPY
const uint8_t SPEAK_PROB[4] = { 45, 25, 15, 10 };

// Cooldown between quips
const uint32_t SPEAK_COOLDOWN_MS = 6000;

// ── Setup ────────────────────────────────────────────────────
void setupSpeech() {
  quipSet = random(4);  // pick a "voice" for this boot
  lastQuipMs = 0;       // allow first quip immediately
  quipActive = false;
  quipDrawnMs = 0;
  quipDurationMs = 0;
  quipNeedsRedraw = false;
  currentQuip[0] = '\0';
}

// ── Pick a quip from either current mood (80%) or default set (20%) ──
const char* pickQuip() {
  const char** pool;
  uint8_t      poolSize;
  uint8_t      mood;

  if (random(100) < 80) {
    // 80% — use current mood
    mood = (uint8_t)currentMood;
  } else {
    // 20% — use the boot-time default "voice"
    mood = quipSet;
  }

  switch (mood) {
    case 0: pool = QUIPS_HYPER;  poolSize = QUIPS_HYPER_N;  break;
    case 1: pool = QUIPS_HAPPY;  poolSize = QUIPS_HAPPY_N;  break;
    case 2: pool = QUIPS_CALM;   poolSize = QUIPS_CALM_N;   break;
    case 3: pool = QUIPS_SLEEPY; poolSize = QUIPS_SLEEPY_N; break;
    default: pool = QUIPS_HAPPY; poolSize = QUIPS_HAPPY_N;  break;
  }
  return pool[random(poolSize)];
}

// ── Should we trigger a quip this loop? ──────────────────────
bool shouldSpeak() {
  if (quipActive) return false;  // one at a time
  if (millis() - lastQuipMs < SPEAK_COOLDOWN_MS) return false;
  return random(100) < SPEAK_PROB[(uint8_t)currentMood];
}

// ── Has the current bubble expired? (call from main loop) ─────
bool shouldExpireQuip() {
  if (!quipActive) return false;
  return (millis() - quipDrawnMs) >= quipDurationMs;
}

// ── Draw a quip bubble at the bottom of the screen ───────────
// Position: centered horizontally, ~30px from bottom
// Style: TRANSPARENT background + orange border + white text
// Text rendering uses U8g2 chinese1 font (UTF-8 capable)
void drawQuipBubble(const char* text) {
  if (!text || text[0] == '\0') return;

  // Measure text width using U8g2 (handles UTF-8 Chinese chars + ASCII)
  u8g2.setFont(u8g2_font_unifont_t_chinese1);  // 16x16 Chinese, 8x16 ASCII
  uint16_t textW = u8g2.getUTF8Width(text);
  if (textW == 0) return;
  if (textW > 220) textW = 220;  // safety clamp

  // chinese1 is 16px tall, baseline at +16
  const int16_t padX  = 10;
  const int16_t padY  = 4;
  const int16_t boxH  = 24;  // 4 + 16 + 4
  const int16_t boxW  = textW + padX * 2;
  const int16_t boxX  = (240 - boxW) / 2;
  const int16_t boxY  = 200;  // bottom of screen, y=200..224

  // TRANSPARENT bubble: only the orange border, no background fill
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 4, C_ORANGE);

  // Text via U8g2 — transparent font mode (no opaque background)
  // The bridge U8G2_FOR_ADAFRUIT_GFX has NO u8g2_DrawUTF8 function (only
  // single-byte drawStr). We must decode UTF-8 to Unicode codepoints
  // manually and call drawGlyph(x, y, codepoint) per char.
  u8g2.setFontMode(1);                       // 1 = transparent background
  u8g2.setForegroundColor(C_WHITE);

  int16_t tx = boxX + padX;
  int16_t ty = boxY + padY + 16;             // baseline
  const uint8_t* p = (const uint8_t*)text;
  while (*p) {
    uint16_t cp = 0;
    uint8_t c = *p++;
    if (c < 0x80) {
      cp = c;                                // ASCII (1 byte)
    } else if ((c & 0xE0) == 0xC0) {
      cp = (c & 0x1F) << 6;
      cp |= (*p++ & 0x3F);                   // 2-byte UTF-8
    } else if ((c & 0xF0) == 0xE0) {
      cp = (c & 0x0F) << 12;
      cp |= (*p++ & 0x3F) << 6;
      cp |= (*p++ & 0x3F);                   // 3-byte UTF-8 (Chinese)
    } else if ((c & 0xF8) == 0xF0) {
      cp = (c & 0x07) << 18;
      cp |= (*p++ & 0x3F) << 12;
      cp |= (*p++ & 0x3F) << 6;
      cp |= (*p++ & 0x3F);                   // 4-byte UTF-8
    }
    if (cp != 0) {
      tx += u8g2.drawGlyph(tx, ty, cp);      // drawGlyph returns width
    }
  }
}

// ── Show a quip (call once per trigger) ──────────────────────
void showQuip(const char* text) {
  if (!text) return;
  strncpy(currentQuip, text, sizeof(currentQuip) - 1);
  currentQuip[sizeof(currentQuip) - 1] = '\0';
  // Duration: 1500ms base + 90ms per char
  quipDurationMs = 1500 + strlen(text) * 90;
  lastQuipMs     = millis();
  quipDrawnMs    = millis();
  quipActive     = true;
  drawQuipBubble(text);
}

// ── Clear the bubble area (call only after shouldExpireQuip()) ─
void clearQuipArea() {
  if (!quipActive) return;
  // Overdraw the bubble region with bg color
  tft.fillRect(0, 195, 240, 45, animBgColor);
  quipActive = false;
  quipDurationMs = 0;
  quipNeedsRedraw = false;
  currentQuip[0] = '\0';
}

// ── Re-draw the active bubble on top of whatever the eyes just drew ──
// Call this AFTER any fillScreen() to restore the bubble.
void redrawQuipIfActive() {
  if (quipActive && currentQuip[0] != '\0') {
    drawQuipBubble(currentQuip);
  }
}

#endif // DYNAMICS_SPEECH_H
